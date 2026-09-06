#pragma once

#include <cstdint>

#include <ESPressio_IClockSynchronizationTarget.hpp>
#include <ESPressio_RadioClockSynchronizer.hpp>
#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_DirectPeerBindings.hpp"

namespace ESPressio::Mesh {

enum class MeshSystemClockRole : std::uint8_t {
    Disabled,
    Reference,
    ClientAndReference
};

enum class MeshSystemClockConvergenceDisposition : std::uint8_t {
    Unchanged,
    Disabled,
    ReferenceConfigured,
    ParentConfigured,
    ParentBindingUnavailable,
    TransportConfigurationFailed,
    Invalid
};

/// <summary>Transport boundary used by Mesh to apply an elected direct synchronization relationship.</summary>
class IMeshSystemClockSynchronizationTransport {
public:
    virtual ~IMeshSystemClockSynchronizationTransport() = default;
    virtual bool ConfigureReference() = 0;
    virtual bool ConfigureClientAndReference(
        const AuthenticatedDirectPeerBinding& parent
    ) = 0;
    virtual void Update() = 0;
    virtual void Shutdown() noexcept = 0;
};

/// <summary>
/// Adapts one Radio-owned precision synchronizer to Mesh's authenticated direct-peer binding.
/// </summary>
/// <remarks>
/// Radio retains the T1/T2/T3/T4 packet exchange and timestamp boundary. This adapter only resolves the
/// generation-safe binding selected by Mesh and supplies the resulting opaque Radio address to Radio.
/// </remarks>
class RadioMeshSystemClockSynchronizationTransport final :
    public IMeshSystemClockSynchronizationTransport {
    Radio::RadioClockSynchronizer& _synchronizer;
    Radio::RadioTransport& _transport;
    Radio::IRadio& _radio;
    Radio::RadioClockSynchronizationConfig _baseConfiguration{};

public:
    RadioMeshSystemClockSynchronizationTransport(
        Radio::RadioClockSynchronizer& synchronizer,
        Radio::RadioTransport& transport,
        Radio::IRadio& radio,
        const Radio::RadioClockSynchronizationConfig& baseConfiguration = {}
    ) noexcept :
        _synchronizer(synchronizer),
        _transport(transport),
        _radio(radio),
        _baseConfiguration(baseConfiguration) {}

    bool ConfigureReference() override {
        auto configuration = _baseConfiguration;
        configuration.Mode = Radio::RadioClockSynchronizationMode::Reference;
        configuration.ReferencePeer = {};
        return _synchronizer.Initialize(configuration);
    }

    bool ConfigureClientAndReference(
        const AuthenticatedDirectPeerBinding& parent
    ) override {
        if (!parent.IsValid()) return false;
        const auto* peer = _transport.Peers().Resolve(parent.Peer);
        if (peer == nullptr || !peer->IsValid() || peer->Interface != &_radio) return false;
        auto configuration = _baseConfiguration;
        configuration.Mode = Radio::RadioClockSynchronizationMode::ClientAndReference;
        configuration.ReferencePeer = peer->Address;
        return _synchronizer.Initialize(configuration);
    }

    void Update() override { _synchronizer.Update(); }
    void Shutdown() noexcept override { _synchronizer.Shutdown(); }
};

/// <summary>
/// Applies Mesh root/parent selection to a Radio precision exchange and the ESPressio-Timing System Clock.
/// </summary>
/// <remarks>
/// Mesh owns root/parent authority and refuses a parent without the exact authenticated direct binding. Radio owns
/// exchange mechanics. Timing owns timestamp calculation, filtering, stepping/slewing, drift and synchronization state.
/// Deadline-driven Mesh traffic should use NowMilliseconds() only when IsDeadlineClockReady() is true.
/// </remarks>
class MeshSystemClockSynchronizationCoordinator final {
    Timing::IClockSynchronizationTarget<Timing::ClockTick>& _clock;
    IMeshSystemClockSynchronizationTransport& _transport;
    System::DeviceIdentifier _localDevice{};
    MeshSystemClockRole _role{MeshSystemClockRole::Disabled};
    ClockCoordinationSelection _selection{};
    RadioIdentifier _localRadio{0U};

    bool SameRelationship(
        const ClockCoordinationSelection& selection,
        RadioIdentifier localRadio
    ) const noexcept {
        return _selection.Root == selection.Root && _selection.Parent == selection.Parent &&
               _selection.ParentIncarnation == selection.ParentIncarnation &&
               _selection.LocalStratum == selection.LocalStratum && _localRadio == localRadio;
    }

public:
    MeshSystemClockSynchronizationCoordinator(
        Timing::IClockSynchronizationTarget<Timing::ClockTick>& clock,
        IMeshSystemClockSynchronizationTransport& transport,
        const System::DeviceIdentifier& localDevice
    ) noexcept : _clock(clock), _transport(transport), _localDevice(localDevice) {}

    MeshSystemClockConvergenceDisposition Converge(
        const ClockCoordinationSelection& selection,
        const AuthenticatedDirectPeerBinding* parentBinding,
        RadioIdentifier localRadio
    ) {
        if (!_localDevice) return MeshSystemClockConvergenceDisposition::Invalid;
        if (!selection.HasRoot()) {
            if (_role == MeshSystemClockRole::Disabled) return MeshSystemClockConvergenceDisposition::Unchanged;
            _transport.Shutdown();
            _role = MeshSystemClockRole::Disabled;
            _selection = {};
            _localRadio = 0U;
            return MeshSystemClockConvergenceDisposition::Disabled;
        }

        const bool localReference = selection.Root == _localDevice;
        if (localReference) {
            if (selection.HasParent() || selection.LocalStratum != ClockRootStratum ||
                localRadio == 0U || localRadio == 0xFFU) {
                return MeshSystemClockConvergenceDisposition::Invalid;
            }
            if (_role == MeshSystemClockRole::Reference && SameRelationship(selection, localRadio)) {
                return MeshSystemClockConvergenceDisposition::Unchanged;
            }
            if (!_transport.ConfigureReference()) {
                _role = MeshSystemClockRole::Disabled;
                return MeshSystemClockConvergenceDisposition::TransportConfigurationFailed;
            }
            _role = MeshSystemClockRole::Reference;
            _selection = selection;
            _localRadio = localRadio;
            return MeshSystemClockConvergenceDisposition::ReferenceConfigured;
        }

        if (!selection.HasParent() || selection.LocalStratum == ClockRootStratum ||
            selection.LocalStratum == InvalidClockStratum || localRadio == 0U || localRadio == 0xFFU) {
            return MeshSystemClockConvergenceDisposition::Invalid;
        }
        if (parentBinding == nullptr || !parentBinding->IsValid() ||
            parentBinding->Neighbour != selection.Parent ||
            parentBinding->Incarnation != selection.ParentIncarnation ||
            parentBinding->LocalRadio != localRadio) {
            return MeshSystemClockConvergenceDisposition::ParentBindingUnavailable;
        }
        if (_role == MeshSystemClockRole::ClientAndReference && SameRelationship(selection, localRadio)) {
            return MeshSystemClockConvergenceDisposition::Unchanged;
        }

        const bool rootChanged = _selection.HasRoot() && _selection.Root != selection.Root;
        if (rootChanged) _clock.ResetSynchronization();
        if (!_transport.ConfigureClientAndReference(*parentBinding)) {
            _role = MeshSystemClockRole::Disabled;
            return MeshSystemClockConvergenceDisposition::TransportConfigurationFailed;
        }
        _role = MeshSystemClockRole::ClientAndReference;
        _selection = selection;
        _localRadio = localRadio;
        return MeshSystemClockConvergenceDisposition::ParentConfigured;
    }

    void Update() { if (_role != MeshSystemClockRole::Disabled) _transport.Update(); }

    bool IsDeadlineClockReady() const {
        if (_role == MeshSystemClockRole::Reference) return true;
        return _role == MeshSystemClockRole::ClientAndReference &&
               _clock.GetSynchronizationStatus().State == Timing::ClockSynchronizationState::Synchronized;
    }

    std::uint64_t NowMilliseconds() const noexcept {
        return _clock.GetSynchronizationTimestampNanoseconds() / Timing::NanosecondsPerMillisecond;
    }

    Timing::ClockSynchronizationStatus<Timing::ClockTick> SynchronizationStatus() const {
        return _clock.GetSynchronizationStatus();
    }

    MeshSystemClockRole Role() const noexcept { return _role; }
    const ClockCoordinationSelection& Selection() const noexcept { return _selection; }

    void Reset() {
        _transport.Shutdown();
        _clock.ResetSynchronization();
        _role = MeshSystemClockRole::Disabled;
        _selection = {};
        _localRadio = 0U;
    }
};

} // namespace ESPressio::Mesh
