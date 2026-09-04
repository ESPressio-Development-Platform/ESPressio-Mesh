#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_ForwardingTransitionCoordinator.hpp>

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t value) {
    System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return System::DeviceIdentifier(bytes);
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return Mesh::MembershipIncarnation(bytes);
}
}

int main() {
    const auto nextHop = Device(2);
    const auto otherNode = Device(3);
    const auto incarnation = Incarnation(7);
    const auto laterIncarnation = Incarnation(8);

    Mesh::ForwardingTransitionCoordinator coordinator;
    Mesh::RemainingHopLimit remaining = 4;

    assert(coordinator.Arm(nextHop, incarnation, 41, 100, 200) ==
           Mesh::ForwardingTransitionArmResult::Armed);
    assert(coordinator.HasPending());
    assert(remaining == 4U);

    // Radio/link evidence alone never enters this API and therefore cannot consume HopLimit.
    assert(remaining == 4U);

    // Only the exact authenticated next-hop identity/incarnation and MeshMessageId may commit the transition.
    assert(coordinator.AcceptAuthenticated(otherNode, incarnation, 41, 110, remaining) ==
           Mesh::ForwardingAcceptanceResult::WrongNextHop);
    assert(coordinator.AcceptAuthenticated(nextHop, laterIncarnation, 41, 110, remaining) ==
           Mesh::ForwardingAcceptanceResult::WrongIncarnation);
    assert(coordinator.AcceptAuthenticated(nextHop, incarnation, 42, 110, remaining) ==
           Mesh::ForwardingAcceptanceResult::WrongMessage);
    assert(remaining == 4U);
    assert(coordinator.HasPending());

    assert(coordinator.AcceptAuthenticated(nextHop, incarnation, 41, 110, remaining) ==
           Mesh::ForwardingAcceptanceResult::Committed);
    assert(remaining == 3U);
    assert(!coordinator.HasPending());
    assert(coordinator.AcceptAuthenticated(nextHop, incarnation, 41, 111, remaining) ==
           Mesh::ForwardingAcceptanceResult::NotPending);
    assert(remaining == 3U);

    // Deadline expiry releases the pending transition and never consumes hop budget.
    assert(coordinator.Arm(nextHop, incarnation, 42, 120, 130) ==
           Mesh::ForwardingTransitionArmResult::Armed);
    assert(coordinator.AcceptAuthenticated(nextHop, incarnation, 42, 130, remaining) ==
           Mesh::ForwardingAcceptanceResult::DeadlineExpired);
    assert(!coordinator.HasPending());
    assert(remaining == 3U);

    // A zero HopLimit cannot be committed even with otherwise valid authenticated acceptance.
    remaining = 0;
    assert(coordinator.Arm(nextHop, incarnation, 43, 140, 200) ==
           Mesh::ForwardingTransitionArmResult::Armed);
    assert(coordinator.AcceptAuthenticated(nextHop, incarnation, 43, 150, remaining) ==
           Mesh::ForwardingAcceptanceResult::HopLimitExhausted);
    assert(!coordinator.HasPending());
    assert(remaining == 0U);

    // Cancellation is explicit and idempotent.
    remaining = 2;
    assert(coordinator.Arm(nextHop, incarnation, 44, 160, 220) ==
           Mesh::ForwardingTransitionArmResult::Armed);
    assert(coordinator.Arm(nextHop, incarnation, 45, 161, 220) ==
           Mesh::ForwardingTransitionArmResult::AlreadyPending);
    assert(coordinator.Cancel());
    assert(!coordinator.Cancel());
    assert(remaining == 2U);

    return 0;
}
