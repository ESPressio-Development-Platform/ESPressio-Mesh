#pragma once

#include <cstdint>

#include <ESPressio_IClockSynchronizationTarget.hpp>
#include <ESPressio_TimeReliability.hpp>
#include <ESPressio_ClockUncertainty.hpp>

#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_DirectPeerBindings.hpp"

namespace ESPressio::Mesh {

enum class MeshSystemClockRole : std::uint8_t {
    Disabled=0,
    Reference,
    ClientAndReference
};

enum class MeshSystemClockConvergenceDisposition : std::uint8_t {
    Unchanged=0,
    Disabled,
    ReferenceConfigured,
    ParentConfigured,
    ParentBindingUnavailable,
    ReferenceLineageInvalid,
    TimingConfigurationFailed,
    TransportConfigurationFailed,
    Invalid
};

/// <summary>Authenticated upstream timing quality propagated by Mesh without reclassifying Timing-owned reliability.</summary>
struct MeshClockReferenceLineage final {
    std::uint64_t ReferenceIdentityToken{0U};
    Timing::TimeReliability Reliability{Timing::TimeReliability::Unqualified};
    Timing::ClockUncertainty UpstreamUncertainty{};
    Timing::ClockUncertainty LocalTransportUncertainty{};

    constexpr bool IsValid() const noexcept {
        if(ReferenceIdentityToken==0U||!Timing::IsValidTimeReliability(Reliability)) return false;
        if(Timing::IsQualifiedTimeReliability(Reliability)) {
            return UpstreamUncertainty.IsKnown&&LocalTransportUncertainty.IsKnown&&
                   Timing::ClockMath::Add(UpstreamUncertainty.Nanoseconds,LocalTransportUncertainty.Nanoseconds)<
                       Timing::ClockSynchronizationProfile::QualifiedCeilingNanoseconds;
        }
        return true;
    }

    constexpr Timing::ClockUncertainty EffectiveUncertainty() const noexcept {
        if(!UpstreamUncertainty.IsKnown||!LocalTransportUncertainty.IsKnown) return {};
        return Timing::ClockUncertainty::Known(
            Timing::ClockMath::Add(UpstreamUncertainty.Nanoseconds,LocalTransportUncertainty.Nanoseconds));
    }
};

/// <summary>Reference relationship passed to the direct-link timestamp transport; no estimator state is owned here.</summary>
struct MeshClockReferenceRelationship final {
    AuthenticatedDirectPeerBinding Parent{};
    System::DeviceIdentifier Root{};
    ClockStratum LocalStratum{InvalidClockStratum};
    MeshClockReferenceLineage Lineage{};

    constexpr bool IsValid() const noexcept {
        return Parent.IsValid()&&static_cast<bool>(Root)&&LocalStratum!=ClockRootStratum&&
               LocalStratum!=InvalidClockStratum&&Lineage.IsValid();
    }
};

/// <summary>
/// Fixed infrastructure control sink implemented by the selected precision synchronization transport integration.
/// </summary>
/// <remarks>
/// Radio-backed implementations configure the direct peer and feed four-timestamp observations to Timing. Mesh never
/// invokes timestamp exchange, chooses evidence cadence, computes offsets, or decides TimeReliability.
/// </remarks>
class IMeshClockReferenceTransportControl {
public:
    virtual ~IMeshClockReferenceTransportControl()=default;
    virtual bool SelectDirectReference(const MeshClockReferenceRelationship& relationship) noexcept=0;
    virtual void ClearDirectReference() noexcept=0;
};

/// <summary>
/// Mesh-only root/parent/reference orchestration. Timing owns synchronization semantics; the transport owns exchange.
/// </summary>
class MeshSystemClockSynchronizationCoordinator final {
    Timing::IClockSynchronizationTarget& _timing;
    IMeshClockReferenceTransportControl& _transport;
    System::DeviceIdentifier _localDevice{};
    MeshSystemClockRole _role{MeshSystemClockRole::Disabled};
    ClockCoordinationSelection _selection{};
    RadioIdentifier _localRadio{0U};
    MeshClockReferenceLineage _lineage{};

    bool SameRelationship(const ClockCoordinationSelection& selection,RadioIdentifier localRadio,
                          const MeshClockReferenceLineage& lineage) const noexcept {
        return _selection.Root==selection.Root&&_selection.Parent==selection.Parent&&
               _selection.ParentIncarnation==selection.ParentIncarnation&&
               _selection.LocalStratum==selection.LocalStratum&&_localRadio==localRadio&&
               _lineage.ReferenceIdentityToken==lineage.ReferenceIdentityToken&&
               _lineage.Reliability==lineage.Reliability&&
               _lineage.UpstreamUncertainty.IsKnown==lineage.UpstreamUncertainty.IsKnown&&
               _lineage.UpstreamUncertainty.Nanoseconds==lineage.UpstreamUncertainty.Nanoseconds&&
               _lineage.LocalTransportUncertainty.IsKnown==lineage.LocalTransportUncertainty.IsKnown&&
               _lineage.LocalTransportUncertainty.Nanoseconds==lineage.LocalTransportUncertainty.Nanoseconds;
    }

public:
    MeshSystemClockSynchronizationCoordinator(Timing::IClockSynchronizationTarget& timing,
        IMeshClockReferenceTransportControl& transport,const System::DeviceIdentifier& localDevice) noexcept
        :_timing(timing),_transport(transport),_localDevice(localDevice) {}

    MeshSystemClockConvergenceDisposition Converge(
        const ClockCoordinationSelection& selection,
        const AuthenticatedDirectPeerBinding* parentBinding,
        RadioIdentifier localRadio,
        const MeshClockReferenceLineage& lineage={}) noexcept {
        if(!_localDevice) return MeshSystemClockConvergenceDisposition::Invalid;

        if(!selection.HasRoot()) {
            if(_role==MeshSystemClockRole::Disabled) return MeshSystemClockConvergenceDisposition::Unchanged;
            _transport.ClearDirectReference();
            _timing.SetSynchronizationActivity(false,false);
            _timing.ResetSynchronization();
            _role=MeshSystemClockRole::Disabled;_selection={};_localRadio=0;_lineage={};
            return MeshSystemClockConvergenceDisposition::Disabled;
        }

        if(selection.Root==_localDevice) {
            if(selection.HasParent()||selection.LocalStratum!=ClockRootStratum||localRadio==0U||localRadio==0xFFU)
                return MeshSystemClockConvergenceDisposition::Invalid;
            if(_role==MeshSystemClockRole::Reference&&_selection.Root==selection.Root&&_localRadio==localRadio)
                return MeshSystemClockConvergenceDisposition::Unchanged;
            _transport.ClearDirectReference();
            _timing.SetSynchronizationActivity(false,true);
            if(_role==MeshSystemClockRole::ClientAndReference) _timing.ResetSynchronization();
            _role=MeshSystemClockRole::Reference;_selection=selection;_localRadio=localRadio;_lineage={};
            return MeshSystemClockConvergenceDisposition::ReferenceConfigured;
        }

        if(!selection.HasParent()||selection.LocalStratum==ClockRootStratum||selection.LocalStratum==InvalidClockStratum||
           localRadio==0U||localRadio==0xFFU) return MeshSystemClockConvergenceDisposition::Invalid;
        if(parentBinding==nullptr||!parentBinding->IsValid()||parentBinding->Neighbour!=selection.Parent||
           parentBinding->Incarnation!=selection.ParentIncarnation||parentBinding->LocalRadio!=localRadio)
            return MeshSystemClockConvergenceDisposition::ParentBindingUnavailable;
        if(!lineage.IsValid()) return MeshSystemClockConvergenceDisposition::ReferenceLineageInvalid;
        if(_role==MeshSystemClockRole::ClientAndReference&&SameRelationship(selection,localRadio,lineage))
            return MeshSystemClockConvergenceDisposition::Unchanged;

        const MeshClockReferenceRelationship relationship{*parentBinding,selection.Root,selection.LocalStratum,lineage};
        if(!relationship.IsValid()) return MeshSystemClockConvergenceDisposition::ReferenceLineageInvalid;
        if(!_transport.SelectDirectReference(relationship))
            return MeshSystemClockConvergenceDisposition::TransportConfigurationFailed;

        const bool sourceChanged=_role!=MeshSystemClockRole::ClientAndReference||
            _lineage.ReferenceIdentityToken!=lineage.ReferenceIdentityToken||
            _selection.Parent!=selection.Parent||_selection.ParentIncarnation!=selection.ParentIncarnation;
        if(sourceChanged) {
            const auto timing=_timing.SelectSynchronizationReference(lineage.ReferenceIdentityToken);
            if(timing!=Timing::ClockConfigurationStatus::Success) {
                _transport.ClearDirectReference();
                _timing.SetSynchronizationActivity(false,false);
                return MeshSystemClockConvergenceDisposition::TimingConfigurationFailed;
            }
        }
        _timing.SetSynchronizationActivity(true,true);
        _role=MeshSystemClockRole::ClientAndReference;_selection=selection;_localRadio=localRadio;_lineage=lineage;
        return MeshSystemClockConvergenceDisposition::ParentConfigured;
    }

    MeshSystemClockRole Role() const noexcept { return _role; }
    const ClockCoordinationSelection& Selection() const noexcept { return _selection; }
    const MeshClockReferenceLineage& Lineage() const noexcept { return _lineage; }

    void Shutdown() noexcept {
        _transport.ClearDirectReference();
        _timing.SetSynchronizationActivity(false,false);
        _timing.ResetSynchronization();
        _role=MeshSystemClockRole::Disabled;_selection={};_localRadio=0;_lineage={};
    }
};

} // namespace ESPressio::Mesh
