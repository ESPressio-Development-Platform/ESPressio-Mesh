#include <cassert>
#include <cstdint>

#include <ESPressio_Mesh.hpp>

using namespace ESPressio::Mesh;

static ESPressio::System::DeviceIdentifier Device(std::uint8_t value) {
    ESPressio::System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return ESPressio::System::DeviceIdentifier(bytes);
}

static MembershipIncarnation Incarnation(std::uint8_t value) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return MembershipIncarnation(bytes);
}

int main() {
    using Memberships = AuthenticatedMembershipTable<2>;
    using Reservations = InboundDeliveryReservationTable<1>;
    using Coordinator = InboundDeliveryCoordinator<2, 1>;

    Memberships memberships;
    Reservations reservations;
    Coordinator coordinator(memberships, reservations);

    const auto deviceA = Device(1);
    const auto incarnationA = Incarnation(1);
    const auto incarnationB = Incarnation(2);

    assert(memberships.UpsertAuthenticated(
        deviceA,
        incarnationA,
        MembershipState::Validating,
        ReachabilityState::Reachable
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(memberships.Size() == 1);

    assert(memberships.UpsertAuthenticated(
        deviceA,
        incarnationA,
        MembershipState::Joining,
        ReachabilityState::Reachable
    ) == AuthenticatedMembershipInsertResult::Updated);
    assert(memberships.FindExact(deviceA, incarnationA)->State == MembershipState::Joining);

    // A newer-looking incarnation must never silently replace the retained authenticated one.
    assert(memberships.UpsertAuthenticated(
        deviceA,
        incarnationB,
        MembershipState::Joining
    ) == AuthenticatedMembershipInsertResult::ConflictingIncarnation);

    const InboundDeliveryIdentity first{deviceA, incarnationA, 1};
    assert(coordinator.TryBegin(first) == InboundDeliveryBeginResult::Reserved);
    assert(coordinator.TryBegin(first) == InboundDeliveryBeginResult::AlreadyInProgress);

    // Retryable local pressure releases InProgress without poisoning committed dedup history.
    assert(coordinator.ReleaseRetryable(first));
    assert(coordinator.TryBegin(first) == InboundDeliveryBeginResult::Reserved);
    assert(coordinator.CommitDefinitive(first) == InboundDeliveryCommitResult::Committed);
    assert(coordinator.TryBegin(first) == InboundDeliveryBeginResult::Duplicate);
    assert(coordinator.WasAccepted(first) == InboundDeliveryAcceptanceResult::NotAccepted);

    // Positive ACK regeneration is retained only for the bounded subset that reached framework acceptance.
    const InboundDeliveryIdentity accepted{deviceA, incarnationA, 2};
    assert(coordinator.TryBegin(accepted) == InboundDeliveryBeginResult::Reserved);
    assert(coordinator.CommitAccepted(accepted) == InboundDeliveryCommitResult::Committed);
    assert(coordinator.TryBegin(accepted) == InboundDeliveryBeginResult::Duplicate);
    assert(coordinator.WasAccepted(accepted) == InboundDeliveryAcceptanceResult::Accepted);

    // Unknown authenticated identities cannot create reservation or dedup state.
    const InboundDeliveryIdentity wrongIncarnation{deviceA, incarnationB, 2};
    assert(coordinator.TryBegin(wrongIncarnation) ==
           InboundDeliveryBeginResult::UnknownAuthenticatedMembership);
    assert(reservations.Empty());

    // Advance committed history enough to make a very old unseen sequence conservatively TooOld.
    const InboundDeliveryIdentity newest{deviceA, incarnationA, 200};
    assert(coordinator.TryBegin(newest) == InboundDeliveryBeginResult::Reserved);
    assert(coordinator.CommitDefinitive(newest) == InboundDeliveryCommitResult::Committed);
    const InboundDeliveryIdentity stale{deviceA, incarnationA, 3};
    assert(coordinator.TryBegin(stale) == InboundDeliveryBeginResult::TooOld);

    // If membership disappears during bounded semantic handoff, no orphaned dedup commit occurs.
    const InboundDeliveryIdentity disappearing{deviceA, incarnationA, 201};
    assert(coordinator.TryBegin(disappearing) == InboundDeliveryBeginResult::Reserved);
    assert(memberships.Remove(deviceA, incarnationA));
    assert(coordinator.CommitDefinitive(disappearing) ==
           InboundDeliveryCommitResult::MembershipUnavailable);
    assert(reservations.Empty());

    // Capacity remains independently bounded.
    assert(memberships.UpsertAuthenticated(
        Device(2), Incarnation(2), MembershipState::Active
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(memberships.UpsertAuthenticated(
        Device(3), Incarnation(3), MembershipState::Active
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(memberships.UpsertAuthenticated(
        Device(4), Incarnation(4), MembershipState::Active
    ) == AuthenticatedMembershipInsertResult::ResourceUnavailable);

    return 0;
}
