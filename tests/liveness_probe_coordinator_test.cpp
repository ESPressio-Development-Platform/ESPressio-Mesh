#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_LivenessProbeCoordinator.hpp"

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

class TestProbePolicy final : public Mesh::IMeshLivenessProbePolicy {
public:
    bool ShouldProbe(const Mesh::LivenessProbeAssessment& assessment) const noexcept override {
        Last = assessment;
        ++Calls;
        return Eligible && assessment.Membership == Mesh::MembershipState::Active;
    }
    mutable Mesh::LivenessProbeAssessment Last{};
    mutable int Calls{0};
    bool Eligible{false};
};

class TestProbeInitiator final : public Mesh::ILivenessProbeInitiator {
public:
    Mesh::LivenessProbeStartDisposition TryStartProbe(
        const System::DeviceIdentifier& device,
        const Mesh::MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        Mesh::LivenessProbeReservation reservation
    ) noexcept override {
        LastDevice = device;
        LastIncarnation = incarnation;
        LastNowMilliseconds = nowMilliseconds;
        LastReservation = reservation;
        ++Calls;
        return Next;
    }

    Mesh::LivenessProbeStartDisposition Next{Mesh::LivenessProbeStartDisposition::Started};
    System::DeviceIdentifier LastDevice{};
    Mesh::MembershipIncarnation LastIncarnation{};
    std::uint64_t LastNowMilliseconds{0};
    Mesh::LivenessProbeReservation LastReservation{};
    int Calls{0};
};

int main() {
    Mesh::AuthenticatedMembershipTable<3> members;
    Mesh::DefaultMeshLivenessPolicy livenessPolicy{100, 200};
    Mesh::MembershipLivenessTracker<3> liveness{members, livenessPolicy};
    Mesh::LivenessProbeReservationTable<1> reservations;
    TestProbePolicy probePolicy;
    TestProbeInitiator initiator;
    Mesh::LivenessProbeCoordinator<3, 1> coordinator{
        members, liveness, reservations, probePolicy, initiator
    };

    const auto device1 = Device(1);
    const auto incarnation1 = Incarnation(1);
    assert(members.UpsertAuthenticated(
        device1, incarnation1, Mesh::MembershipState::Active, Mesh::ReachabilityState::Unknown
    ) == Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(liveness.ObserveAuthenticatedEvidence(device1, incarnation1, 1000));

    Mesh::LivenessProbeReservation probe{};
    probePolicy.Eligible = false;
    assert(coordinator.Consider(device1, incarnation1, 1050, probe) ==
           Mesh::LivenessProbeCoordinatorResult::NotEligible);
    assert(!probe);
    assert(probePolicy.Last.Reachability == Mesh::ReachabilityState::Reachable);
    assert(probePolicy.Last.HasAuthenticatedEvidence);
    assert(probePolicy.Last.EvidenceAgeMilliseconds == 50);
    assert(initiator.Calls == 0);

    probePolicy.Eligible = true;
    assert(coordinator.Consider(device1, incarnation1, 1100, probe) ==
           Mesh::LivenessProbeCoordinatorResult::Started);
    assert(probe);
    assert(probePolicy.Last.Reachability == Mesh::ReachabilityState::Suspect);
    assert(probePolicy.Last.EvidenceAgeMilliseconds == 100);
    assert(initiator.Calls == 1);
    assert(initiator.LastDevice == device1);
    assert(initiator.LastIncarnation == incarnation1);
    assert(initiator.LastNowMilliseconds == 1100);
    assert(initiator.LastReservation.Slot == probe.Slot);

    Mesh::LivenessProbeReservation duplicate{};
    assert(coordinator.Consider(device1, incarnation1, 1101, duplicate) ==
           Mesh::LivenessProbeCoordinatorResult::AlreadyInProgress);
    assert(duplicate.Slot == probe.Slot);
    assert(initiator.Calls == 1);

    assert(coordinator.Complete(probe));
    initiator.Next = Mesh::LivenessProbeStartDisposition::TemporarilyUnavailable;
    Mesh::LivenessProbeReservation temporary{};
    assert(coordinator.Consider(device1, incarnation1, 1200, temporary) ==
           Mesh::LivenessProbeCoordinatorResult::InitiatorTemporarilyUnavailable);
    assert(!temporary);
    assert(reservations.Size() == 0);
    assert(initiator.LastNowMilliseconds == 1200);

    initiator.Next = Mesh::LivenessProbeStartDisposition::Rejected;
    Mesh::LivenessProbeReservation rejected{};
    assert(coordinator.Consider(device1, incarnation1, 1201, rejected) ==
           Mesh::LivenessProbeCoordinatorResult::InitiatorRejected);
    assert(!rejected);
    assert(reservations.Size() == 0);

    initiator.Next = Mesh::LivenessProbeStartDisposition::Started;
    Mesh::LivenessProbeReservation unreachable{};
    assert(coordinator.Consider(device1, incarnation1, 1300, unreachable) ==
           Mesh::LivenessProbeCoordinatorResult::Started);
    assert(probePolicy.Last.Reachability == Mesh::ReachabilityState::Unreachable);
    assert(coordinator.Complete(unreachable));

    const auto device2 = Device(2);
    const auto incarnation2 = Incarnation(2);
    assert(members.UpsertAuthenticated(
        device2, incarnation2, Mesh::MembershipState::Validating, Mesh::ReachabilityState::Unknown
    ) == Mesh::AuthenticatedMembershipInsertResult::Inserted);
    Mesh::LivenessProbeReservation validating{};
    assert(coordinator.Consider(device2, incarnation2, 1400, validating) ==
           Mesh::LivenessProbeCoordinatorResult::NotEligible);
    assert(!probePolicy.Last.HasAuthenticatedEvidence);

    Mesh::LivenessProbeReservation unknown{};
    assert(coordinator.Consider(Device(9), Incarnation(9), 1500, unknown) ==
           Mesh::LivenessProbeCoordinatorResult::MemberNotFound);
    assert(coordinator.Consider(device1, incarnation1, 0, unknown) ==
           Mesh::LivenessProbeCoordinatorResult::InvalidTime);

    return 0;
}
