#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_RouteCache.hpp"
#include "ESPressio_RoutingStrategy.hpp"

namespace ESPressio::Mesh {

/// <summary>Disposition returned when selecting a locally usable route.</summary>
enum class RouteSelectionDisposition : std::uint8_t {
    Cached,
    Planned,
    LocalDestination,
    Unreachable,
    TemporarilyUnavailable,
    ResourceUnavailable,
    Invalid,
    InvalidStrategyResult
};

/// <summary>
/// Injected current-evidence gate applied to every cached or freshly planned route before forwarding.
/// </summary>
/// <remarks>
/// Implementations compose current authenticated membership, topology freshness, link usability, local Radio binding
/// availability and any routing-policy constraints relevant to the concrete application. Keeping this gate separate
/// prevents the disposable RouteCache from becoming an authority or from embedding one universal freshness/cost model.
/// </remarks>
template<std::size_t HopCapacity = Limits::MaxRouteHops>
class IRouteUsabilityPolicy {
public:
    using Route = ResolvedRoute<HopCapacity>;
    virtual ~IRouteUsabilityPolicy() = default;

    /// <summary>Returns true only when the complete route is usable against current local evidence.</summary>
    virtual bool IsUsable(const Route& route) const noexcept = 0;
};

/// <summary>
/// Composes disposable route-cache hints with injected current-evidence revalidation and route planning.
/// </summary>
/// <remarks>
/// A cached route is never returned merely because it exists. It must first satisfy structural hop-limit validation and
/// IRouteUsabilityPolicy. A rejected cache hint is ignored and the strategy is asked to plan from current evidence.
/// Freshly planned routes are subjected to the same usability gate before use. Successful plans are cached best-effort;
/// cache saturation cannot make an otherwise valid route unusable.
/// </remarks>
template<
    typename TCharacteristics,
    std::size_t LinkCapacity = Limits::MaxTopologyLinks,
    std::size_t AuthorityCapacity = Limits::MaxMeshNodes,
    std::size_t CacheCapacity = Limits::MaxRouteCacheEntries,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class RoutingCoordinator final {
public:
    using Strategy = IRoutingStrategy<TCharacteristics, LinkCapacity, AuthorityCapacity, HopCapacity>;
    using Evidence = typename Strategy::Evidence;
    using Route = typename Strategy::Route;
    using Cache = RouteCache<CacheCapacity, HopCapacity>;
    using Usability = IRouteUsabilityPolicy<HopCapacity>;

private:
    Cache& _cache;
    const Strategy& _strategy;
    const Usability& _usability;

    static RouteSelectionDisposition Map(RoutePlanningDisposition disposition) noexcept {
        switch (disposition) {
            case RoutePlanningDisposition::Planned: return RouteSelectionDisposition::Planned;
            case RoutePlanningDisposition::LocalDestination: return RouteSelectionDisposition::LocalDestination;
            case RoutePlanningDisposition::Unreachable: return RouteSelectionDisposition::Unreachable;
            case RoutePlanningDisposition::TemporarilyUnavailable: return RouteSelectionDisposition::TemporarilyUnavailable;
            case RoutePlanningDisposition::ResourceUnavailable: return RouteSelectionDisposition::ResourceUnavailable;
            case RoutePlanningDisposition::Invalid: return RouteSelectionDisposition::Invalid;
        }
        return RouteSelectionDisposition::Invalid;
    }

public:
    RoutingCoordinator(Cache& cache, const Strategy& strategy, const Usability& usability) noexcept
        : _cache(cache), _strategy(strategy), _usability(usability) {}

    /// <summary>Selects one currently usable local route, consulting the cache only as a revalidated hint.</summary>
    RouteSelectionDisposition Select(const Evidence& evidence, Route& route) noexcept {
        route.Clear();
        if (!evidence.Source || !evidence.Destination) return RouteSelectionDisposition::Invalid;

        if (const auto* cached = _cache.Find(evidence.Source, evidence.Destination); cached != nullptr) {
            if (IsValidPlannedRoute(*cached, evidence.Source, evidence.Destination, evidence.RemainingHops) &&
                _usability.IsUsable(*cached)) {
                route = *cached;
                return evidence.Source == evidence.Destination
                    ? RouteSelectionDisposition::LocalDestination
                    : RouteSelectionDisposition::Cached;
            }
        }

        const auto planning = _strategy.Plan(evidence, route);
        if (planning == RoutePlanningDisposition::LocalDestination) {
            return IsValidPlannedRoute(route, evidence.Source, evidence.Destination, evidence.RemainingHops)
                ? RouteSelectionDisposition::LocalDestination
                : RouteSelectionDisposition::InvalidStrategyResult;
        }
        if (planning != RoutePlanningDisposition::Planned) {
            route.Clear();
            return Map(planning);
        }
        if (!IsValidPlannedRoute(route, evidence.Source, evidence.Destination, evidence.RemainingHops) ||
            !_usability.IsUsable(route)) {
            route.Clear();
            return RouteSelectionDisposition::InvalidStrategyResult;
        }

        RouteCacheHandle ignored{};
        (void)_cache.Store(route, ignored); // Disposable optimization only; saturation does not invalidate the route.
        return RouteSelectionDisposition::Planned;
    }
};

} // namespace ESPressio::Mesh
