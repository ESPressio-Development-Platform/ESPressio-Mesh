#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_MembershipLiveness.hpp"

using namespace ESPressio;

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
    Mesh::AuthenticatedMembershipTable<2> members;
    const auto device = Device(1);
    const auto incarnation = Incarnation(1);
    const auto otherIncarnation = Incarnation(2);

    assert(members.UpsertAuthenticated(device, incarnation, Mesh::MembershipState::Active) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Mesh::DefaultMeshLivenessPolicy policy{5'000, 15'000};
    Mesh::MembershipLivenessTracker<2> liveness{members, policy};

    // Evidence is accepted only for an already-authenticated exact incarnation.
    assert(!liveness.ObserveAuthenticatedEvidence(device, otherIncarnation, 1'000));
    assert(liveness.ObserveAuthenticatedEvidence(device, incarnation, 1'000));
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Reachable);

    assert(liveness.Evaluate(device, incarnation, 5'999) == Mesh::ReachabilityState::Reachable);
    assert(liveness.Evaluate(device, incarnation, 6'000) == Mesh::ReachabilityState::Suspect);
    assert(liveness.Evaluate(device, incarnation, 16'000) == Mesh::ReachabilityState::Unreachable);

    // Full authenticated member state remains retained while locally unreachable.
    assert(members.FindExact(device, incarnation) != nullptr);
    assert(!liveness.IsUnreachableRetentionElapsed(device, incarnation, 75'999));
    assert(liveness.IsUnreachableRetentionElapsed(device, incarnation, 76'000));

    // Any subsequent valid authenticated evidence restores Reachable immediately and clears expiry age.
    assert(liveness.ObserveAuthenticatedEvidence(device, incarnation, 20'000));
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Reachable);
    assert(!liveness.IsUnreachableRetentionElapsed(device, incarnation, 100'000));

    // Stale monotonic evidence cannot move the evidence clock backwards.
    assert(!liveness.ObserveAuthenticatedEvidence(device, incarnation, 19'999));
    assert(liveness.Evaluate(device, incarnation, 25'000) == Mesh::ReachabilityState::Suspect);

    // Policy is replaceable and owns thresholds rather than membership storage.
    Mesh::DefaultMeshLivenessPolicy faster{100, 200};
    Mesh::MembershipLivenessTracker<2> fasterTracker{members, faster};
    assert(fasterTracker.ObserveAuthenticatedEvidence(device, incarnation, 30'000));
    assert(fasterTracker.Evaluate(device, incarnation, 30'099) == Mesh::ReachabilityState::Reachable);
    assert(fasterTracker.Evaluate(device, incarnation, 30'100) == Mesh::ReachabilityState::Suspect);
    assert(fasterTracker.Evaluate(device, incarnation, 30'200) == Mesh::ReachabilityState::Unreachable);

    assert(fasterTracker.Forget(device, incarnation));
    assert(!fasterTracker.IsUnreachableRetentionElapsed(device, incarnation, 100'000));
    return 0;
}
