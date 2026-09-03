#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_MembershipLifecycleCoordinator.hpp"

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
    Mesh::MembershipTombstoneTable<4> tombstones;
    Mesh::DefaultMeshLivenessPolicy policy{100, 200};
    Mesh::MembershipLivenessTracker<2> liveness{members, policy};
    Mesh::MembershipRetentionCoordinator<2, 4> retention{members, tombstones};
    Mesh::MembershipLifecycleCoordinator<2, 4> lifecycle{members, liveness, retention};

    const auto device = Device(1);
    const auto incarnation = Incarnation(1);
    assert(members.UpsertAuthenticated(device, incarnation, Mesh::MembershipState::Active) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(liveness.ObserveAuthenticatedEvidence(device, incarnation, 1'000));

    assert(lifecycle.Evaluate(device, incarnation, 1'099, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::NoChange);
    assert(lifecycle.Evaluate(device, incarnation, 1'100, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::ReachabilityChanged);
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Suspect);

    assert(lifecycle.Evaluate(device, incarnation, 1'200, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::ReachabilityChanged);
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Unreachable);

    // The complete record remains available throughout the requested unreachable retention interval.
    assert(lifecycle.Evaluate(device, incarnation, 1'699, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::NoChange);
    assert(members.FindExact(device, incarnation) != nullptr);

    // Expiry retires through the retention coordinator, creating compact local history first.
    assert(lifecycle.Evaluate(device, incarnation, 1'700, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::RetiredLocallyForgotten);
    assert(members.FindExact(device, incarnation) == nullptr);
    const auto* tombstone = tombstones.FindRetained(device, incarnation);
    assert(tombstone != nullptr);
    assert(tombstone->Disposition == Mesh::MembershipTombstoneDisposition::LocallyForgotten);

    assert(lifecycle.Evaluate(device, incarnation, 1'701, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::MembershipNotFound);
    return 0;
}
