#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationRadioTerminalCoordinator.hpp>
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
    Radio::RadioTransmissionHandle NextTransmission{61};
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::None, 64, 1, 512};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        return Radio::RadioSendResult::Accepted({}, NextTransmission);
    }
    void Resolve(const Radio::RadioAddress& destination, const Radio::RadioDirectLinkEvidence& evidence) {
        _observers.NotifyTransmissionResolved(*this, NextTransmission, destination, 0, evidence);
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
};

class NoSameRouteRetry final : public Mesh::IRouteAttemptPolicy {
public:
    bool ShouldRetryCurrentRoute(const Mesh::RouteAttemptEvidence&) const noexcept override { return false; }
};

class NoDistinctRouteRetry final : public Mesh::IRetryPolicy {
public:
    bool ShouldTryAnotherRoute(const Mesh::RouteAttemptEvidence&) const noexcept override { return false; }
};
}

int main() {
    constexpr ESPressio::Mesh::ApplicationPrimitiveDescriptor primitive{
        ESPressio::Primitive::FamilyIds::Event, 1
    };
    const auto local = Device(1);
    const auto remote = Device(2);
    const auto incarnation = Incarnation(9);
    const std::uint8_t payloadBytes[]{1, 2, 3};
    const auto payload = Mesh::ApplicationPayload::Borrowed(payloadBytes, sizeof(payloadBytes));

    Mesh::AuthenticatedMembershipTable<2> memberships;
    assert(memberships.UpsertAuthenticated(
        remote,
        incarnation,
        Mesh::MembershipState::Active,
        Mesh::ReachabilityState::Reachable
    ) == Mesh::AuthenticatedMembershipInsertResult::Inserted);

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
    Mesh::ApplicationRadioTerminalCoordinator<2, 4, 4, 2, 2, 2, 2> applicationRadio(recipients);
    Mesh::DefaultRouteAttemptPolicy defaultRoutePolicy;
    Mesh::DefaultRetryPolicy defaultRetryPolicy;
    Mesh::DeliveryAcknowledgementTracker<2> acknowledgementTracker;
    Mesh::DeliveryAcknowledgementCoordinator<2> acknowledgements(acknowledgementTracker);
    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> submission(memberships, bindings, transport);

    // Preflight aggregate authority before consuming Radio terminal evidence. A stale aggregate handle cannot release the
    // correlation or pending next-hop acceptance; the same terminal evidence remains consumable through the valid handle.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 101}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 100, 500, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);

        Mesh::RouteAttemptCoordinator attempts(defaultRoutePolicy, defaultRetryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(submission, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 100, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(101));

        radio.NextTransmission = {61};
        auto submitted = radioDelivery.Submit(local, route, 3, payloadBytes, sizeof(payloadBytes), 102);
        assert(submitted.Action == Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
        assert(radioDelivery.HasPendingRadioTerminalCorrelation());
        radio.Resolve(address, Radio::RadioDirectLinkEvidence::Failed());

        Mesh::ApplicationTransmissionHandle stale{handle.Slot, static_cast<std::uint16_t>(handle.Generation + 1U)};
        if (stale.Generation == 0U) stale.Generation = 1U;
        assert(applicationRadio.TryConsume(stale, radioDelivery, 110) ==
            Mesh::ApplicationRadioTerminalDisposition::UnknownTransmission);
        assert(radioDelivery.HasPendingRadioTerminalCorrelation());
        assert(delivery.AwaitingNextHopAcceptance());

        assert(applicationRadio.TryConsume(handle, radioDelivery, 111) ==
            Mesh::ApplicationRadioTerminalDisposition::RetryCurrentRoute);
        assert(!radioDelivery.HasPendingRadioTerminalCorrelation());
        assert(!delivery.AwaitingNextHopAcceptance());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 101, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::Pending);

        assert(recipients.TerminalizeComposed(
                   handle,
                   101,
                   Mesh::ApplicationRecipientOutcome::PermanentFailure,
                   radioDelivery) == Mesh::ApplicationRecipientTerminalizationResult::Terminalized);
        assert(!radioDelivery.IsActive());
        assert(aggregate.Release(handle));
    }

    // Direct-link completion/peer ACK remains non-terminal: Mesh still waits for authenticated next-hop acceptance.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 202}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 200, 600, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(defaultRoutePolicy, defaultRetryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(submission, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 200, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(201));
        radio.NextTransmission = {62};
        assert(radioDelivery.Submit(local, route, 3, payloadBytes, sizeof(payloadBytes), 202).Action ==
            Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
        radio.Resolve(address, Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged());
        assert(applicationRadio.TryConsume(handle, radioDelivery, 210) ==
            Mesh::ApplicationRadioTerminalDisposition::AwaitingNextHopAcceptance);
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 202, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::Pending);
        assert(delivery.AwaitingNextHopAcceptance());

        assert(recipients.TerminalizeComposed(
                   handle,
                   202,
                   Mesh::ApplicationRecipientOutcome::PermanentFailure,
                   radioDelivery) == Mesh::ApplicationRecipientTerminalizationResult::Terminalized);
        assert(!radioDelivery.IsActive());
        assert(aggregate.Release(handle));
    }

    // Deadline observed while consuming a failed Radio attempt becomes the authoritative aggregate DeadlineExpired result.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 303}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 300, 350, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(defaultRoutePolicy, defaultRetryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(submission, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 300, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(301));
        radio.NextTransmission = {63};
        assert(radioDelivery.Submit(local, route, 3, payloadBytes, sizeof(payloadBytes), 302).Action ==
            Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
        radio.Resolve(address, Radio::RadioDirectLinkEvidence::Failed());
        assert(applicationRadio.TryConsume(handle, radioDelivery, 350) ==
            Mesh::ApplicationRadioTerminalDisposition::DeadlineExpired);
        assert(!radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 303, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::DeadlineExpired);
        assert(aggregate.Release(handle));
    }

    // Policy exhaustion is definitive for this recipient and maps to aggregate PermanentFailure, not delivery success.
    {
        NoSameRouteRetry noSameRoute;
        NoDistinctRouteRetry noDistinctRoute;
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 404}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, primitive, payload, 400, 800, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(noSameRoute, noDistinctRoute);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(submission, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 400, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(401));
        radio.NextTransmission = {64};
        assert(radioDelivery.Submit(local, route, 3, payloadBytes, sizeof(payloadBytes), 402).Action ==
            Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
        radio.Resolve(address, Radio::RadioDirectLinkEvidence::Failed());
        assert(applicationRadio.TryConsume(handle, radioDelivery, 403) ==
            Mesh::ApplicationRadioTerminalDisposition::PermanentFailure);
        assert(!radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 404, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::PermanentFailure);
        assert(aggregate.Release(handle));
    }

    transport.Stop();
    return 0;
}
