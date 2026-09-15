#pragma once

#include <cstddef>

#include <ESPressio_IRadio.hpp>

#include "ESPressio_DirectPeerBindings.hpp"

namespace ESPressio::Mesh {

/// <summary>Converges local authenticated direct-peer execution bindings with final Radio peer lifecycle.</summary>
/// <remarks>
/// Final Radio deliberately exposes no semantic peer-observer graph. The composition root that owns peer creation/removal
/// calls this coordinator explicitly. Invalidating a RadioPeerHandle removes only the executable local binding; it does
/// not alter authenticated membership, liveness, deduplication or tombstones because direct-link loss is not authoritative
/// evidence that the Mesh member has left.
/// </remarks>
template<std::size_t BindingCapacity = Limits::MaxTopologyLinks>
class DirectPeerLifecycleCoordinator final {
    AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;

public:
    explicit DirectPeerLifecycleCoordinator(AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings) noexcept
        : _bindings(bindings) {}

    void RadioPeerObserved(Radio::RadioPeerHandle) noexcept {}

    void RadioPeerInvalidated(Radio::RadioPeerHandle peer) noexcept {
        if (peer) (void)_bindings.RemovePeer(peer);
    }
};

} // namespace ESPressio::Mesh
