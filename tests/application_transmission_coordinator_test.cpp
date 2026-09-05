#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationRecipientLifecycleCoordinator.hpp>
#include <ESPressio_DeliveryAcknowledgementTracker.hpp>
#include <ESPressio_RouteAttemptPolicy.hpp>

using namespace ESPressio::Mesh;

namespace {
ESPressio::System::DeviceIdentifier Device(std::uint8_t value) { ESPressio::System::DeviceIdentifier::Storage bytes{}; bytes[15] = value; return ESPressio::System::DeviceIdentifier(bytes); }
MembershipIncarnation Incarnation(std::uint8_t value) { MembershipIncarnation::Storage bytes{}; bytes[15] = value; return MembershipIncarnation(bytes); }
}

int main() {
    constexpr ESPressio::Mesh::ApplicationPrimitiveDescriptor primitive{
        ESPressio::Primitive::FamilyIds::Event, 1
    };
    ApplicationTransmissionTable<> transmissions; DefaultMeshTrafficGovernor traffic; ApplicationTransmissionCoordinator<> coordinator(transmissions, traffic);
    ApplicationRecipientLifecycleCoordinator<4> recipientLifecycle(coordinator);
    ApplicationTransmissionRecipient recipients[] = {{Device(1), Incarnation(11), 101},{Device(2), Incarnation(12), 102}};
    const std::uint8_t bytes[] = {9,8,7}; const auto payload = ApplicationPayload::Borrowed(bytes, sizeof(bytes));

    DefaultRouteAttemptPolicy routePolicy; DefaultRetryPolicy retryPolicy;
    DeliveryAcknowledgementTracker<4> tracker; DeliveryAcknowledgementCoordinator<4> acknowledgements(tracker);

    ApplicationTransmissionHandle aggregate{};
    assert(coordinator.Begin(recipients, 2, primitive, payload, 100, 1000, aggregate) == ApplicationTransmissionAdmissionResult::Begun);
    assert(aggregate && traffic.Active(MeshTrafficClass::Application) == 1U);
    assert(coordinator.PrimitiveDescriptor(aggregate) != nullptr);
    assert(coordinator.PrimitiveDescriptor(aggregate)->Family == ESPressio::Primitive::FamilyIds::Event);
    assert(coordinator.Payload(aggregate) != nullptr && coordinator.Payload(aggregate)->StableData() == bytes);

    assert(!coordinator.Release(aggregate));
    assert(transmissions.Contains(aggregate));
    assert(coordinator.Payload(aggregate) != nullptr && coordinator.Payload(aggregate)->StableData() == bytes);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);

    RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> firstDelivery(attempts, acknowledgements);
    assert(coordinator.BeginRecipient(aggregate, 0, 101, true, firstDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(firstDelivery.IsActive() && firstDelivery.MessageId() == 101U && firstDelivery.AbsoluteDeadlineMilliseconds() == 1000U);
    assert(recipientLifecycle.Terminalize(aggregate, 101, ApplicationRecipientOutcome::Delivered, firstDelivery) == ApplicationRecipientTerminalizationResult::Terminalized);
    assert(!firstDelivery.IsActive());
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);

    RouteAttemptCoordinator secondAttempts(routePolicy, retryPolicy); OutboundDeliveryLifecycle<4> secondDelivery(secondAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(aggregate, 1, 102, false, secondDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(coordinator.Payload(aggregate)->StableData() == bytes);
    assert(recipientLifecycle.Terminalize(aggregate, 102, ApplicationRecipientOutcome::PermanentFailure, secondDelivery) == ApplicationRecipientTerminalizationResult::Terminalized);
    assert(!secondDelivery.IsActive());
    assert(transmissions.IsTerminal(aggregate) && traffic.Active(MeshTrafficClass::Application) == 0U && transmissions.Contains(aggregate));
    assert(coordinator.Release(aggregate));

    ApplicationTransmissionHandle invalid{};
    assert(coordinator.Begin(nullptr, 0, primitive, payload, 100, 1000, invalid) == ApplicationTransmissionAdmissionResult::Invalid);
    assert(coordinator.Begin(recipients, 2, primitive, {}, 100, 1000, invalid) == ApplicationTransmissionAdmissionResult::Invalid);
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);

    // Explicit single-aggregate expiry offers the same aggregate-first external-cleanup ordering as the bulk sweep.
    ApplicationTransmissionRecipient explicitRecipient[] = {{Device(6), Incarnation(16), 401}};
    ApplicationTransmissionHandle expiring{};
    assert(coordinator.Begin(explicitRecipient, 1, primitive, payload, 100, 200, expiring) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator explicitAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> explicitDelivery(explicitAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(expiring, 0, 110, true, explicitDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(explicitDelivery.IsActive() && explicitDelivery.AwaitingDestinationAcknowledgement());
    assert(tracker.Size() == 1U && traffic.Active(MeshTrafficClass::Application) == 1U);

    std::size_t explicitCallbacks = 0U;
    assert(!coordinator.ExpireWithRecipients(expiring, 199, [&](ApplicationTransmissionHandle, MeshMessageId) noexcept {
        assert(false && "explicit expiry callback must not run before the immutable deadline");
    }));
    assert(explicitDelivery.IsActive() && tracker.Size() == 1U);
    assert(coordinator.ExpireWithRecipients(expiring, 200, [&](ApplicationTransmissionHandle handle, MeshMessageId messageId) noexcept {
        ++explicitCallbacks;
        assert(handle == expiring && messageId == 401U);
        ApplicationRecipientOutcome outcome{};
        assert(coordinator.TryGetRecipientOutcome(handle, messageId, outcome));
        assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);
        // Traffic release occurs only after this exact external ACK-bearing lifecycle is retired.
        assert(traffic.Active(MeshTrafficClass::Application) == 1U);
        explicitDelivery.Reset();
    }));
    assert(explicitCallbacks == 1U);
    assert(!explicitDelivery.IsActive() && tracker.Empty());
    assert(transmissions.IsTerminal(expiring) && traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(!coordinator.ExpireWithRecipients(expiring, 201, [&](ApplicationTransmissionHandle, MeshMessageId) noexcept {
        assert(false && "already-terminal explicit recipient must not be reported twice");
    }));
    assert(coordinator.Release(expiring));

    ApplicationTransmissionRecipient immediateRecipient[] = {{Device(5), Incarnation(15), 301}};
    ApplicationTransmissionHandle immediate{};
    assert(coordinator.Begin(immediateRecipient, 1, primitive, payload, 100, 250, immediate) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator immediateAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> immediateDelivery(immediateAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(immediate, 0, 250, false, immediateDelivery) == ApplicationRecipientBeginResult::DeadlineExpired);
    assert(!immediateDelivery.IsActive() && transmissions.IsTerminal(immediate));
    ApplicationTransmissionRecipient immediateInspected{}; ApplicationRecipientOutcome immediateOutcome{};
    assert(transmissions.TryGetRecipient(immediate, 0, immediateInspected, immediateOutcome));
    assert(immediateOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(coordinator.Release(immediate));

    ApplicationTransmissionRecipient sweepA[] = {{Device(3), Incarnation(13), 201}};
    ApplicationTransmissionRecipient sweepB[] = {{Device(4), Incarnation(14), 202}};
    ApplicationTransmissionHandle early{}; ApplicationTransmissionHandle later{};
    assert(coordinator.Begin(sweepA, 1, primitive, payload, 100, 300, early) == ApplicationTransmissionAdmissionResult::Begun);
    assert(coordinator.Begin(sweepB, 1, primitive, payload, 100, 400, later) == ApplicationTransmissionAdmissionResult::Begun);
    assert(traffic.Active(MeshTrafficClass::Application) == 2U);

    RouteAttemptCoordinator sweepAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> sweepDelivery(sweepAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(early, 0, 110, true, sweepDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(sweepDelivery.IsActive());
    assert(sweepDelivery.AwaitingDestinationAcknowledgement());
    assert(tracker.Size() == 1U);

    assert(coordinator.ExpireDueWithRecipients(299, [&](ApplicationTransmissionHandle, MeshMessageId) noexcept {
        assert(false && "recipient callback must not run before the deadline");
    }) == 0U);
    assert(traffic.Active(MeshTrafficClass::Application) == 2U);
    assert(sweepDelivery.IsActive() && tracker.Size() == 1U);

    std::size_t expiredRecipientCallbacks = 0U;
    assert(coordinator.ExpireDueWithRecipients(300, [&](ApplicationTransmissionHandle handle, MeshMessageId messageId) noexcept {
        ++expiredRecipientCallbacks;
        assert(handle == early);
        assert(messageId == 201U);
        ApplicationRecipientOutcome outcome{};
        assert(coordinator.TryGetRecipientOutcome(handle, messageId, outcome));
        assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);
        assert(sweepDelivery.IsActive() && sweepDelivery.MessageId() == messageId);
        sweepDelivery.Reset();
    }) == 1U);
    assert(expiredRecipientCallbacks == 1U);
    assert(transmissions.IsTerminal(early) && !transmissions.IsTerminal(later));
    assert(!sweepDelivery.IsActive() && tracker.Empty());
    assert(traffic.Active(MeshTrafficClass::Application) == 1U && transmissions.Contains(early));

    ApplicationTransmissionRecipient inspected{}; ApplicationRecipientOutcome inspectedOutcome{};
    assert(transmissions.TryGetRecipient(early, 0, inspected, inspectedOutcome));
    assert(inspectedOutcome == ApplicationRecipientOutcome::DeadlineExpired);

    assert(coordinator.ExpireDueWithRecipients(450, [&](ApplicationTransmissionHandle handle, MeshMessageId messageId) noexcept {
        ++expiredRecipientCallbacks;
        assert(handle == later);
        assert(messageId == 202U);
        ApplicationRecipientOutcome outcome{};
        assert(coordinator.TryGetRecipientOutcome(handle, messageId, outcome));
        assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);
    }) == 1U);
    assert(expiredRecipientCallbacks == 2U && transmissions.IsTerminal(later));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(coordinator.ExpireDueWithRecipients(500, [&](ApplicationTransmissionHandle, MeshMessageId) noexcept {
        assert(false && "already-terminal recipients must not be reported twice");
    }) == 0U);
    assert(coordinator.Release(early)); assert(coordinator.Release(later));

    MeshTrafficReservation held[Limits::ApplicationTransmissionCapacity]{};
    for (std::size_t i = 0; i < Limits::ApplicationTransmissionCapacity; ++i) assert(traffic.TryAcquire(MeshTrafficClass::Application, held[i]) == MeshTrafficAdmissionResult::Admitted);
    ApplicationTransmissionHandle blocked{};
    assert(coordinator.Begin(recipients, 2, primitive, payload, 100, 1000, blocked) == ApplicationTransmissionAdmissionResult::ResourceUnavailable);
    assert(!blocked && transmissions.Size() == 0U); for (auto reservation : held) assert(traffic.Release(reservation));
    return 0;
}
