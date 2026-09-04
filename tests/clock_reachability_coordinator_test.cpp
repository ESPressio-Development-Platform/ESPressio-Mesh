#include <cassert>
#include <cstdint>

#include <ESPressio_ClockReachabilityCoordinator.hpp>

using namespace ESPressio::Mesh;

namespace {
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

struct Quality final { std::uint16_t Value{0}; };

ClockCoordinationAdvertisement<Quality> Advertisement(
    System::DeviceIdentifier sender,
    MembershipIncarnation incarnation,
    std::uint64_t observedAt
) {
    return {sender, incarnation, sender, ClockRootStratum, {10}, observedAt};
}
}

int main() {
    AuthenticatedMembershipTable<> membership;
    ClockCoordinationTable<Quality> clock;
    ClockReachabilityCoordinator<Quality> coordinator(membership, clock);

    const auto device = Device(1);
    const auto incarnation = Incarnation(1);
    const auto staleIncarnation = Incarnation(2);

    assert(coordinator.Converge({}, incarnation) == ClockReachabilityDisposition::Invalid);
    assert(coordinator.Converge(device, incarnation) == ClockReachabilityDisposition::MembershipUnavailable);

    assert(membership.UpsertAuthenticated(
        device,
        incarnation,
        MembershipState::Active,
        ReachabilityState::Reachable
    ) == AuthenticatedMembershipUpsertResult::Inserted);
    assert(clock.Observe(Advertisement(device, incarnation, 100)));
    assert(clock.Size() == 1U);

    // Reachable and Suspect are still authenticated candidates; policy may decide whether they are desirable.
    assert(coordinator.Converge(device, incarnation) == ClockReachabilityDisposition::Retained);
    assert(clock.Size() == 1U);
    assert(membership.SetReachability(device, incarnation, ReachabilityState::Suspect));
    assert(coordinator.Converge(device, incarnation) == ClockReachabilityDisposition::Retained);
    assert(clock.Size() == 1U);

    // Only the exact current Unreachable incarnation loses retained clock-election state.
    assert(membership.SetReachability(device, incarnation, ReachabilityState::Unreachable));
    assert(coordinator.Converge(device, staleIncarnation) == ClockReachabilityDisposition::MembershipUnavailable);
    assert(clock.Size() == 1U);
    assert(coordinator.Converge(device, incarnation) == ClockReachabilityDisposition::Removed);
    assert(clock.Size() == 0U);
    assert(coordinator.Converge(device, incarnation) == ClockReachabilityDisposition::Retained);

    // Restored authenticated reachability allows a later authenticated observation to participate again.
    assert(membership.SetReachability(device, incarnation, ReachabilityState::Reachable));
    assert(clock.Observe(Advertisement(device, incarnation, 200)));
    assert(clock.Size() == 1U);
    assert(coordinator.Converge(device, incarnation) == ClockReachabilityDisposition::Retained);
    assert(clock.Size() == 1U);

    return 0;
}
