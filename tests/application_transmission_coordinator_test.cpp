#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationTransmissionCoordinator.hpp>
#include <ESPressio_DeliveryAcknowledgementTracker.hpp>
#include <ESPressio_RouteAttemptPolicy.hpp>

using namespace ESPressio::Mesh;

namespace {
ESPressio::System::DeviceIdentifier Device(std::uint8_t value) { ESPressio::System::DeviceIdentifier::Storage bytes{}; bytes[15] = value; return ESPressio::System::DeviceIdentifier(bytes); }
MembershipIncarnation Incarnation(std::uint8_t value) { MembershipIncarnation::Storage bytes{}; bytes[15] = value; return MembershipIncarnation(bytes); }
}

int main() {
    ApplicationTransmissionTable<> transmissions; DefaultMeshTrafficGovernor traffic; ApplicationTransmissionCoordinator<> coordinator(transmissions, traffic);
    ApplicationTransmissionRecipient recipients[] = {{Device(1), Incarnation(11), 101},{Device(2), Incarnation(12), 102}};
    const std::uint8_t bytes[] = {9,8,7}; const auto payload = ApplicationPayload::Borrowed(bytes, sizeof(bytes));

    ApplicationTransmissionHandle aggregate{};
    assert(coordinator.Begin(recipients, 2, payload, 100, 1000, aggregate) == ApplicationTransmissionAdmissionResult::Begun);
    assert(aggregate && traffic.Active(MeshTrafficClass::Application) == 1U);
    assert(coordinator.Payload(aggregate) != nullptr && coordinator.Payload(aggregate)->StableData() == bytes);

    DefaultRouteAttemptPolicy routePolicy; DefaultRetryPolicy retryPolicy; RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    DeliveryAcknowledgementTracker<4> tracker; DeliveryAcknowledgementCoordinator<4> acknowledgements(tracker);
    OutboundDeliveryLifecycle<4> firstDelivery(attempts, acknowledgements);
    assert(coordinator.BeginRecipient(aggregate, 0, 101, true, firstDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(firstDelivery.IsActive() && firstDelivery.MessageId() == 101U && firstDelivery.AbsoluteDeadlineMilliseconds() == 1000U);
    assert(coordinator.SetRecipientOutcome(aggregate, 101, ApplicationRecipientOutcome::Delivered) == ApplicationTransmissionUpdateResult::Updated);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U); firstDelivery.Reset();

    RouteAttemptCoordinator secondAttempts(routePolicy, retryPolicy); OutboundDeliveryLifecycle<4> secondDelivery(secondAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(aggregate, 1, 102, false, secondDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(coordinator.Payload(aggregate)->StableData() == bytes); // same backing for every recipient
    assert(coordinator.SetRecipientOutcome(aggregate, 102, ApplicationRecipientOutcome::PermanentFailure) == ApplicationTransmissionUpdateResult::Updated);
    assert(transmissions.IsTerminal(aggregate) && traffic.Active(MeshTrafficClass::Application) == 0U && transmissions.Contains(aggregate));
    assert(coordinator.Release(aggregate)); secondDelivery.Reset();

    ApplicationTransmissionHandle invalid{};
    assert(coordinator.Begin(nullptr, 0, payload, 100, 1000, invalid) == ApplicationTransmissionAdmissionResult::Invalid);
    assert(coordinator.Begin(recipients, 2, {}, 100, 1000, invalid) == ApplicationTransmissionAdmissionResult::Invalid);
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);

    ApplicationTransmissionHandle expiring{};
    assert(coordinator.Begin(recipients, 2, payload, 100, 200, expiring) == ApplicationTransmissionAdmissionResult::Begun);
    assert(!coordinator.Expire(expiring, 199)); assert(coordinator.Expire(expiring, 200));
    assert(transmissions.IsTerminal(expiring) && traffic.Active(MeshTrafficClass::Application) == 0U); assert(coordinator.Release(expiring));

    // Beginning a recipient at/after the immutable aggregate deadline terminalizes that recipient immediately. This avoids
    // retaining an impossible Pending recipient until a later maintenance sweep and releases traffic when it was the last.
    ApplicationTransmissionRecipient immediateRecipient[] = {{Device(5), Incarnation(15), 301}};
    ApplicationTransmissionHandle immediate{};
    assert(coordinator.Begin(immediateRecipient, 1, payload, 100, 250, immediate) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator immediateAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> immediateDelivery(immediateAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(immediate, 0, 250, false, immediateDelivery) == ApplicationRecipientBeginResult::DeadlineExpired);
    assert(!immediateDelivery.IsActive() && transmissions.IsTerminal(immediate));
    ApplicationTransmissionRecipient immediateInspected{}; ApplicationRecipientOutcome immediateOutcome{};
    assert(transmissions.TryGetRecipient(immediate, 0, immediateInspected, immediateOutcome));
    assert(immediateOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(coordinator.Release(immediate));

    // A bounded sweep enforces immutable deadlines across all accepted aggregates without silently releasing records.
    ApplicationTransmissionRecipient sweepA[] = {{Device(3), Incarnation(13), 201}};
    ApplicationTransmissionRecipient sweepB[] = {{Device(4), Incarnation(14), 202}};
    ApplicationTransmissionHandle early{}; ApplicationTransmissionHandle later{};
    assert(coordinator.Begin(sweepA, 1, payload, 100, 300, early) == ApplicationTransmissionAdmissionResult::Begun);
    assert(coordinator.Begin(sweepB, 1, payload, 100, 400, later) == ApplicationTransmissionAdmissionResult::Begun);
    assert(traffic.Active(MeshTrafficClass::Application) == 2U);
    assert(coordinator.ExpireDue(299) == 0U && traffic.Active(MeshTrafficClass::Application) == 2U);
    assert(coordinator.ExpireDue(300) == 1U && transmissions.IsTerminal(early) && !transmissions.IsTerminal(later));
    assert(traffic.Active(MeshTrafficClass::Application) == 1U && transmissions.Contains(early));
    ApplicationTransmissionRecipient inspected{}; ApplicationRecipientOutcome inspectedOutcome{};
    assert(transmissions.TryGetRecipient(early, 0, inspected, inspectedOutcome));
    assert(inspectedOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(coordinator.ExpireDue(450) == 1U && transmissions.IsTerminal(later));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(coordinator.ExpireDue(500) == 0U); // terminal aggregates are not reported repeatedly
    assert(coordinator.Release(early)); assert(coordinator.Release(later));

    MeshTrafficReservation held[Limits::ApplicationTransmissionCapacity]{};
    for (std::size_t i = 0; i < Limits::ApplicationTransmissionCapacity; ++i) assert(traffic.TryAcquire(MeshTrafficClass::Application, held[i]) == MeshTrafficAdmissionResult::Admitted);
    ApplicationTransmissionHandle blocked{};
    assert(coordinator.Begin(recipients, 2, payload, 100, 1000, blocked) == ApplicationTransmissionAdmissionResult::ResourceUnavailable);
    assert(!blocked && transmissions.Size() == 0U); for (auto reservation : held) assert(traffic.Release(reservation));
    return 0;
}
