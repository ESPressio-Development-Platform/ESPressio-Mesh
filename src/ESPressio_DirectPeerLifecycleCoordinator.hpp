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
