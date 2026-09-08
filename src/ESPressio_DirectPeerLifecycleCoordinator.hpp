#pragma once

#include <cstddef>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_DirectPeerBindings.hpp"

namespace ESPressio::Mesh {

/// <summary>Converges local authenticated direct-peer execution bindings with Radio peer lifecycle.</summary>
/// <remarks>
/// Invalidating a RadioPeerHandle removes only the executable local binding. It deliberately does not remove or alter
/// authenticated membership, deduplication, liveness or tombstones because loss of one direct link is not authoritative
/// evidence that the Mesh member has left. Topology/liveness convergence remains owned by their respective services.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: sizeof(Radio::IRadioTransportPeerObserver) [0 bytes dynamic allocation]
 * Members:
 * - _bindings (AuthenticatedDirectPeerBindingTable<BindingCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: sizeof(Radio::IRadioTransportPeerObserver) + 4 bytes known members [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<std::size_t BindingCapacity = Limits::MaxTopologyLinks>
class DirectPeerLifecycleCoordinator final : public Radio::IRadioTransportPeerObserver {
    AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;

public:
    explicit DirectPeerLifecycleCoordinator(AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings) noexcept
        : _bindings(bindings) {}

    void OnRadioPeerObserved(Radio::RadioTransport&, Radio::IRadio&, Radio::RadioPeerHandle,
                             const Radio::RadioAddress&) override {}

    void OnRadioPeerInvalidated(Radio::RadioTransport&, Radio::IRadio&, Radio::RadioPeerHandle peer,
                                const Radio::RadioAddress&, Radio::RadioPeerInvalidationReason) override {
        (void)_bindings.RemovePeer(peer);
    }
};

} // namespace ESPressio::Mesh
