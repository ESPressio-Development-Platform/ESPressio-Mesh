#include <cassert>
#include <cstdint>

#include <ESPressio_RouteAttemptPolicy.hpp>
#include <ESPressio_RoutingStrategy.hpp>

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

class FirstContiguousStrategy final : public IRoutingStrategy<Characteristics> {
public:
    RoutePlanningDisposition Plan(const Evidence& evidence, Route& route) const noexcept override {
        route.Clear();
        if (!evidence.Source || !evidence.Destination) return RoutePlanningDisposition::Invalid;
        if (evidence.Source == evidence.Destination) {
            return route.Assign(evidence.Source, evidence.Destination, nullptr, 0)
                ? RoutePlanningDisposition::LocalDestination
                : RoutePlanningDisposition::Invalid;
        }
        if (evidence.RemainingHops == 0U) return RoutePlanningDisposition::Unreachable;

        TopologyLinkIdentity hops[Limits::MaxRouteHops]{};
        std::size_t count = 0U;
        auto current = evidence.Source;
        while (current != evidence.Destination && count < Limits::MaxRouteHops && count < evidence.RemainingHops) {
            const DirectedTopologyLink<Characteristics>* selected = nullptr;
            for (const auto& link : evidence.Topology) {
                if (link.Identity.Advertiser == current) {
                    selected = &link;
                    break;
                }
            }
            if (selected == nullptr) return RoutePlanningDisposition::Unreachable;
            hops[count++] = selected->Identity;
            current = selected->Identity.Neighbour;
        }
        if (current != evidence.Destination) return RoutePlanningDisposition::Unreachable;
        return route.Assign(evidence.Source, evidence.Destination, hops, count)
            ? RoutePlanningDisposition::Planned
            : RoutePlanningDisposition::Invalid;
    }
};
}

int main() {
    TopologyGraphStore<Characteristics> graph;
    const auto a = Device(1);
    const auto b = Device(2);
    const auto c = Device(3);
    const auto incarnation = Incarnation(7);

    DirectedTopologyLink<Characteristics> aLinks[]{{{a, 1, b, 1}, {10}}};
    DirectedTopologyLink<Characteristics> bLinks[]{{{b, 1, c, 1}, {20}}};
    assert(graph.ApplyComplete(a, incarnation, 1, aLinks, 1) == TopologySnapshotApplyResult::Applied);
    assert(graph.ApplyComplete(b, incarnation, 1, bLinks, 1) == TopologySnapshotApplyResult::Applied);

    FirstContiguousStrategy strategy;
    ResolvedRoute<> route;
    RoutingEvidence<Characteristics> evidence{graph, a, c, 2};
    assert(strategy.Plan(evidence, route) == RoutePlanningDisposition::Planned);
    assert(route.HopCount() == 2U);
    assert(IsValidPlannedRoute(route, a, c, 2));
    assert(!IsValidPlannedRoute(route, a, c, 1));

    ResolvedRoute<> local;
    RoutingEvidence<Characteristics> localEvidence{graph, a, a, 0};
    assert(strategy.Plan(localEvidence, local) == RoutePlanningDisposition::LocalDestination);
    assert(IsValidPlannedRoute(local, a, a, 0));

    DefaultRouteAttemptPolicy routeAttempts;
    RouteAttemptEvidence attempts{};
    attempts.LastOutcome = RouteAttemptOutcome::RetryableFailure;
    attempts.AttemptsOnCurrentRoute = 1;
    assert(routeAttempts.ShouldRetryCurrentRoute(attempts));
    attempts.AttemptsOnCurrentRoute = Limits::MaxSameRouteAttempts;
    assert(!routeAttempts.ShouldRetryCurrentRoute(attempts));
    attempts.LastOutcome = RouteAttemptOutcome::PermanentFailure;
    attempts.AttemptsOnCurrentRoute = 1;
    assert(!routeAttempts.ShouldRetryCurrentRoute(attempts));

    DefaultRetryPolicy retries;
    attempts = {};
    attempts.LastOutcome = RouteAttemptOutcome::RouteUnavailable;
    attempts.DistinctRoutesAttempted = 1;
    assert(retries.ShouldTryAnotherRoute(attempts));
    attempts.DistinctRoutesAttempted = Limits::MaxRoutesAttempted;
    assert(!retries.ShouldTryAnotherRoute(attempts));
    attempts.DistinctRoutesAttempted = 1;
    attempts.LastOutcome = RouteAttemptOutcome::DeadlineExpired;
    assert(!retries.ShouldTryAnotherRoute(attempts));

    return 0;
}
