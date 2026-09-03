#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_TopologyFreshness.hpp"

using namespace ESPressio;

struct Characteristics final {
    std::uint16_t ReliabilityPermille{0};
    constexpr bool operator==(const Characteristics& other) const noexcept {
        return ReliabilityPermille == other.ReliabilityPermille;
    }
};

using Snapshot = Mesh::TopologySnapshot<Characteristics, 2>;

class TestFreshnessPolicy final : public Mesh::ITopologyFreshnessPolicy<Characteristics, 2> {
public:
    Mesh::TopologyFreshnessState Classify(
        const Mesh::TopologyFreshnessEvidence<Characteristics, 2>& evidence
    ) const noexcept override {
        // Demonstrates that policy can consume both reachability and normalized link characteristics.
        if (evidence.AuthorityReachability == Mesh::ReachabilityState::Unreachable) {
            return Mesh::TopologyFreshnessState::Stale;
        }
        if (evidence.Snapshot.Size() != 0 &&
            evidence.Snapshot.begin()->Characteristics.ReliabilityPermille < 500) {
            return Mesh::TopologyFreshnessState::Degraded;
        }
        if (evidence.ExpectedCadenceMilliseconds != 0U &&
            evidence.LocalAgeMilliseconds >= evidence.ExpectedCadenceMilliseconds * 4U) {
            return Mesh::TopologyFreshnessState::Expired;
        }
        if (evidence.ExpectedCadenceMilliseconds != 0U &&
            evidence.LocalAgeMilliseconds >= evidence.ExpectedCadenceMilliseconds * 2U) {
            return Mesh::TopologyFreshnessState::Stale;
        }
        if (evidence.ExpectedCadenceMilliseconds != 0U &&
            evidence.LocalAgeMilliseconds >= evidence.ExpectedCadenceMilliseconds) {
            return Mesh::TopologyFreshnessState::Degraded;
        }
        return Mesh::TopologyFreshnessState::Fresh;
    }
};

static System::DeviceIdentifier Device(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return Mesh::MembershipIncarnation{bytes};
}

int main() {
    const auto authority = Device(1);
    const auto neighbour = Device(2);
    const auto incarnation = Incarnation(1);
    const Snapshot::Link link{{authority, 1, neighbour, 2}, {900}};

    Snapshot snapshot;
    assert(snapshot.ApplyComplete(authority, incarnation, 1, &link, 1) ==
           Mesh::TopologySnapshotApplyResult::Applied);

    TestFreshnessPolicy policy;
    Mesh::TopologyFreshnessTracker<Characteristics, 2> freshness{snapshot, policy};

    // No local authenticated receipt means no usable freshness assertion.
    assert(freshness.Evaluate(100, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Expired);

    assert(freshness.ObserveAuthenticatedReceipt(authority, incarnation, 1, 100));
    assert(freshness.Evaluate(150, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Fresh);
    assert(freshness.Evaluate(200, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Degraded);
    assert(freshness.Evaluate(300, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Stale);
    assert(freshness.Evaluate(500, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Expired);

    // Same-generation authenticated retransmission refreshes local age without changing generation.
    assert(snapshot.ApplyComplete(authority, incarnation, 1, &link, 1) ==
           Mesh::TopologySnapshotApplyResult::RefreshedSameGeneration);
    assert(freshness.ObserveAuthenticatedReceipt(authority, incarnation, 1, 600));
    assert(snapshot.Generation() == 1);
    assert(freshness.Evaluate(650, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Fresh);

    // Authority reachability is policy input and may degrade retained topology immediately.
    assert(freshness.Evaluate(651, Mesh::ReachabilityState::Unreachable, 100) ==
           Mesh::TopologyFreshnessState::Stale);

    // Old incarnation/generation and monotonic regressions cannot refresh the retained authority.
    assert(!freshness.ObserveAuthenticatedReceipt(authority, Incarnation(2), 1, 700));
    assert(!freshness.ObserveAuthenticatedReceipt(authority, incarnation, 2, 700));
    assert(!freshness.ObserveAuthenticatedReceipt(authority, incarnation, 1, 599));
    assert(freshness.LastReceiptMilliseconds() == 600);

    // A new complete generation has no freshness until its authenticated receipt is explicitly observed.
    const Snapshot::Link changed{{authority, 1, neighbour, 2}, {450}};
    assert(snapshot.ApplyComplete(authority, incarnation, 2, &changed, 1) ==
           Mesh::TopologySnapshotApplyResult::Applied);
    freshness.Reset();
    assert(freshness.Evaluate(800, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Expired);
    assert(freshness.ObserveAuthenticatedReceipt(authority, incarnation, 2, 800));
    assert(freshness.Evaluate(801, Mesh::ReachabilityState::Reachable, 100) ==
           Mesh::TopologyFreshnessState::Degraded);

    return 0;
}
