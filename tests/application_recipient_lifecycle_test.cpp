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
    constexpr ESPressio::Mesh::ApplicationPrimitiveDescriptor primitive{
        ESPressio::Primitive::FamilyIds::Event, 1
    };
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
    assert(aggregate.Begin(recipients, 2, primitive, payload, 100, 1000, handle) == ApplicationTransmissionAdmissionResult::Begun);
    assert(traffic.Active(MeshTrafficClass::Application) == 1U);

    RouteAttemptCoordinator firstAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> first(firstAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(handle, 0, 101, true, first) == ApplicationRecipientBeginResult::Begun);
    assert(first.IsActive() && first.AwaitingDestinationAcknowledgement());

    RouteAttemptCoordinator unrelatedAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> unrelated(unrelatedAttempts, acknowledgements);
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
    assert(aggregate.Begin(lone, 1, primitive, payload, 100, 1000, other) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator otherAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> otherDelivery(otherAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(other, 0, 201, false, otherDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(lifecycle.Terminalize({}, 201, ApplicationRecipientOutcome::PermanentFailure, otherDelivery) ==
        ApplicationRecipientTerminalizationResult::UnknownTransmission);
    assert(otherDelivery.IsActive());
    assert(lifecycle.Terminalize(other, 201, ApplicationRecipientOutcome::DeadlineExpired, otherDelivery) ==
        ApplicationRecipientTerminalizationResult::Terminalized);
    assert(!otherDelivery.IsActive() && transmissions.IsTerminal(other));
    assert(aggregate.Release(other));

    ApplicationTransmissionRecipient expiring[] = {{Device(4), Incarnation(14), 301}};
    ApplicationTransmissionHandle expiringHandle{};
    assert(aggregate.Begin(expiring, 1, primitive, payload, 100, 200, expiringHandle) == ApplicationTransmissionAdmissionResult::Begun);
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

    ApplicationTransmissionRecipient raced[] = {{Device(5), Incarnation(15), 401}};
    ApplicationTransmissionHandle racedHandle{};
    assert(aggregate.Begin(raced, 1, primitive, payload, 100, 200, racedHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator racedAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> racedDelivery(racedAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(racedHandle, 0, 101, true, racedDelivery) == ApplicationRecipientBeginResult::Begun);
    assert(racedDelivery.IsActive() && racedDelivery.AwaitingDestinationAcknowledgement());
    assert(aggregate.ExpireDue(200) == 1U);
    assert(racedDelivery.IsActive());
    assert(lifecycle.Terminalize(racedHandle, 401, ApplicationRecipientOutcome::Delivered, racedDelivery) ==
        ApplicationRecipientTerminalizationResult::AlreadyTerminal);
    assert(!racedDelivery.IsActive());
    assert(aggregate.TryGetRecipientOutcome(racedHandle, 401, expiredOutcome));
    assert(expiredOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(aggregate.Release(racedHandle));

    // Composed sweeping retires matching external state synchronously so bounded ACK/forwarding resources are not
    // retained merely because no later delivery callback happens to touch an expired aggregate recipient.
    ApplicationTransmissionRecipient sweptRecipients[] = {
        {Device(6), Incarnation(16), 501},
        {Device(7), Incarnation(17), 502}
    };
    ApplicationTransmissionHandle sweptHandle{};
    assert(aggregate.Begin(sweptRecipients, 2, primitive, payload, 100, 300, sweptHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator sweptFirstAttempts(routePolicy, retryPolicy);
    RouteAttemptCoordinator sweptSecondAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> sweptFirst(sweptFirstAttempts, acknowledgements);
    OutboundDeliveryLifecycle<4> sweptSecond(sweptSecondAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(sweptHandle, 0, 101, true, sweptFirst) == ApplicationRecipientBeginResult::Begun);
    assert(aggregate.BeginRecipient(sweptHandle, 1, 101, true, sweptSecond) == ApplicationRecipientBeginResult::Begun);
    assert(sweptFirst.IsActive() && sweptSecond.IsActive());

    const auto sweep = lifecycle.ExpireDueAndRetire(
        300,
        [&](ApplicationTransmissionHandle candidate, MeshMessageId messageId) noexcept -> OutboundDeliveryLifecycle<4>* {
            if (!(candidate == sweptHandle)) return nullptr;
            if (messageId == 501) return &sweptFirst;
            if (messageId == 502) return &sweptSecond;
            return nullptr;
        }
    );
    assert(sweep.ExpiredTransmissions == 1U);
    assert(sweep.ExpiredRecipients == 2U);
    assert(sweep.RetiredExternalLifecycles == 2U);
    assert(sweep.ExternalLifecycleMismatches == 0U);
    assert(!sweptFirst.IsActive() && !sweptSecond.IsActive());
    assert(transmissions.IsTerminal(sweptHandle));
    assert(traffic.Active(MeshTrafficClass::Application) == 0U);
    assert(aggregate.TryGetRecipientOutcome(sweptHandle, 501, expiredOutcome));
    assert(expiredOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(aggregate.TryGetRecipientOutcome(sweptHandle, 502, expiredOutcome));
    assert(expiredOutcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(aggregate.Release(sweptHandle));

    // A bad resolver can never reset another recipient. The mismatch is surfaced instead of silently cancelling state.
    ApplicationTransmissionRecipient mismatchRecipient[] = {{Device(8), Incarnation(18), 601}};
    ApplicationTransmissionHandle mismatchHandle{};
    assert(aggregate.Begin(mismatchRecipient, 1, primitive, payload, 100, 300, mismatchHandle) == ApplicationTransmissionAdmissionResult::Begun);
    RouteAttemptCoordinator mismatchAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> mismatchDelivery(mismatchAttempts, acknowledgements);
    assert(aggregate.BeginRecipient(mismatchHandle, 0, 101, true, mismatchDelivery) == ApplicationRecipientBeginResult::Begun);

    RouteAttemptCoordinator foreignAttempts(routePolicy, retryPolicy);
    OutboundDeliveryLifecycle<4> foreignDelivery(foreignAttempts, acknowledgements);
    assert(foreignDelivery.Begin(Device(9), Incarnation(19), 999, 101, 1000, true) == OutboundDeliveryBeginResult::Begun);
    const auto mismatchSweep = lifecycle.ExpireDueAndRetire(
        300,
        [&](ApplicationTransmissionHandle, MeshMessageId) noexcept -> OutboundDeliveryLifecycle<4>* { return &foreignDelivery; }
    );
    assert(mismatchSweep.ExpiredTransmissions == 1U);
    assert(mismatchSweep.ExpiredRecipients == 1U);
    assert(mismatchSweep.RetiredExternalLifecycles == 0U);
    assert(mismatchSweep.ExternalLifecycleMismatches == 1U);
    assert(mismatchDelivery.IsActive());
    assert(foreignDelivery.IsActive());
    mismatchDelivery.Reset();
    foreignDelivery.Reset();
    assert(aggregate.Release(mismatchHandle));

    return 0;
}
