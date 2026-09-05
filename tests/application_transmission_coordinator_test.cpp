#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationTransmissionCoordinator.hpp>
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
    ApplicationTransmissionCoordinator<> coordinator(transmissions, traffic);

    ApplicationTransmissionRecipient recipients[] = {
        {Device(1), Incarnation(11), 101},
        {Device(2), Incarnation(12), 102}
    };

    ApplicationTransmissionHandle aggregate{};
    assert(coordinator.Begin(recipients, 2, 100, 1000, aggregate) == ApplicationTransmissionAdmissionResult::Begun);
    assert(aggregate);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);

    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    DeliveryAcknowledgementTracker<4> tracker;
    DeliveryAcknowledgementCoordinator<4> acknowledgements(tracker);
    OutboundDeliveryLifecycle<4> firstDelivery(attempts, acknowledgements);

    assert(coordinator.BeginRecipient(aggregate, 0, 101, true, firstDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(firstDelivery.IsActive());
    assert(firstDelivery.MessageId() == 101U);
    assert(firstDelivery.AbsoluteDeadlineMilliseconds() == 1000U);
    assert(firstDelivery.AwaitingDestinationAcknowledgement());

    // Completing one recipient must not release the aggregate's one Application reservation.
    assert(coordinator.SetRecipientOutcome(aggregate, 101, ApplicationRecipientOutcome::Delivered) == ApplicationTransmissionUpdateResult::Updated);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);
    assert(coordinator.BeginRecipient(aggregate, 0, 102, false, firstDelivery) == ApplicationRecipientBeginResult::AlreadyTerminal);
    firstDelivery.Reset();

    RouteAttemptCoordinator secondAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> secondDelivery(secondAttempts, acknowledgements);
    assert(coordinator.BeginRecipient(aggregate, 1, 102, false, secondDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(secondDelivery.MessageId() == 102U);
    assert(!secondDelivery.AwaitingDestinationAcknowledgement());

    // Final recipient completion releases governance capacity while retaining queryable aggregate results until Release.
    assert(coordinator.SetRecipientOutcome(aggregate, 102, ApplicationRecipientOutcome::PermanentFailure) == ApplicationTransmissionUpdateResult::Updated);
    assert(transmissions.IsTerminal(aggregate));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(transmissions.Contains(aggregate));
    assert(coordinator.Release(aggregate));
    assert(!transmissions.Contains(aggregate));
    secondDelivery.Reset();

    // Invalid aggregate input must roll back the governor reservation.
    ApplicationTransmissionHandle invalid{};
    assert(coordinator.Begin(nullptr, 0, 100, 1000, invalid) == ApplicationTransmissionAdmissionResult::Invalid);
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);

    // Deadline expiry terminates only pending recipients and releases governance capacity.
    ApplicationTransmissionHandle expiring{};
    assert(coordinator.Begin(recipients, 2, 100, 200, expiring) == ApplicationTransmissionAdmissionResult::Begun);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);
    assert(!coordinator.Expire(expiring, 199));
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);
    assert(coordinator.Expire(expiring, 200));
    assert(transmissions.IsTerminal(expiring));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(coordinator.Release(expiring));

    // Saturating Application governance must reject before allocating an aggregate, and rollback remains deterministic.
    MeshTrafficReservation held[Limits::ApplicationTransmissionCapacity]{};
    for (std::size_t i = 0; i < Limits::ApplicationTransmissionCapacity; ++i) {
        assert(traffic.TryAcquire(MeshTrafficClass::Application, held[i]) == MeshTrafficAdmissionResult::Admitted);
    }
    ApplicationTransmissionHandle blocked{};
    assert(coordinator.Begin(recipients, 2, 100, 1000, blocked) == ApplicationTransmissionAdmissionResult::ResourceUnavailable);
    assert(!blocked);
    assert(transmissions.Size() == 0U);
    for (auto reservation : held) assert(traffic.Release(reservation));

    return 0;
}
