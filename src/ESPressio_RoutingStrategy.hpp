#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_Route.hpp"
#include "ESPressio_TopologyGraphStore.hpp"

namespace ESPressio::Mesh {

/// <summary>Outcome of one local route-planning request.</summary>
enum class RoutePlanningDisposition : std::uint8_t {
    Planned,
    LocalDestination,
    Unreachable,
    TemporarilyUnavailable,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Read-only bounded routing input supplied to an injected routing strategy.
/// </summary>
/// <remarks>
/// The graph contains authoritative directed topology only. Freshness, membership, executable Radio bindings and
/// technology-specific observations remain independent concerns and are deliberately exposed through strategy-owned
/// policy/context rather than collapsed here into a universal scalar route cost. The strategy must treat Expired or
/// otherwise unusable topology as unavailable according to the composition that supplies it.
/// </remarks>
template<
    typename TCharacteristics,
    std::size_t LinkCapacity = Limits::MaxTopologyLinks,
    std::size_t AuthorityCapacity = Limits::MaxMeshNodes
>
struct RoutingEvidence final {
    const TopologyGraphStore<TCharacteristics, LinkCapacity, AuthorityCapacity>& Topology;
    System::DeviceIdentifier Source{};
    System::DeviceIdentifier Destination{};
    RemainingHopLimit RemainingHops{Limits::DefaultHopLimit};
};

/// <summary>
/// Injectable strategy that interprets the bounded topology and chooses a local route without imposing a universal
/// RouteCost representation on Mesh.
/// </summary>
/// <remarks>
/// A strategy may use arbitrary bounded, technology-independent link characteristics and independently supplied local
/// policy/state. The returned route is a disposable local planning artifact, not a source-routing wire contract. Mesh
/// forwarding remains adaptive hop-by-hop and every forwarding node may plan a different next hop.
/// </remarks>
template<
    typename TCharacteristics,
    std::size_t LinkCapacity = Limits::MaxTopologyLinks,
    std::size_t AuthorityCapacity = Limits::MaxMeshNodes,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class IRoutingStrategy {
public:
    using Evidence = RoutingEvidence<TCharacteristics, LinkCapacity, AuthorityCapacity>;
    using Route = ResolvedRoute<HopCapacity>;

    virtual ~IRoutingStrategy() = default;

    /// <summary>
    /// Plans one bounded route from the supplied current local evidence.
    /// </summary>
    /// <remarks>
    /// On Planned, route must be a valid contiguous Source→Destination path whose hop count does not exceed both the
    /// compile-time route bound and RemainingHops. Other dispositions must not be interpreted as delivery completion.
    /// </remarks>
    virtual RoutePlanningDisposition Plan(const Evidence& evidence, Route& route) const noexcept = 0;
};

/// <summary>
/// Validates the structural postconditions of a route returned by an injected strategy.
/// </summary>
template<std::size_t HopCapacity = Limits::MaxRouteHops>
bool IsValidPlannedRoute(
    const ResolvedRoute<HopCapacity>& route,
    const System::DeviceIdentifier& source,
    const System::DeviceIdentifier& destination,
    RemainingHopLimit remainingHops
) noexcept {
    if (!source || !destination || route.Source() != source || route.Destination() != destination) return false;
    if (source == destination) return route.HopCount() == 0U;
    return route.HopCount() != 0U && route.HopCount() <= static_cast<std::size_t>(remainingHops);
}

} // namespace ESPressio::Mesh
