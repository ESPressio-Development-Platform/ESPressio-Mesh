#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationNextHopAcceptanceCoordinator.hpp>
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
    Radio::RadioTransmissionHandle NextTransmission{81};
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override { return {Radio::RadioCapability::None, 64, 1, 512}; }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        return Radio::RadioSendResult::Accepted({}, NextTransmission);
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
};

/// <summary>
/// Minimal independently owned external lifecycle used to establish aggregate terminal authority "elsewhere" while
/// intentionally leaving the real Radio-delivery lifecycle active for late-evidence retirement coverage.
/// </summary>
class IndependentExternalLifecycle final {
    Mesh::MeshMessageId _messageId{0};
    bool _active{false};
public:
    explicit IndependentExternalLifecycle(Mesh::MeshMessageId messageId) noexcept
        : _messageId(messageId), _active(messageId != 0U) {}
    bool IsActive() const noexcept { return _active; }
    Mesh::MeshMessageId MessageId() const noexcept { return _messageId; }
    void Reset() noexcept { _active = false; }
};
}

int main() {
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
    Mesh::ApplicationNextHopAcceptanceCoordinator<2, 4, 4, 2, 2, 2, 2> acceptance(recipients);
    Mesh::DefaultRouteAttemptPolicy routePolicy;
    Mesh::DefaultRetryPolicy retryPolicy;
    Mesh::DeliveryAcknowledgementTracker<2> acknowledgementTracker;
    Mesh::DeliveryAcknowledgementCoordinator<2> acknowledgements(acknowledgementTracker);
    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> forwarding(memberships, bindings, transport);

    // Aggregate authority is checked before acceptance. Stale aggregate identity and unrelated authenticated evidence
    // cannot consume the pending transition, release Radio correlation, or decrement HopLimit.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 101}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, payload, 100, 500, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 100, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(101));
        radio.NextTransmission = {81};
        assert(radioDelivery.Submit(local, route, 3, bytes, sizeof(bytes), 102).Action ==
            Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
        assert(radioDelivery.HasPendingRadioTerminalCorrelation());

        Mesh::RemainingHopLimit remaining{3};
        Mesh::ApplicationTransmissionHandle stale{handle.Slot, static_cast<std::uint16_t>(handle.Generation + 1U)};
        if (stale.Generation == 0U) stale.Generation = 1U;
        assert(acceptance.ApplyAuthenticated(stale, radioDelivery, remote, incarnation, 101, 110, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::UnknownTransmission);
        assert(remaining == 3U);
        assert(radioDelivery.HasPendingRadioTerminalCorrelation());
        assert(delivery.AwaitingNextHopAcceptance());

        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, Device(7), incarnation, 101, 111, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::UnrelatedEvidence);
        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, remote, Incarnation(8), 101, 112, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::UnrelatedEvidence);
        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, remote, incarnation, 999, 113, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::UnrelatedEvidence);
        assert(remaining == 3U);
        assert(radioDelivery.HasPendingRadioTerminalCorrelation());
        assert(delivery.AwaitingNextHopAcceptance());

        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, remote, incarnation, 101, 114, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::ForwardingTransitionCommitted);
        assert(remaining == 2U);
        assert(!radioDelivery.HasPendingRadioTerminalCorrelation());
        assert(!delivery.AwaitingNextHopAcceptance());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 101, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::Pending);

        // Replaying the same acceptance cannot consume another hop or convert forwarding responsibility into delivery.
        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, remote, incarnation, 101, 115, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::UnrelatedEvidence);
        assert(remaining == 2U);
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

    // Acceptance at the immutable deadline does not commit HopLimit and terminalizes the aggregate as DeadlineExpired.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 202}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, payload, 200, 250, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 200, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(201));
        radio.NextTransmission = {82};
        assert(radioDelivery.Submit(local, route, 3, bytes, sizeof(bytes), 202).Action ==
            Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);

        Mesh::RemainingHopLimit remaining{3};
        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, remote, incarnation, 202, 250, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::DeadlineExpired);
        assert(remaining == 3U);
        assert(!radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 202, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::DeadlineExpired);
        assert(aggregate.Release(handle));
    }

    // If aggregate terminalization wins through another owner first, late acceptance is not applied and the exact
    // still-active Radio delivery is retired by the aggregate-aware acceptance coordinator without consuming HopLimit.
    {
        Mesh::ApplicationTransmissionRecipient recipient[] = {{remote, incarnation, 303}};
        Mesh::ApplicationTransmissionHandle handle{};
        assert(aggregate.Begin(recipient, 1, payload, 300, 600, handle) == Mesh::ApplicationTransmissionAdmissionResult::Begun);
        Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
        Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
        Mesh::ForwardingRadioAttemptCoordinator<4, 2, 2, 2> radioAttempts(forwarding, correlation, attempts);
        Mesh::OutboundRadioDeliveryCoordinator<2, 4, 2, 2, 2> radioDelivery(delivery, radioAttempts);
        assert(aggregate.BeginRecipient(handle, 0, 300, false, delivery) == Mesh::ApplicationRecipientBeginResult::Begun);
        assert(delivery.BeginDistinctRouteAttempt(301));
        radio.NextTransmission = {83};
        assert(radioDelivery.Submit(local, route, 3, bytes, sizeof(bytes), 302).Action ==
            Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);

        IndependentExternalLifecycle independent{303};
        assert(recipients.TerminalizeComposed(
                   handle,
                   303,
                   Mesh::ApplicationRecipientOutcome::DeadlineExpired,
                   independent) == Mesh::ApplicationRecipientTerminalizationResult::Terminalized);
        assert(!independent.IsActive());
        assert(radioDelivery.IsActive());

        Mesh::RemainingHopLimit remaining{3};
        assert(acceptance.ApplyAuthenticated(handle, radioDelivery, remote, incarnation, 303, 303, remaining) ==
            Mesh::ApplicationNextHopAcceptanceDisposition::AlreadyTerminal);
        assert(remaining == 3U);
        assert(!radioDelivery.IsActive());
        Mesh::ApplicationRecipientOutcome outcome{};
        assert(aggregate.TryGetRecipientOutcome(handle, 303, outcome));
        assert(outcome == Mesh::ApplicationRecipientOutcome::DeadlineExpired);
        assert(aggregate.Release(handle));
    }

    transport.Stop();
    return 0;
}
