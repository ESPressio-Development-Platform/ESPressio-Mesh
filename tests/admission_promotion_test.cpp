#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_AdmissionPromotionCoordinator.hpp"

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

static Radio::RadioPeerHandle Peer(std::uint16_t slot, std::uint16_t generation) {
    return Radio::RadioPeerHandle{slot, generation};
}

int main() {
    Mesh::PendingNeighbourCandidateTable<3> candidates;
    Mesh::InboundAuthenticationReservationTable<1> authentications;
    Mesh::AuthenticatedMembershipTable<1> memberships;
    Mesh::AdmissionPromotionCoordinator<3, 1, 1> promotion{candidates, authentications, memberships};

    const auto claimedDevice = Device(1);
    const auto claimedIncarnation = Incarnation(1);
    Mesh::NeighbourCandidateHandle first{};
    assert(candidates.Observe(
               1,
               Peer(0, 1),
               Mesh::UntrustedMembershipClaim{claimedDevice, claimedIncarnation},
               100,
               first) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.SetState(first, Mesh::MembershipState::Authenticating));
    assert(authentications.TryReserve(first) == Mesh::InboundAuthenticationReservationResult::Reserved);

    // Authenticated identity, not the candidate's untrusted claim, becomes membership authority.
    const auto authenticatedDevice = Device(9);
    const auto authenticatedIncarnation = Incarnation(9);
    assert(promotion.CompleteAuthenticated(first, authenticatedDevice, authenticatedIncarnation) ==
           Mesh::AdmissionPromotionResult::PromotedToValidating);
    assert(candidates.Resolve(first) == nullptr);
    assert(!authentications.Contains(first));
    const auto* promoted = memberships.FindExact(authenticatedDevice, authenticatedIncarnation);
    assert(promoted != nullptr);
    assert(promoted->State == Mesh::MembershipState::Validating);
    assert(promoted->Reachability == Mesh::ReachabilityState::Reachable);
    assert(memberships.FindExact(claimedDevice, claimedIncarnation) == nullptr);

    // A conflicting new incarnation cannot silently replace existing authenticated authority.
    Mesh::NeighbourCandidateHandle conflict{};
    assert(candidates.Observe(
               1,
               Peer(1, 1),
               Mesh::UntrustedMembershipClaim{authenticatedDevice, Incarnation(10)},
               200,
               conflict) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.SetState(conflict, Mesh::MembershipState::Authenticating));
    assert(authentications.TryReserve(conflict) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(promotion.CompleteAuthenticated(conflict, authenticatedDevice, Incarnation(10)) ==
           Mesh::AdmissionPromotionResult::ConflictingIncarnation);
    assert(!authentications.Contains(conflict));
    assert(candidates.Resolve(conflict) != nullptr);
    assert(candidates.Resolve(conflict)->State == Mesh::MembershipState::Discovered);
    assert(memberships.FindExact(authenticatedDevice, authenticatedIncarnation) != nullptr);

    // A definitive rejection releases all pre-auth resources and never creates membership.
    Mesh::NeighbourCandidateHandle rejected{};
    assert(candidates.Observe(
               2,
               Peer(2, 1),
               Mesh::UntrustedMembershipClaim{Device(2), Incarnation(2)},
               300,
               rejected) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.SetState(rejected, Mesh::MembershipState::Authenticating));
    assert(authentications.TryReserve(rejected) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(promotion.CompleteRejected(rejected));
    assert(candidates.Resolve(rejected) == nullptr);
    assert(!authentications.Contains(rejected));
    assert(memberships.FindDevice(Device(2)) == nullptr);

    // Retryable pressure releases expensive authentication but preserves discovery state.
    assert(candidates.SetState(conflict, Mesh::MembershipState::Authenticating));
    assert(authentications.TryReserve(conflict) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(promotion.ReleaseRetryable(conflict));
    assert(candidates.Resolve(conflict) != nullptr);
    assert(candidates.Resolve(conflict)->State == Mesh::MembershipState::Discovered);
    assert(!authentications.Contains(conflict));
    return 0;
}
