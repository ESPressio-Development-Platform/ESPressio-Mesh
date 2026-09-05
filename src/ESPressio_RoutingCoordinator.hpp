#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_RouteCache.hpp"
#include "ESPressio_RoutingStrategy.hpp"

namespace ESPressio::Mesh {

/// <summary>Disposition returned while resolving one currently usable local route.</summary>
enum class RouteResolutionDisposition : std::uint8_t {
    Cached,
    Planned,
    LocalDestination,
    Unreachable,
    TemporarilyUnavailable,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Injected current-evidence gate for disposable cached and freshly planned routes.
/// </summary>
/// <remarks>
/// Implementations compose the independently owned evidence required by the application/technology build, including
/// authenticated membership, topology freshness, executable local-link bindings and any routing-policy constraints.
/// Returning false never mutates authoritative topology; it only declares this local route unusable now.
/// </remarks>
template<std::size_t HopCapacity = Limits::MaxRouteHops>
class IRouteUsabilityPolicy {
public:
    using Route = ResolvedRoute<HopCapacity>;
    virtual ~IRouteUsabilityPolicy() = default;
    virtual bool IsUsable(const Route& route) const noexcept = 0;
};

/// <summary>
/// Resolves a usable local route while preventing disposable cache hints from bypassing present evidence.
/// </summary>
/// <remarks>
/// Cache hits are always revalidated through IRouteUsabilityPolicy before use. A rejected hit is invalidated and the
/// injected IRoutingStrategy is consulted. Newly planned routes must satisfy both structural postconditions and the same
/// current-evidence usability policy. Cache insertion is opportunistic: a full cache never converts an otherwise usable
/// planned route into delivery failure.
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
    using Route = ResolvedRoute<HopCapacity>;
    using Cache = RouteCache<CacheCapacity, HopCapacity>;
    using UsabilityPolicy = IRouteUsabilityPolicy<HopCapacity>;

private:
    const Strategy& _strategy;
    const UsabilityPolicy& _usability;
    Cache& _cache;

public:
    RoutingCoordinator(const Strategy& strategy, const UsabilityPolicy& usability, Cache& cache) noexcept
        : _strategy(strategy), _usability(usability), _cache(cache) {}

    /// <summary>Resolves one currently usable route from cache or strategy.</summary>
    RouteResolutionDisposition Resolve(const Evidence& evidence, Route& route) noexcept {
        route.Clear();
        if (!evidence.Source || !evidence.Destination) return RouteResolutionDisposition::Invalid;

        if (evidence.Source == evidence.Destination) {
            if (!route.Assign(evidence.Source, evidence.Destination, nullptr, 0U)) {
                return RouteResolutionDisposition::Invalid;
            }
            return _usability.IsUsable(route)
                ? RouteResolutionDisposition::LocalDestination
                : RouteResolutionDisposition::TemporarilyUnavailable;
        }
        if (evidence.RemainingHops == 0U) return RouteResolutionDisposition::Unreachable;

        const Route* cached = _cache.Find(evidence.Source, evidence.Destination);
        if (cached != nullptr) {
            if (IsValidPlannedRoute(*cached, evidence.Source, evidence.Destination, evidence.RemainingHops) &&
                _usability.IsUsable(*cached)) {
                route = *cached;
                return RouteResolutionDisposition::Cached;
            }
            _cache.InvalidateEndpoint(evidence.Destination);
        }

        Route planned;
        const auto disposition = _strategy.Plan(evidence, planned);
        if (disposition == RoutePlanningDisposition::LocalDestination) {
            return RouteResolutionDisposition::Invalid;
        }
        if (disposition != RoutePlanningDisposition::Planned) {
            switch (disposition) {
                case RoutePlanningDisposition::Unreachable: return RouteResolutionDisposition::Unreachable;
                case RoutePlanningDisposition::TemporarilyUnavailable: return RouteResolutionDisposition::TemporarilyUnavailable;
                case RoutePlanningDisposition::ResourceUnavailable: return RouteResolutionDisposition::ResourceUnavailable;
                case RoutePlanningDisposition::Invalid: return RouteResolutionDisposition::Invalid;
                case RoutePlanningDisposition::LocalDestination:
                case RoutePlanningDisposition::Planned: break;
            }
            return RouteResolutionDisposition::Invalid;
        }

        if (!IsValidPlannedRoute(planned, evidence.Source, evidence.Destination, evidence.RemainingHops) ||
            !_usability.IsUsable(planned)) {
            return RouteResolutionDisposition::TemporarilyUnavailable;
        }

        route = planned;
        RouteCacheHandle ignored{};
        (void)_cache.Store(planned, ignored);
        return RouteResolutionDisposition::Planned;
    }
};

} // namespace ESPressio::Mesh
