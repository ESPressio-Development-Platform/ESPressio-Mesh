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

int main() {
    Mesh::PendingNeighbourCandidateTable<2> candidates;
    Mesh::InboundAuthenticationReservationTable<1> authentications;

    Mesh::NeighbourCandidateHandle first{};
    const Mesh::UntrustedMembershipClaim claim1{Device(1), Incarnation(1)};
    assert(candidates.Observe(1, claim1, 100, first) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(first);
    assert(candidates.Size() == 1);
    assert(candidates.Resolve(first) != nullptr);
    assert(candidates.Resolve(first)->State == Mesh::MembershipState::Discovered);

    Mesh::NeighbourCandidateHandle refreshed{};
    assert(candidates.Observe(1, claim1, 120, refreshed) == Mesh::PendingCandidateInsertResult::Refreshed);
    assert(refreshed == first);
    assert(candidates.Resolve(first)->LastObservedMilliseconds == 120);

    assert(candidates.SetState(first, Mesh::MembershipState::Authenticating));
    assert(!candidates.SetState(first, Mesh::MembershipState::Validating));

    assert(authentications.TryReserve(first) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(authentications.TryReserve(first) == Mesh::InboundAuthenticationReservationResult::AlreadyInProgress);

    Mesh::NeighbourCandidateHandle second{};
    const Mesh::UntrustedMembershipClaim claim2{Device(2), Incarnation(2)};
    assert(candidates.Observe(2, claim2, 130, second) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(authentications.TryReserve(second) == Mesh::InboundAuthenticationReservationResult::ResourceUnavailable);
    assert(authentications.Release(first));
    assert(authentications.TryReserve(second) == Mesh::InboundAuthenticationReservationResult::Reserved);

    // Capacity is explicit; a third candidate cannot consume authenticated-member resources implicitly.
    Mesh::NeighbourCandidateHandle third{};
    assert(candidates.Observe(3, Mesh::UntrustedMembershipClaim{Device(3), Incarnation(3)}, 140, third) ==
           Mesh::PendingCandidateInsertResult::ResourceUnavailable);
    assert(!third);

    // Removing and reusing a slot invalidates the stale handle through generation advancement.
    assert(candidates.Remove(first));
    assert(candidates.Resolve(first) == nullptr);
    Mesh::NeighbourCandidateHandle replacement{};
    assert(candidates.Observe(3, Mesh::UntrustedMembershipClaim{Device(3), Incarnation(3)}, 150, replacement) ==
           Mesh::PendingCandidateInsertResult::Inserted);
    assert(replacement);
    assert(replacement.Slot == first.Slot);
    assert(replacement.Generation != first.Generation);
    assert(candidates.Resolve(first) == nullptr);
    assert(candidates.Resolve(replacement) != nullptr);

    // Invalid Radio identifiers and zero identity claims are rejected before consuming slots.
    Mesh::NeighbourCandidateHandle invalid{};
    assert(candidates.Observe(0, claim1, 160, invalid) == Mesh::PendingCandidateInsertResult::Invalid);
    assert(candidates.Observe(0xFF, claim1, 160, invalid) == Mesh::PendingCandidateInsertResult::Invalid);
    return 0;
}
