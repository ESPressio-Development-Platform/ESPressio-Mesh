#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationRadioSubmissionCoordinator.hpp>
#include <ESPressio_DeferredLogicalTransferTracker.hpp>

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

class DeferredRadio final : public Radio::IRadio {
    Radio::RadioObserverSubscriptions _observers{};
    bool _started{false};
    Radio::RadioAddress _local{Radio::RadioAddress::FromBytes(reinterpret_cast<const std::uint8_t*>("L"), 1)};
public:
    std::size_t SendCount{0};
    Radio::RadioTransmissionHandle NextTransmission{71};
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override { return {Radio::RadioCapability::None, 64, 1, 512}; }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        ++SendCount;
        return Radio::RadioSendResult::Accepted({}, NextTransmission);
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
};
}

int main() {
    constexpr ESPressio::Mesh::ApplicationPrimitiveDescriptor primitive{
        ESPressio::Primitive::FamilyIds::Event, 1
    };
    const auto local = Device(1);
    const auto remote = Device(2);
    const auto incarnation = Incarnation(9);
    const std::uint8_t bytes[]{1, 2, 3};
    const auto payload = Mesh::ApplicationPayload::Borrowed(bytes, sizeof(bytes));

    Mesh::AuthenticatedMembershipTable<2> memberships;
    assert(memberships.UpsertAuthenticated(remote, incarnation, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) ==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Radio::DeferredLogicalTransferTracker<4> radioTracker;
    Mesh::ForwardingRadioTerminalCorrelation<4> correlation;
    Radio::RadioTransport transport{radioTracker, correlation};
    DeferredRadio radio;
    assert(transport.AddInterface(radio));
    assert(transport.Start());
    const std::uint8_t addressByte = 7;
    const auto address = Radio::RadioAddress::FromBytes(&addressByte, 1);
    Radio::RadioPeerHandle peer{};
    assert(transport.Peers().Observe(radio, address, peer) == Radio::RadioPeerObserveResult::Observed);

    Mesh::AuthenticatedDirectPeerBindingTable<2> bindings;
    assert(bindings.Bind({remote, incarnation, 1, peer}) == Mesh::DirectPeerBindingResult::Bound);
    Mesh::TopologyLinkIdentity hop{local, 1, remote, 1};
    Mesh::ResolvedRoute<2> route;
    assert(route.Assign(local, remote, &hop, 1));

    Mesh::ApplicationTransmissionTable<4, 2> transmissions;
    Mesh::DefaultMeshTrafficGovernor traffic;
    Mesh::ApplicationTransmissionCoordinator<4, 2> aggregate(transmissions, traffic);
    Mesh::ApplicationRecipientLifecycleCoordinator<2, 4, 2> recipients(aggregate);
    Mesh::ApplicationRadioSubmissionCoordinator<2, 4, 4, 2, 2, 2, 2> applicationSubmit(recipients);
    Mesh::DefaultRouteAttemptPolicy routePolicy;
    Mesh::DefaultRetryPolicy retryPolicy;
    Mesh::DeliveryAcknowledgementTracker<2> acknowledgementTracker;
    Mesh::DeliveryAcknowledgementCoordinator<2> acknowledgements(acknowledgementTracker);
    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> forwarding(memberships, bindings, transport);

    // Unknown aggregate authority is rejected before any Radio submission or correlation mutation.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 101}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 100, 500, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 100, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(101));

        Mesh::ApplicationTransmissionHandle stale{handle.Slot, static_cast<std::uint16_t>(handle.Generation + 1U)};
        if (stale.Generation == 0U) stale.Generation = 1U;
        const auto before = radio.SendCount;
        auto result = applicationSubmit.Submit(stale, radioDelivery, local, route, 3, bytes, sizeof(bytes), 102);
        assert(result.Disposition == Mesh::ApplicationRadioSubmissionDisposition::UnknownTransmission);
        assert(radio.SendCount == before);
        assert(!radioDelivery.HasPendingRadioTerminalCorrelation());

        radio.NextTransmission = {71};
        result = applicationSubmit.Submit(handle, radioDelivery, local, route, 3, bytes, sizeof(bytes), 103);
        assert(result.Disposition == Mesh::ApplicationRadioSubmissionDisposition::AwaitingNextHopAcceptance);
        assert(radio.SendCount == before + 1U);
        assert(radioDelivery.HasPendingRadioTerminalCorrelation());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 101, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::Pending);

        // Test cleanup must use the same aggregate-first retirement ordering as production code.
        assert(recipients.TerminalizeComposed(
                   handle,
                   101,
                   Mesh::ApplicationRecipientOutcome::PermanentFailure,
                   radioDelivery) == Mesh::ApplicationRecipientTerminalizationResult::Terminalized);
        assert(!radioDelivery.IsActive());
        assert(aggregate.Release(handle));
    }

    // A synchronous deadline stop is committed to the aggregate and retires exact delivery state without reaching Radio.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 202}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 200, 250, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 200, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(201));
        const auto before = radio.SendCount;
        const auto result = applicationSubmit.Submit(handle, radioDelivery, local, route, 3, bytes, sizeof(bytes), 250);
        assert(result.Disposition == Mesh::ApplicationRadioSubmissionDisposition::DeadlineExpired);
        assert(radio.SendCount == before);
        assert(!radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 202, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::DeadlineExpired);
        assert(aggregate.Release(handle));
    }

    // Hop-budget exhaustion is a definitive forwarding failure and is reconciled as aggregate PermanentFailure.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 303}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 300, 600, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 300, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(301));
        const auto before = radio.SendCount;
        const auto result = applicationSubmit.Submit(handle, radioDelivery, local, route, 0, bytes, sizeof(bytes), 302);
        assert(result.Disposition == Mesh::ApplicationRadioSubmissionDisposition::PermanentFailure);
        assert(radio.SendCount == before);
        assert(!radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 303, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::PermanentFailure);
        assert(aggregate.Release(handle));
    }

    // Loss of the executable direct-peer binding is route-unavailable evidence: replan remains non-terminal.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 404}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 400, 800, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 400, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(401));
        assert(transport.InvalidatePeer(peer));
        const auto result = applicationSubmit.Submit(handle, radioDelivery, local, route, 3, bytes, sizeof(bytes), 402);
        assert(result.Disposition == Mesh::ApplicationRadioSubmissionDisposition::ReplanDistinctRoute);
        assert(radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 404, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::Pending);

        assert(recipients.TerminalizeComposed(
                   handle,
                   404,
                   Mesh::ApplicationRecipientOutcome::PermanentFailure,
                   radioDelivery) == Mesh::ApplicationRecipientTerminalizationResult::Terminalized);
        assert(!radioDelivery.IsActive());
        assert(aggregate.Release(handle));
    }

    transport.Stop();
    return 0;
}
