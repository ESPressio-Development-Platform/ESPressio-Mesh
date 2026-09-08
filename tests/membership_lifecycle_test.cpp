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

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Joining (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Authenticated (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Unavailable (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Lost (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Disconnected (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Last (Mesh::MeshNodeLifecycleNotification): 35 bytes [0 bytes dynamic allocation]
 * Total Memory: 60 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class LifecycleObserver final : public Mesh::IMeshLifecycleObserver {
public:
    std::uint32_t Joining{0};
    std::uint32_t Authenticated{0};
    std::uint32_t Unavailable{0};
    std::uint32_t Lost{0};
    std::uint32_t Disconnected{0};
    Mesh::MeshNodeLifecycleNotification Last{};

    void OnMeshNodeJoining(const Mesh::MeshNodeLifecycleNotification& event) override {
        ++Joining; Last = event;
    }
    void OnMeshNodeAuthenticated(const Mesh::MeshNodeLifecycleNotification& event) override {
        ++Authenticated; Last = event;
    }
    void OnMeshNodeUnavailable(const Mesh::MeshNodeLifecycleNotification& event) override {
        ++Unavailable; Last = event;
    }
    void OnMeshNodeLost(const Mesh::MeshNodeLifecycleNotification& event) override {
        ++Lost; Last = event;
    }
    void OnMeshNodeDisconnected(const Mesh::MeshNodeLifecycleNotification& event) override {
        ++Disconnected; Last = event;
    }
};

int main() {
    Mesh::AuthenticatedMembershipTable<2> members;
    Mesh::MembershipTombstoneTable<4> tombstones;
    Mesh::DefaultMeshLivenessPolicy policy{100, 200};
    Mesh::MembershipLivenessTracker<2> liveness{members, policy};
    Mesh::MembershipRetentionCoordinator<2, 4> retention{members, tombstones};
    Mesh::MeshLifecycleNotifications notifications;
    LifecycleObserver observer;
    auto observerHandle = notifications.RegisterObserver(&observer);
    assert(observerHandle);
    Mesh::MembershipLifecycleCoordinator<2, 4> lifecycle{
        members, liveness, retention, &notifications};

    const auto device = Device(1);
    const auto incarnation = Incarnation(1);
    assert(members.UpsertAuthenticated(device, incarnation, Mesh::MembershipState::Validating) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(lifecycle.ActivateAuthenticated(device, incarnation, 1'000) ==
           Mesh::MembershipLifecycleResult::ActivatedAuthenticated);
    assert(observer.Joining == 1U && observer.Authenticated == 1U);
    assert(observer.Last.Device == device && observer.Last.Incarnation == incarnation);
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Reachable);

    assert(lifecycle.Evaluate(device, incarnation, 1'099, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::NoChange);
    assert(lifecycle.Evaluate(device, incarnation, 1'100, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::ReachabilityChanged);
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Suspect);

    assert(lifecycle.Evaluate(device, incarnation, 1'200, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::ReachabilityChanged);
    assert(members.FindExact(device, incarnation)->Reachability == Mesh::ReachabilityState::Unreachable);
    assert(observer.Unavailable == 1U);

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
    assert(observer.Lost == 1U);
    assert(observer.Last.Reason == Mesh::MeshNodeLifecycleReason::UnreachableTimeout);

    // A cryptographically authenticated new incarnation may retire the old incarnation immediately.
    const auto oldIncarnation = Incarnation(2);
    assert(members.UpsertAuthenticated(device, oldIncarnation, Mesh::MembershipState::Active,
                                       Mesh::ReachabilityState::Reachable) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(liveness.ObserveAuthenticatedEvidence(device, oldIncarnation, 2'000));
    assert(lifecycle.RecordSupersededIncarnation(device, oldIncarnation, 2'001, 1'000) ==
           Mesh::MembershipLifecycleResult::RetiredSupersededIncarnation);
    assert(members.FindExact(device, oldIncarnation) == nullptr);
    tombstone = tombstones.FindRetained(device, oldIncarnation);
    assert(tombstone != nullptr);
    assert(tombstone->Disposition == Mesh::MembershipTombstoneDisposition::SupersededIncarnation);
    assert(observer.Lost == 2U);
    assert(observer.Last.Reason == Mesh::MeshNodeLifecycleReason::SupersededIncarnation);

    // Graceful authenticated Leave is semantically distinct from timeout/loss.
    const auto leaveIncarnation = Incarnation(3);
    assert(members.UpsertAuthenticated(device, leaveIncarnation, Mesh::MembershipState::Active,
                                       Mesh::ReachabilityState::Reachable) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(liveness.ObserveAuthenticatedEvidence(device, leaveIncarnation, 3'000));
    assert(lifecycle.RecordAuthoritativeLeave(device, leaveIncarnation, 3'001, 1'000) ==
           Mesh::MembershipLifecycleResult::RetiredAuthoritativeLeave);
    assert(observer.Disconnected == 1U);
    assert(observer.Last.Reason == Mesh::MeshNodeLifecycleReason::AuthoritativeLeave);

    assert(lifecycle.Evaluate(device, leaveIncarnation, 3'002, 500, 1'000) ==
           Mesh::MembershipLifecycleResult::MembershipNotFound);
    return 0;
}