#include <cassert>
#include <cstdint>

#include <ESPressio_RouteAttemptCoordinator.hpp>
#include <ESPressio_RoutePlanner.hpp>

using namespace ESPressio::Mesh;

namespace {
struct Characteristics final {
    std::uint16_t Value{0};
    constexpr bool operator==(const Characteristics& other) const noexcept { return Value == other.Value; }
};

ESPressio::System::DeviceIdentifier Device(std::uint8_t value) {
    ESPressio::System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return ESPressio::System::DeviceIdentifier(bytes);
}

MembershipIncarnation Incarnation(std::uint8_t value) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return MembershipIncarnation(bytes);
}

class CountingStrategy final : public IRoutingStrategy<Characteristics> {
    mutable std::uint32_t _calls{0};
public:
    std::uint32_t Calls() const noexcept { return _calls; }

    RoutePlanningDisposition Plan(const Evidence& evidence, Route& route) const noexcept override {
        ++_calls;
        route.Clear();
        for (const auto& first : evidence.Topology) {
            if (first.Identity.Advertiser != evidence.Source) continue;
            if (first.Identity.Neighbour == evidence.Destination) {
                TopologyLinkIdentity hops[]{first.Identity};
                return route.Assign(evidence.Source, evidence.Destination, hops, 1U)
                    ? RoutePlanningDisposition::Planned : RoutePlanningDisposition::Invalid;
            }
            for (const auto& second : evidence.Topology) {
                if (second.Identity.Advertiser != first.Identity.Neighbour ||
                    second.Identity.Neighbour != evidence.Destination) continue;
                TopologyLinkIdentity hops[]{first.Identity, second.Identity};
                return route.Assign(evidence.Source, evidence.Destination, hops, 2U)
                    ? RoutePlanningDisposition::Planned : RoutePlanningDisposition::Invalid;
            }
        }
        return RoutePlanningDisposition::Unreachable;
    }
};

class SwitchableRevalidation final : public IRouteRevalidationPolicy<Characteristics> {
public:
    bool Usable{true};
    mutable std::uint32_t Calls{0};

    bool IsUsable(const Evidence&, const Route&) const noexcept override {
        ++Calls;
        return Usable;
    }
};
}

int main() {
    TopologyGraphStore<Characteristics> graph;
    const auto a = Device(1);
    const auto b = Device(2);
    const auto c = Device(3);
    const auto incarnation = Incarnation(9);
    DirectedTopologyLink<Characteristics> aLinks[]{{{a, 1, b, 1}, {10}}};
    DirectedTopologyLink<Characteristics> bLinks[]{{{b, 1, c, 1}, {20}}};
    assert(graph.ApplyComplete(a, incarnation, 1, aLinks, 1) == TopologySnapshotApplyResult::Applied);
    assert(graph.ApplyComplete(b, incarnation, 1, bLinks, 1) == TopologySnapshotApplyResult::Applied);

    RouteCache<> cache;
    CountingStrategy strategy;
    SwitchableRevalidation validation;
    RoutePlanner<Characteristics> planner{cache, strategy, validation};
    RoutingEvidence<Characteristics> evidence{graph, a, c, 2};
    RoutePlan<> plan;

    // First plan comes from strategy and is cached only after current-evidence validation.
    assert(planner.PlanRoute(evidence, plan) == RoutePlanningDisposition::Planned);
    assert(plan.Origin == RoutePlanOrigin::Strategy);
    assert(strategy.Calls() == 1U);
    assert(cache.Size() == 1U);

    // A later cache hit still invokes current-evidence validation before use.
    assert(planner.PlanRoute(evidence, plan) == RoutePlanningDisposition::Planned);
    assert(plan.Origin == RoutePlanOrigin::Cache);
    assert(strategy.Calls() == 1U);

    // Rejected cached evidence is invalidated and cannot bypass fresh planning.
    validation.Usable = false;
    assert(planner.PlanRoute(evidence, plan) == RoutePlanningDisposition::Unreachable);
    assert(strategy.Calls() == 2U);
    assert(cache.Size() == 0U);

    validation.Usable = true;
    assert(planner.PlanRoute(evidence, plan) == RoutePlanningDisposition::Planned);
    assert(plan.Origin == RoutePlanOrigin::Strategy);
    assert(strategy.Calls() == 3U);

    DefaultRouteAttemptPolicy sameRoutePolicy;
    DefaultRetryPolicy retryPolicy;
    RouteAttemptCoordinator attempts{sameRoutePolicy, retryPolicy};
    constexpr std::uint64_t deadline = 10'000U;

    assert(attempts.BeginDistinctRouteAttempt(1'000U, deadline));
    assert(attempts.DistinctRoutesAttempted() == 1U);
    assert(attempts.AttemptsOnCurrentRoute() == 1U);
    assert(attempts.Decide(RouteAttemptOutcome::RetryableFailure, 2'000U, deadline) == RouteAttemptAction::RetryCurrentRoute);
    assert(attempts.BeginCurrentRouteRetry(2'000U, deadline));
    assert(attempts.BeginCurrentRouteRetry(3'000U, deadline));
    assert(attempts.AttemptsOnCurrentRoute() == Limits::MaxSameRouteAttempts);
    assert(attempts.Decide(RouteAttemptOutcome::RetryableFailure, 4'000U, deadline) == RouteAttemptAction::ReplanDistinctRoute);

    assert(attempts.BeginDistinctRouteAttempt(4'000U, deadline));
    assert(attempts.BeginDistinctRouteAttempt(5'000U, deadline));
    assert(attempts.BeginDistinctRouteAttempt(6'000U, deadline));
    assert(attempts.DistinctRoutesAttempted() == Limits::MaxRoutesAttempted);
    assert(attempts.Decide(RouteAttemptOutcome::RouteUnavailable, 7'000U, deadline) == RouteAttemptAction::StopAttemptLimit);
    assert(!attempts.BeginDistinctRouteAttempt(7'000U, deadline));

    attempts.Reset();
    assert(attempts.BeginDistinctRouteAttempt(9'000U, deadline));
    assert(attempts.Decide(RouteAttemptOutcome::RetryableFailure, deadline, deadline) == RouteAttemptAction::StopDeadlineExpired);
    assert(!attempts.BeginCurrentRouteRetry(deadline, deadline));
    assert(attempts.Decide(RouteAttemptOutcome::PermanentFailure, 9'100U, deadline) == RouteAttemptAction::StopPermanentFailure);
    assert(attempts.Decide(RouteAttemptOutcome::Delivered, 9'100U, deadline) == RouteAttemptAction::Complete);

    return 0;
}
