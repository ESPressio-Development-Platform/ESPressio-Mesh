#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_AdmissionResources.hpp"

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
    Mesh::PendingNeighbourCandidateTable<2> candidates;
    Mesh::InboundAuthenticationReservationTable<1> authentications;

    Mesh::NeighbourCandidateHandle first{};
    const Mesh::UntrustedMembershipClaim claim1{Device(1), Incarnation(1)};
    assert(candidates.Observe(1, Peer(0, 1), claim1, 100, first) ==
           Mesh::PendingCandidateInsertResult::Inserted);
    assert(first);
    assert(candidates.Size() == 1);
    assert(candidates.Resolve(first) != nullptr);
    assert(candidates.Resolve(first)->State == Mesh::MembershipState::Discovered);
    assert(candidates.Resolve(first)->Peer == Peer(0, 1));

    Mesh::NeighbourCandidateHandle refreshed{};
    assert(candidates.Observe(1, Peer(0, 1), claim1, 120, refreshed) ==
           Mesh::PendingCandidateInsertResult::Refreshed);
    assert(refreshed == first);
    assert(candidates.Resolve(first)->LastObservedMilliseconds == 120);

    // The same untrusted identity claim through a distinct Radio peer binding is a distinct candidate.
    Mesh::NeighbourCandidateHandle second{};
    assert(candidates.Observe(2, Peer(1, 1), claim1, 125, second) ==
           Mesh::PendingCandidateInsertResult::Inserted);
    assert(second != first);

    assert(candidates.SetState(first, Mesh::MembershipState::Authenticating));
    assert(!candidates.SetState(first, Mesh::MembershipState::Validating));

    assert(authentications.TryReserve(first) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(authentications.TryReserve(first) == Mesh::InboundAuthenticationReservationResult::AlreadyInProgress);
    assert(authentications.TryReserve(second) == Mesh::InboundAuthenticationReservationResult::ResourceUnavailable);
    assert(authentications.Release(first));
    assert(authentications.TryReserve(second) == Mesh::InboundAuthenticationReservationResult::Reserved);

    // Capacity is explicit; a third candidate cannot consume authenticated-member resources implicitly.
    Mesh::NeighbourCandidateHandle third{};
    assert(candidates.Observe(3, Peer(2, 1), Mesh::UntrustedMembershipClaim{Device(3), Incarnation(3)}, 140, third) ==
           Mesh::PendingCandidateInsertResult::ResourceUnavailable);
    assert(!third);

    // Removing and reusing a slot invalidates the stale Mesh-local candidate handle through generation advancement.
    assert(candidates.Remove(first));
    assert(candidates.Resolve(first) == nullptr);
    Mesh::NeighbourCandidateHandle replacement{};
    assert(candidates.Observe(3, Peer(2, 1), Mesh::UntrustedMembershipClaim{Device(3), Incarnation(3)}, 150, replacement) ==
           Mesh::PendingCandidateInsertResult::Inserted);
    assert(replacement);
    assert(replacement.Slot == first.Slot);
    assert(replacement.Generation != first.Generation);
    assert(candidates.Resolve(first) == nullptr);
    assert(candidates.Resolve(replacement) != nullptr);

    // Invalid Mesh Radio identifiers and invalid Radio-owned peer handles are rejected before consuming slots.
    Mesh::NeighbourCandidateHandle invalid{};
    assert(candidates.Observe(0, Peer(3, 1), claim1, 160, invalid) == Mesh::PendingCandidateInsertResult::Invalid);
    assert(candidates.Observe(0xFF, Peer(3, 1), claim1, 160, invalid) == Mesh::PendingCandidateInsertResult::Invalid);
    assert(candidates.Observe(1, Radio::RadioPeerHandle{}, claim1, 160, invalid) == Mesh::PendingCandidateInsertResult::Invalid);
    return 0;
}
