#include <cassert>
#include <cstdint>

#include <ESPressio_DeferredLogicalTransferTracker.hpp>
#include <ESPressio_OutboundRadioDeliveryCoordinator.hpp>

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t value) {
    System::DeviceIdentifier::Storage bytes{}; bytes[15] = value; return System::DeviceIdentifier(bytes);
}
Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    Mesh::MembershipIncarnation::Storage bytes{}; bytes[15] = value; return Mesh::MembershipIncarnation(bytes);
}
class DeferredRadio final : public Radio::IRadio {
    Radio::RadioObserverSubscriptions _observers{};
    bool _started{false};
    Radio::RadioAddress _local{Radio::RadioAddress::FromBytes(reinterpret_cast<const std::uint8_t*>("L"), 1)};
public:
    Radio::RadioTransmissionHandle NextTransmission{51};
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override { return {Radio::RadioCapability::None, 64, 1, 512}; }
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
}

int main() {
    const auto local = Device(1);
    const auto remote = Device(2);
    const auto incarnation = Incarnation(9);

    Mesh::AuthenticatedMembershipTable<2> memberships;
    assert(memberships.UpsertAuthenticated(remote, incarnation, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Radio::DeferredLogicalTransferTracker<2> radioTracker;
    Mesh::ForwardingRadioTerminalCorrelation<1> correlation;
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

    Mesh::DefaultRouteAttemptPolicy routePolicy;
    Mesh::DefaultRetryPolicy retryPolicy;
    Mesh::RouteAttemptCoordinator attempts{routePolicy, retryPolicy};
    Mesh::DeliveryAcknowledgementTracker<1> ackTracker;
    Mesh::DeliveryAcknowledgementCoordinator<1> acknowledgements{ackTracker};
    Mesh::OutboundDeliveryLifecycle<1> delivery{attempts, acknowledgements};
    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> submission{memberships, bindings, transport};
    Mesh::ForwardingRadioAttemptCoordinator<1, 2, 2, 2> radioAttempts{submission, correlation, attempts};
    Mesh::OutboundRadioDeliveryCoordinator<1, 1, 2, 2, 2> coordinator{delivery, radioAttempts};
    const std::uint8_t payload[]{1, 2, 3};

    assert(delivery.Begin(remote, incarnation, 71, 100, 500, false) == Mesh::OutboundDeliveryBeginResult::Begun);
    assert(delivery.BeginDistinctRouteAttempt(101));
    auto result = coordinator.Submit(local, route, 3, payload, sizeof(payload), 102);
    assert(result.Submission.Disposition == Mesh::ForwardingSubmissionDisposition::Accepted);
    assert(result.Submission.NextHop == remote);
    assert(result.Submission.NextHopIncarnation == incarnation);
    assert(result.Action == Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
    assert(coordinator.HasPendingRadioTerminalCorrelation());
    assert(delivery.AwaitingNextHopAcceptance());

    // Terminal Radio completion is consumed and correlation is released, but exact Mesh acceptance remains pending.
    radio.Resolve(address, Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged());
    Mesh::OutboundForwardingAction terminalAction{};
    assert(coordinator.TryConsumeRadioTerminal(110, terminalAction));
    assert(terminalAction == Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
    assert(!coordinator.HasPendingRadioTerminalCorrelation());
    assert(delivery.AwaitingNextHopAcceptance());

    Mesh::RemainingHopLimit hops = 3;
    assert(coordinator.AcceptNextHopAuthenticated(remote, incarnation, 71, 120, hops) ==
           Mesh::ForwardingAcceptanceAction::ForwardingComplete);
    assert(hops == 2U);
    assert(!delivery.AwaitingNextHopAcceptance());
    coordinator.Reset();

    // Authenticated Mesh acceptance may arrive before deferred Radio terminal evidence. It is stronger for the Mesh hop,
    // so the retained Radio correlation is abandoned deterministically and later provider evidence becomes inert here.
    radio.NextTransmission = {52};
    assert(delivery.Begin(remote, incarnation, 72, 200, 500, false) == Mesh::OutboundDeliveryBeginResult::Begun);
    assert(delivery.BeginDistinctRouteAttempt(201));
    result = coordinator.Submit(local, route, 3, payload, sizeof(payload), 202);
    assert(coordinator.HasPendingRadioTerminalCorrelation());
    hops = 3;
    assert(coordinator.AcceptNextHopAuthenticated(remote, incarnation, 72, 203, hops) ==
           Mesh::ForwardingAcceptanceAction::ForwardingComplete);
    assert(hops == 2U);
    assert(!coordinator.HasPendingRadioTerminalCorrelation());
    radio.Resolve(address, Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged());
    assert(!coordinator.TryConsumeRadioTerminal(204, terminalAction));
    coordinator.Reset();

    // Terminal Radio failure consumes correlation, cancels the now-invalid pending acceptance and enters bounded retry.
    radio.NextTransmission = {53};
    assert(delivery.Begin(remote, incarnation, 73, 300, 500, false) == Mesh::OutboundDeliveryBeginResult::Begun);
    assert(delivery.BeginDistinctRouteAttempt(301));
    result = coordinator.Submit(local, route, 3, payload, sizeof(payload), 302);
    assert(delivery.AwaitingNextHopAcceptance());
    radio.Resolve(address, Radio::RadioDirectLinkEvidence::Failed());
    assert(coordinator.TryConsumeRadioTerminal(303, terminalAction));
    assert(terminalAction == Mesh::OutboundForwardingAction::RetryCurrentRoute);
    assert(!delivery.AwaitingNextHopAcceptance());
    assert(!coordinator.HasPendingRadioTerminalCorrelation());
    assert(delivery.BeginCurrentRouteRetry(304));
    coordinator.Reset();

    // Cancellation/reset cannot leak the explicit-capacity correlation reservation.
    radio.NextTransmission = {54};
    assert(delivery.Begin(remote, incarnation, 74, 400, 500, false) == Mesh::OutboundDeliveryBeginResult::Begun);
    assert(delivery.BeginDistinctRouteAttempt(401));
    result = coordinator.Submit(local, route, 3, payload, sizeof(payload), 402);
    assert(coordinator.HasPendingRadioTerminalCorrelation());
    assert(correlation.Size() == 1U);
    coordinator.Reset();
    assert(correlation.Size() == 0U);
    assert(!delivery.IsActive());

    transport.Stop();
    return 0;
}
