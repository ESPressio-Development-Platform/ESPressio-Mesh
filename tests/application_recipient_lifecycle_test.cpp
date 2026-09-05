#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationRecipientLifecycleCoordinator.hpp>
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
    ApplicationRecipientLifecycleCoordinator<4> lifecycle(aggregate);

    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    DeliveryAcknowledgementTracker<4> acknowledgementTracker;
    DeliveryAcknowledgementCoordinator<4> acknowledgements(acknowledgementTracker);

    const std::uint8_t bytes[] = {1, 2, 3, 4};
    const auto payload = ApplicationPayload::Borrowed(bytes, sizeof(bytes));
    ApplicationTransmissionRecipient recipients[] = {
        {Device(1), Incarnation(11), 101},
        {Device(2), Incarnation(12), 102}
    };

    ApplicationTransmissionHandle handle{};
    assert(aggregate.Begin(recipients, 2, payload, 100, 1000, handle) == ApplicationTransmissionAdmissionResult::Begun);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);

    RouteAttemptCoordinator firstAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> first(firstAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(handle, 0, 101, true, first) == ApplicationRecipientBeginResult::Begun);
    assert(first.IsActive() && first.AwaitingDestinationAcknowledgement());

    RouteAttemptCoordinator unrelatedAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> unrelated(unrelatedAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(handle, 1, 101, false, unrelated) == ApplicationRecipientBeginResult::Invalid);
    assert(lifecycle.Terminalize(handle, 101, ApplicationRecipientOutcome::Delivered, unrelated) ==
        ApplicationRecipientTerminalizationResult::Invalid);
    assert(first.IsActive() && transmissions.Contains(handle));

    assert(lifecycle.Terminalize(handle, 101, ApplicationRecipientOutcome::Delivered, first) ==
        ApplicationRecipientTerminalizationResult::Terminalized);
    assert(!first.IsActive());
    assert(transmissions.Contains(handle) && !transmissions.IsTerminal(handle));
    assert(aggregate.Payload(handle) != nullptr && aggregate.Payload(handle)->StableData() == bytes);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);

    RouteAttemptCoordinator secondAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> second(secondAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(handle, 1, 102, false, second) == ApplicationRecipientBeginResult::Begun);
    assert(lifecycle.Terminalize(handle, 102, ApplicationRecipientOutcome::PermanentFailure, second) ==
        ApplicationRecipientTerminalizationResult::Terminalized);
    assert(!second.IsActive());
    assert(transmissions.IsTerminal(handle));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(aggregate.Payload(handle) != nullptr && aggregate.Payload(handle)->StableData() == bytes);
    assert(aggregate.Release(handle));

    ApplicationTransmissionRecipient lone[] = {{Device(3), Incarnation(13), 201}};
    ApplicationTransmissionHandle other{};
    assert(aggregate.Begin(lone, 1, payload, 100, 1000, other) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator otherAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> otherDelivery(otherAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(other, 0, 101, false, otherDelivery) == ApplicationRecipientBeginResult::Invalid);
    assert(aggregate.BeginRecipient(other, 0, 201, false, otherDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(lifecycle.Terminalize({}, 201, ApplicationRecipientOutcome::PermanentFailure, otherDelivery) ==
        ApplicationRecipientTerminalizationResult::UnknownTransmission);
    assert(otherDelivery.IsActive());
    assert(lifecycle.Terminalize(other, 201, ApplicationRecipientOutcome::DeadlineExpired, otherDelivery) ==
        ApplicationRecipientTerminalizationResult::Terminalized);
    assert(!otherDelivery.IsActive() && transmissions.IsTerminal(other));
    assert(aggregate.Release(other));

    // Deadline sweeping terminalizes aggregate state independently; active external delivery state is then retired
    // explicitly, preserving the aggregate outcome and never treating retirement as cancellation.
    ApplicationTransmissionRecipient expiring[] = {{Device(4), Incarnation(14), 301}};
    ApplicationTransmissionHandle expiringHandle{};
    assert(aggregate.Begin(expiring, 1, payload, 100, 200, expiringHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator expiringAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> expiringDelivery(expiringAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(expiringHandle, 0, 101, false, expiringDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(lifecycle.RetireTerminal(expiringHandle, 301, expiringDelivery) == ApplicationRecipientRetirementResult::NotTerminal);
    assert(expiringDelivery.IsActive());
    assert(aggregate.ExpireDue(200) == 1U);
    assert(transmissions.IsTerminal(expiringHandle));
    assert(expiringDelivery.IsActive());

    ApplicationRecipientOutcome expiredOutcome{};
    assert(aggregate.TryGetRecipientOutcome(expiringHandle, 301, expiredOutcome));
    assert(expiredOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(lifecycle.RetireTerminal(expiringHandle, 301, expiringDelivery) == ApplicationRecipientRetirementResult::Retired);
    assert(!expiringDelivery.IsActive());
    assert(aggregate.TryGetRecipientOutcome(expiringHandle, 301, expiredOutcome));
    assert(expiredOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(aggregate.Release(expiringHandle));

    return 0;
}
