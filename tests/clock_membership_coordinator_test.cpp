#include <cassert>
#include <cstdint>

#include <ESPressio_ClockMembershipCoordinator.hpp>

using namespace ESPressio::Mesh;

namespace {
struct Quality final { std::uint16_t Score{0}; };

System::DeviceIdentifier Device(std::uint8_t value) {
    System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return System::DeviceIdentifier(bytes);
}

MembershipIncarnation Incarnation(std::uint8_t value) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return MembershipIncarnation(bytes);
}

ClockCoordinationAdvertisement<Quality> Advertisement(
    const System::DeviceIdentifier& sender,
    const MembershipIncarnation& incarnation,
    const System::DeviceIdentifier& root,
    ClockStratum stratum,
    std::uint64_t observedAt
) {
    return {sender, incarnation, root, stratum, Quality{1}, observedAt};
}
}

int main() {
    AuthenticatedMembershipTable<> membership;
    ClockCoordinationTable<Quality> clock;
    ClockMembershipCoordinator<Quality> coordinator(membership, clock);

    const auto root = Device(1);
    const auto peer = Device(2);
    const auto oldIncarnation = Incarnation(1);
    const auto currentIncarnation = Incarnation(2);

    const auto current = Advertisement(peer, currentIncarnation, root, 1, 100);
    assert(coordinator.ObserveAuthenticated(current) == ClockObservationDisposition::MembershipUnavailable);
    assert(clock.Size() == 0U);

    assert(membership.UpsertAuthenticated(
        peer,
        currentIncarnation,
        MembershipState::Validating,
        ReachabilityState::Reachable
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(coordinator.ObserveAuthenticated(current) == ClockObservationDisposition::MembershipNotActive);
    assert(clock.Size() == 0U);

    assert(membership.SetMembershipState(peer, currentIncarnation, MembershipState::Active));
    assert(coordinator.ObserveAuthenticated(current) == ClockObservationDisposition::Observed);
    assert(clock.Size() == 1U);

    // An authenticated identity for another incarnation cannot refresh or replace the retained observation.
    const auto stale = Advertisement(peer, oldIncarnation, root, 1, 200);
    assert(coordinator.ObserveAuthenticated(stale) == ClockObservationDisposition::MembershipUnavailable);
    assert(clock.Size() == 1U);

    // Same-incarnation monotonic regression is rejected by the clock table and cannot overwrite newer evidence.
    const auto regressed = Advertisement(peer, currentIncarnation, root, 1, 99);
    assert(coordinator.ObserveAuthenticated(regressed) == ClockObservationDisposition::ResourceUnavailable);
    assert(clock.Size() == 1U);

    // Retirement cleanup is exact-incarnation: a stale incarnation cannot remove current clock state.
    assert(!coordinator.ForgetRetiredMembership(peer, oldIncarnation));
    assert(clock.Size() == 1U);
    assert(coordinator.ForgetRetiredMembership(peer, currentIncarnation));
    assert(clock.Size() == 0U);

    // A replacement incarnation becomes eligible only after membership authority has retired the old record and admitted it.
    assert(membership.Remove(peer, currentIncarnation));
    assert(membership.UpsertAuthenticated(
        peer,
        oldIncarnation,
        MembershipState::Active,
        ReachabilityState::Reachable
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(coordinator.ObserveAuthenticated(stale) == ClockObservationDisposition::Observed);
    assert(clock.Size() == 1U);

    coordinator.Clear();
    assert(clock.Size() == 0U);
    return 0;
}
