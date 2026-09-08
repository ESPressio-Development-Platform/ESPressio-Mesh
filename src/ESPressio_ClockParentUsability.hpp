#pragma once

#include <cstddef>

#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_DirectPeerBindings.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Clock-parent usability policy requiring an executable direct Radio binding for the exact authenticated sender
/// incarnation.
/// </summary>
/// <remarks>
/// This policy is intentionally applied only to immediate parent selection. It does not participate in global root
/// election, so an elected root may remain several Mesh hops away while synchronization proceeds through a direct
/// neighbour advertising that root. The binding table already represents post-authentication local executable peers;
/// this policy performs no authentication and does not inspect Radio addresses.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _peers (AuthenticatedDirectPeerBindingTable<DirectPeerCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<
    typename TQuality,
    std::size_t DirectPeerCapacity = Limits::MaxTopologyLinks
>
class DirectClockParentUsabilityPolicy final : public IClockParentUsabilityPolicy<TQuality> {
    const AuthenticatedDirectPeerBindingTable<DirectPeerCapacity>& _peers;

public:
    explicit DirectClockParentUsabilityPolicy(
        const AuthenticatedDirectPeerBindingTable<DirectPeerCapacity>& peers
    ) noexcept : _peers(peers) {}

    bool IsUsableParent(const ClockCoordinationAdvertisement<TQuality>& advertisement) const noexcept override {
        return advertisement.IsStructurallyValid() &&
               _peers.HasNeighbour(advertisement.Sender, advertisement.SenderIncarnation);
    }
};

} // namespace ESPressio::Mesh
