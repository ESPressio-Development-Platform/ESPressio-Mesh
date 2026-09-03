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

/// <summary>Injected current-evidence validator for disposable cached route hints and new strategy plans.</summary>
/// <remarks>
/// Implementations re-check every condition that can invalidate a route, including current authenticated membership,
/// topology freshness, link usability and composition-specific routing policy. The cache is never authoritative.
/// </remarks>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks,
         std::size_t AuthorityCapacity = Limits::MaxMeshNodes, std::size_t HopCapacity = Limits::MaxRouteHops>
class IRouteRevalidationPolicy {
public:
    using Evidence = RoutingEvidence<TCharacteristics, LinkCapacity, AuthorityCapacity>;
    using Route = ResolvedRoute<HopCapacity>;

    virtual ~IRouteRevalidationPolicy() = default;
    virtual bool IsUsable(const Evidence& evidence, const Route& route) const noexcept = 0;
};

/// <summary>Composes disposable cache lookup, current-evidence revalidation and injected fresh route planning.</summary>
/// <remarks>
/// Cached hints are copied and revalidated before use. Rejected hints are removed before fresh planning. Newly planned
/// routes must pass the same current-evidence policy, preventing strategy output from bypassing present membership/link/
/// freshness requirements. Cache saturation cannot prevent successful forwarding because storage remains optional.
/// </remarks>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks,
         std::size_t AuthorityCapacity = Limits::MaxMeshNodes, std::size_t CacheCapacity = Limits::MaxRouteCacheEntries,
         std::size_t HopCapacity = Limits::MaxRouteHops>
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

    RoutePlanningDisposition PlanRoute(const Evidence& evidence, Plan& plan) noexcept {
        plan = {};
        if (!evidence.Source || !evidence.Destination) return RoutePlanningDisposition::Invalid;

        if (evidence.Source == evidence.Destination) {
            if (!plan.Route.Assign(evidence.Source, evidence.Destination, nullptr, 0U)) return RoutePlanningDisposition::Invalid;
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
            (void)_cache.InvalidatePair(evidence.Source, evidence.Destination);
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
        (void)_cache.Store(candidate, ignored);
        return RoutePlanningDisposition::Planned;
    }
};

} // namespace ESPressio::Mesh
