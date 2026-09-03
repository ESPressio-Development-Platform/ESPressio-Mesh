#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_RouteCache.hpp"
#include "ESPressio_RoutingStrategy.hpp"

namespace ESPressio::Mesh {

/// <summary>Origin of one currently usable local route plan.</summary>
enum class RoutePlanOrigin : std::uint8_t {
    Cache,
    Strategy,
    LocalDestination
};

/// <summary>Complete local route-planning result after cache revalidation or strategy execution.</summary>
template<std::size_t HopCapacity = Limits::MaxRouteHops>
struct RoutePlan final {
    ResolvedRoute<HopCapacity> Route{};
    RoutePlanOrigin Origin{RoutePlanOrigin::Strategy};
};

/// <summary>
/// Injected current-evidence validator for disposable cached route hints.
/// </summary>
/// <remarks>
/// Implementations must re-check every condition that can invalidate a previously planned route, including current
/// authenticated membership, topology freshness, link usability and composition-specific routing policy. The cache is
/// never authoritative and this interface is deliberately separate from cache storage so no stale hint can bypass
/// present evidence. Returning false causes the hint to be ignored and fresh strategy planning to occur.
/// </remarks>
template<
    typename TCharacteristics,
    std::size_t LinkCapacity = Limits::MaxTopologyLinks,
    std::size_t AuthorityCapacity = Limits::MaxMeshNodes,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class IRouteRevalidationPolicy {
public:
    using Evidence = RoutingEvidence<TCharacteristics, LinkCapacity, AuthorityCapacity>;
    using Route = ResolvedRoute<HopCapacity>;

    virtual ~IRouteRevalidationPolicy() = default;
    virtual bool IsUsable(const Evidence& evidence, const Route& route) const noexcept = 0;
};

/// <summary>
/// Composes a disposable bounded route cache with injected current-evidence revalidation and fresh strategy planning.
/// </summary>
/// <remarks>
/// A cache hit is never returned directly. It must first pass structural checks and IRouteRevalidationPolicy against
/// current evidence. Rejected hints are explicitly invalidated before strategy planning. Newly planned routes are also
/// structurally validated before optional cache insertion. Cache saturation cannot prevent forwarding because caching is
/// an optimization only.
/// </remarks>
template<
    typename TCharacteristics,
    std::size_t LinkCapacity = Limits::MaxTopologyLinks,
    std::size_t AuthorityCapacity = Limits::MaxMeshNodes,
    std::size_t CacheCapacity = Limits::MaxRouteCacheEntries,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class RoutePlanner final {
public:
    using Strategy = IRoutingStrategy<TCharacteristics, LinkCapacity, AuthorityCapacity, HopCapacity>;
    using Revalidation = IRouteRevalidationPolicy<TCharacteristics, LinkCapacity, AuthorityCapacity, HopCapacity>;
    using Cache = RouteCache<CacheCapacity, HopCapacity>;
    using Evidence = RoutingEvidence<TCharacteristics, LinkCapacity, AuthorityCapacity>;
    using Route = ResolvedRoute<HopCapacity>;
    using Plan = RoutePlan<HopCapacity>;

private:
    Cache& _cache;
    const Strategy& _strategy;
    const Revalidation& _revalidation;

public:
    RoutePlanner(Cache& cache, const Strategy& strategy, const Revalidation& revalidation) noexcept
        : _cache(cache), _strategy(strategy), _revalidation(revalidation) {}

    /// <summary>Returns a currently usable route or a non-success planning disposition.</summary>
    RoutePlanningDisposition PlanRoute(const Evidence& evidence, Plan& plan) noexcept {
        plan = {};
        if (!evidence.Source || !evidence.Destination) return RoutePlanningDisposition::Invalid;

        if (evidence.Source == evidence.Destination) {
            if (!plan.Route.Assign(evidence.Source, evidence.Destination, nullptr, 0U)) {
                return RoutePlanningDisposition::Invalid;
            }
            plan.Origin = RoutePlanOrigin::LocalDestination;
            return RoutePlanningDisposition::LocalDestination;
        }

        if (const auto* cached = _cache.Find(evidence.Source, evidence.Destination)) {
            const Route cachedCopy = *cached;
            if (IsValidPlannedRoute(cachedCopy, evidence.Source, evidence.Destination, evidence.RemainingHops) &&
                _revalidation.IsUsable(evidence, cachedCopy)) {
                plan.Route = cachedCopy;
                plan.Origin = RoutePlanOrigin::Cache;
                return RoutePlanningDisposition::Planned;
            }
            // The cache exposes endpoint lookup rather than a handle here; invalidate all hints for this endpoint pair
            // indirectly by replacing/clearing only when a fresh route succeeds. A rejected hint is never used below.
        }

        Route candidate;
        const auto disposition = _strategy.Plan(evidence, candidate);
        if (disposition == RoutePlanningDisposition::LocalDestination) {
            if (!IsValidPlannedRoute(candidate, evidence.Source, evidence.Destination, evidence.RemainingHops)) {
                return RoutePlanningDisposition::Invalid;
            }
            plan.Route = candidate;
            plan.Origin = RoutePlanOrigin::LocalDestination;
            return disposition;
        }
        if (disposition != RoutePlanningDisposition::Planned) return disposition;
        if (!IsValidPlannedRoute(candidate, evidence.Source, evidence.Destination, evidence.RemainingHops) ||
            !_revalidation.IsUsable(evidence, candidate)) {
            return RoutePlanningDisposition::Unreachable;
        }

        plan.Route = candidate;
        plan.Origin = RoutePlanOrigin::Strategy;
        RouteCacheHandle ignored{};
        (void)_cache.Store(candidate, ignored); // Cache pressure cannot change successful planning semantics.
        return RoutePlanningDisposition::Planned;
    }
};

} // namespace ESPressio::Mesh
