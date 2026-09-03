#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_TopologySnapshot.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Bounded local route-planning result represented as a contiguous sequence of directed topology edge identities.
/// </summary>
/// <remarks>
/// This is a local routing artifact, never a mandatory source-route wire contract. Forwarding remains adaptive
/// hop-by-hop: a forwarding node may compute a different route/next hop from its current topology and policy.
/// RadioAddress and RadioPeerHandle never appear here because they are executable local-link bindings, not topology
/// identity. The route merely records which directed logical edges a strategy selected while planning.
/// </remarks>
template<std::size_t HopCapacity = Limits::MaxRouteHops>
class ResolvedRoute final {
    static_assert(HopCapacity > 0, "A route must permit at least one hop.");

    System::DeviceIdentifier _source{};
    System::DeviceIdentifier _destination{};
    std::array<TopologyLinkIdentity, HopCapacity> _hops{};
    std::size_t _hopCount{0};

public:
    static constexpr std::size_t MaximumHops() noexcept { return HopCapacity; }

    constexpr const System::DeviceIdentifier& Source() const noexcept { return _source; }
    constexpr const System::DeviceIdentifier& Destination() const noexcept { return _destination; }
    constexpr std::size_t HopCount() const noexcept { return _hopCount; }
    constexpr bool Empty() const noexcept { return _hopCount == 0U; }

    constexpr const TopologyLinkIdentity* begin() const noexcept { return _hops.data(); }
    constexpr const TopologyLinkIdentity* end() const noexcept { return _hops.data() + _hopCount; }

    constexpr const TopologyLinkIdentity* Hop(std::size_t index) const noexcept {
        return index < _hopCount ? &_hops[index] : nullptr;
    }

    /// <summary>Returns the first planned directed edge, which is the current node's planned next hop.</summary>
    constexpr const TopologyLinkIdentity* NextHop() const noexcept {
        return _hopCount == 0U ? nullptr : &_hops[0];
    }

    /// <summary>
    /// Replaces the route with one contiguous, loop-free sequence of directed link identities.
    /// </summary>
    /// <remarks>
    /// A zero-hop route is valid only when source equals destination. A nonzero route must begin at source, each edge
    /// must continue from the preceding neighbour, and the final neighbour must equal destination. Repeated devices are
    /// rejected so a cached/planned route cannot encode a forwarding loop.
    /// </remarks>
    bool Assign(
        const System::DeviceIdentifier& source,
        const System::DeviceIdentifier& destination,
        const TopologyLinkIdentity* hops,
        std::size_t hopCount
    ) noexcept {
        if (!source || !destination || (hops == nullptr && hopCount != 0U) || hopCount > HopCapacity) return false;
        if (hopCount == 0U) {
            if (source != destination) return false;
            Clear();
            _source = source;
            _destination = destination;
            return true;
        }

        System::DeviceIdentifier expectedAdvertiser = source;
        std::array<System::DeviceIdentifier, HopCapacity + 1U> visited{};
        std::size_t visitedCount = 1U;
        visited[0] = source;

        for (std::size_t i = 0; i < hopCount; ++i) {
            const auto& hop = hops[i];
            if (!hop || hop.Advertiser != expectedAdvertiser) return false;
            for (std::size_t j = 0; j < visitedCount; ++j) {
                if (visited[j] == hop.Neighbour) return false;
            }
            visited[visitedCount++] = hop.Neighbour;
            expectedAdvertiser = hop.Neighbour;
        }
        if (expectedAdvertiser != destination) return false;

        Clear();
        _source = source;
        _destination = destination;
        for (std::size_t i = 0; i < hopCount; ++i) _hops[i] = hops[i];
        _hopCount = hopCount;
        return true;
    }

    /// <summary>Clears the local planning artifact.</summary>
    void Clear() noexcept {
        _source = {};
        _destination = {};
        for (auto& hop : _hops) hop = {};
        _hopCount = 0U;
    }
};

} // namespace ESPressio::Mesh
