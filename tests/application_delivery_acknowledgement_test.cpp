#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationDeliveryAcknowledgementCoordinator.hpp>
#include <ESPressio_DeliveryAcknowledgementTracker.hpp>
#include <ESPressio_RouteAttemptPolicy.hpp>

using namespace ESPressio::Mesh;

namespace {
ESPressio::System::DeviceIdentifier Device(std::uint8_t value) {
    ESPressio::System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return ESPressio::System::DeviceIdentifier(bytes);
}
MembershipIncarnation Incarnation(std::uint8_t value) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return MembershipIncarnation(bytes);
}
}

int main() {
    ApplicationTransmissionTable<> transmissions;
    DefaultMeshTrafficGovernor traffic;
    ApplicationTransmissionCoordinator<> aggregate(transmissions, traffic);
    ApplicationRecipientLifecycleCoordinator<4> recipients(aggregate);
    ApplicationDeliveryAcknowledgementCoordinator<4> applicationAcks(recipients);

    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    DeliveryAcknowledgementTracker<4> acknowledgementTracker;
    DeliveryAcknowledgementCoordinator<4> acknowledgements(acknowledgementTracker);

    const std::uint8_t bytes[] = {7, 8, 9};
    const auto payload = ApplicationPayload::Borrowed(bytes, sizeof(bytes));

    // A valid authenticated destination ACK commits Delivered in the aggregate and retires the exact delivery lifecycle.
    ApplicationTransmissionRecipient firstRecipient[] = {{Device(1), Incarnation(11), 101}};
    ApplicationTransmissionHandle firstHandle{};
    assert(aggregate.Begin(firstRecipient, 1, payload, 100, 1000, firstHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator firstAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> firstDelivery(firstAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(firstHandle, 0, 100, true, firstDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(firstDelivery.IsActive() && firstDelivery.AwaitingDestinationAcknowledgement());

    assert(applicationAcks.ApplyAuthenticated(firstHandle, firstDelivery, Device(1), Incarnation(11), 101, 150) ==
        ApplicationDeliveryAcknowledgementResult::Delivered);
    assert(!firstDelivery.IsActive());
    ApplicationRecipientOutcome outcome{};
    assert(aggregate.TryGetRecipientOutcome(firstHandle, 101, outcome));
    assert(outcome == ApplicationRecipientOutcome::Delivered);
    assert(transmissions.IsTerminal(firstHandle));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(aggregate.Release(firstHandle));

    // An ACK from the wrong authenticated source is unrelated and cannot terminalize or retire the recipient.
    ApplicationTransmissionRecipient secondRecipient[] = {{Device(2), Incarnation(12), 202}};
    ApplicationTransmissionHandle secondHandle{};
    assert(aggregate.Begin(secondRecipient, 1, payload, 100, 1000, secondHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator secondAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> secondDelivery(secondAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(secondHandle, 0, 100, true, secondDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(applicationAcks.ApplyAuthenticated(secondHandle, secondDelivery, Device(9), Incarnation(12), 202, 150) ==
        ApplicationDeliveryAcknowledgementResult::Unrelated);
    assert(secondDelivery.IsActive() && secondDelivery.AwaitingDestinationAcknowledgement());
    assert(aggregate.TryGetRecipientOutcome(secondHandle, 202, outcome));
    assert(outcome == ApplicationRecipientOutcome::Pending);

    assert(applicationAcks.ApplyAuthenticated(secondHandle, secondDelivery, Device(2), Incarnation(12), 202, 160) ==
        ApplicationDeliveryAcknowledgementResult::Delivered);
    assert(aggregate.Release(secondHandle));

    // If ACK processing discovers the immutable deadline has expired, DeadlineExpired is committed and delivery state retires.
    ApplicationTransmissionRecipient expiringRecipient[] = {{Device(3), Incarnation(13), 303}};
    ApplicationTransmissionHandle expiringHandle{};
    assert(aggregate.Begin(expiringRecipient, 1, payload, 100, 200, expiringHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator expiringAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> expiringDelivery(expiringAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(expiringHandle, 0, 100, true, expiringDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(applicationAcks.ApplyAuthenticated(expiringHandle, expiringDelivery, Device(3), Incarnation(13), 303, 200) ==
        ApplicationDeliveryAcknowledgementResult::DeadlineExpired);
    assert(!expiringDelivery.IsActive());
    assert(aggregate.TryGetRecipientOutcome(expiringHandle, 303, outcome));
    assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(aggregate.Release(expiringHandle));

    // If aggregate deadline sweeping wins immediately before a late valid ACK, the aggregate outcome remains authoritative
    // while the exact stale per-delivery lifecycle is still retired.
    ApplicationTransmissionRecipient racedRecipient[] = {{Device(4), Incarnation(14), 404}};
    ApplicationTransmissionHandle racedHandle{};
    assert(aggregate.Begin(racedRecipient, 1, payload, 100, 200, racedHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator racedAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> racedDelivery(racedAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(racedHandle, 0, 100, true, racedDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(aggregate.ExpireDue(200) == 1U);
    assert(racedDelivery.IsActive());
    assert(applicationAcks.ApplyAuthenticated(racedHandle, racedDelivery, Device(4), Incarnation(14), 404, 199) ==
        ApplicationDeliveryAcknowledgementResult::AlreadyTerminal);
    assert(!racedDelivery.IsActive());
    assert(aggregate.TryGetRecipientOutcome(racedHandle, 404, outcome));
    assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(aggregate.Release(racedHandle));

    return 0;
}
