#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_OutboundDeliveryLifecycle.hpp>

using namespace ESPressio::Mesh;

static ESPressio::System::DeviceIdentifier Device(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return ESPressio::System::DeviceIdentifier{bytes};
}

static MembershipIncarnation Incarnation(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return MembershipIncarnation{bytes};
}

int main() {
    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    RouteAttemptCoordinator otherAttempts(routePolicy, retryPolicy);
    DeliveryAcknowledgementTracker<2> tracker;
    DeliveryAcknowledgementCoordinator<2> acknowledgements(tracker);
    OutboundDeliveryLifecycle<2> lifecycle(attempts, acknowledgements);
    OutboundDeliveryLifecycle<2> otherLifecycle(otherAttempts, acknowledgements);

    const auto destination = Device(9);
    const auto destinationIncarnation = Incarnation(9);
    const auto otherDestination = Device(8);
    const auto otherDestinationIncarnation = Incarnation(8);
    const auto nextHop = Device(2);
    const auto nextHopIncarnation = Incarnation(2);
    constexpr MeshMessageId messageId = 44;
    constexpr MeshMessageId otherMessageId = 144;

    assert(lifecycle.Begin(destination, destinationIncarnation, messageId, 100, 500, true) ==
           OutboundDeliveryBeginResult::Begun);
    assert(lifecycle.IsActive());
    assert(lifecycle.AwaitingDestinationAcknowledgement());
    assert(tracker.Size() == 1U);

    // Two active lifecycles may share one bounded ACK tracker. An ACK for the other lifecycle must not be
    // consumed through this lifecycle merely because it is otherwise valid in the shared tracker.
    assert(otherLifecycle.Begin(
               otherDestination,
               otherDestinationIncarnation,
               otherMessageId,
               100,
               500,
               true) == OutboundDeliveryBeginResult::Begun);
    assert(tracker.Size() == 2U);
    assert(lifecycle.ApplyDestinationAcknowledgementAuthenticated(
               otherDestination,
               otherDestinationIncarnation,
               otherMessageId,
               105) == OutboundDeliveryAcknowledgementAction::IgnoreUnrelatedAcknowledgement);
    assert(lifecycle.AwaitingDestinationAcknowledgement());
    assert(otherLifecycle.AwaitingDestinationAcknowledgement());
    assert(tracker.Size() == 2U);
    assert(otherLifecycle.ApplyDestinationAcknowledgementAuthenticated(
               otherDestination,
               otherDestinationIncarnation,
               otherMessageId,
               106) == OutboundDeliveryAcknowledgementAction::DeliveryConfirmed);
    assert(!otherLifecycle.AwaitingDestinationAcknowledgement());
    assert(lifecycle.AwaitingDestinationAcknowledgement());
    assert(tracker.Size() == 1U);
    otherLifecycle.Reset();

    assert(lifecycle.BeginDistinctRouteAttempt(110));

    ForwardingSubmissionResult accepted{};
    accepted.Disposition = ForwardingSubmissionDisposition::Accepted;
    accepted.RadioResult = {
        ESPressio::Radio::RadioTransportSendStatus::Accepted,
        ESPressio::Radio::RadioSendResult::Accepted(
            ESPressio::Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged()
        )
    };

    // Strongest Radio evidence still only arms exact next-hop Mesh acceptance.
    assert(lifecycle.AfterSubmission(accepted, nextHop, nextHopIncarnation, 120) ==
           OutboundForwardingAction::AwaitingNextHopAcceptance);
    assert(lifecycle.AwaitingNextHopAcceptance());

    RemainingHopLimit hops = 5;
    assert(lifecycle.AcceptNextHopAuthenticated(
               Device(3), nextHopIncarnation, messageId, 130, hops) ==
           ForwardingAcceptanceAction::IgnoreUnrelatedEvidence);
    assert(hops == 5U);

    assert(lifecycle.AcceptNextHopAuthenticated(
               nextHop, nextHopIncarnation, messageId, 140, hops) ==
           ForwardingAcceptanceAction::ForwardingComplete);
    assert(hops == 4U);
    assert(!lifecycle.AwaitingNextHopAcceptance());

    // Intermediate forwarding completion does not consume the final-destination ACK record.
    assert(lifecycle.AwaitingDestinationAcknowledgement());
    assert(tracker.Size() == 1U);

    assert(lifecycle.ApplyDestinationAcknowledgementAuthenticated(
               nextHop, nextHopIncarnation, messageId, 150) ==
           OutboundDeliveryAcknowledgementAction::IgnoreUnrelatedAcknowledgement);
    assert(tracker.Size() == 1U);

    assert(lifecycle.ApplyDestinationAcknowledgementAuthenticated(
               destination, destinationIncarnation, messageId, 160) ==
           OutboundDeliveryAcknowledgementAction::DeliveryConfirmed);
    assert(!lifecycle.AwaitingDestinationAcknowledgement());
    assert(tracker.Empty());

    lifecycle.Reset();
    assert(!lifecycle.IsActive());

    // Retryable submission remains governed by the existing route-attempt policy.
    assert(lifecycle.Begin(destination, destinationIncarnation, 45, 200, 600, false) ==
           OutboundDeliveryBeginResult::Begun);
    assert(lifecycle.BeginDistinctRouteAttempt(210));
    ForwardingSubmissionResult retryable{};
    retryable.Disposition = ForwardingSubmissionDisposition::RetryableFailure;
    assert(lifecycle.AfterSubmission(retryable, nextHop, nextHopIncarnation, 220) ==
           OutboundForwardingAction::RetryCurrentRoute);
    lifecycle.Reset();

    // Reset releases reserved end-to-end ACK capacity deterministically.
    assert(lifecycle.Begin(destination, destinationIncarnation, 46, 300, 700, true) ==
           OutboundDeliveryBeginResult::Begun);
    assert(tracker.Size() == 1U);
    lifecycle.Reset();
    assert(tracker.Empty());

    return 0;
}
