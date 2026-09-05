#include <cassert>
#include <cstdint>

#include <ESPressio_DeferredLogicalTransferTracker.hpp>
#include <ESPressio_ForwardingRadioAttemptCoordinator.hpp>

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
    Radio::RadioTransmissionHandle NextTransmission{41};
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
    assert(memberships.UpsertAuthenticated(remote, incarnation, Mesh::MembershipState::Active,
                                           Mesh::ReachabilityState::Reachable) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Radio::DeferredLogicalTransferTracker<2> radioTracker;
    Mesh::ForwardingRadioTerminalCorrelation<1> correlation;
    Radio::RadioTransport transport{radioTracker, correlation};
    DeferredRadio radio;
    assert(transport.AddInterface(radio));
    assert(transport.Start());

    const std::uint8_t remoteAddressByte = 7;
    const auto remoteAddress = Radio::RadioAddress::FromBytes(&remoteAddressByte, 1);
    Radio::RadioPeerHandle peer{};
    assert(transport.Peers().Observe(radio, remoteAddress, peer) == Radio::RadioPeerObserveResult::Observed);

    Mesh::AuthenticatedDirectPeerBindingTable<2> bindings;
    assert(bindings.Bind({remote, incarnation, 1, peer}) == Mesh::DirectPeerBindingResult::Bound);

    Mesh::TopologyLinkIdentity hop{local, 1, remote, 1};
    Mesh::ResolvedRoute<2> route;
    assert(route.Assign(local, remote, &hop, 1));

    Mesh::DefaultRouteAttemptPolicy routePolicy;
    Mesh::DefaultRetryPolicy retryPolicy;
    Mesh::RouteAttemptCoordinator attempts{routePolicy, retryPolicy};
    assert(attempts.BeginDistinctRouteAttempt(100, 500));

    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> submission{memberships, bindings, transport};
    Mesh::ForwardingRadioAttemptCoordinator<1, 2, 2, 2> coordinator{submission, correlation, attempts};
    const std::uint8_t payload[]{1, 2, 3};

    auto result = coordinator.Submit(local, route, 1, payload, sizeof(payload), 100, 500);
    assert(result.Submission.Disposition == Mesh::ForwardingSubmissionDisposition::Accepted);
    assert(result.Action == Mesh::ForwardingAttemptAction::AwaitingNextHopAcceptance);
    assert(result.Correlation);
    assert(result.CorrelationDisposition == Mesh::ForwardingRadioCorrelationDisposition::Bound);

    // The single Mesh correlation slot is retained while terminal Radio evidence is outstanding. A second forwarding
    // attempt is rejected before Radio submission rather than accepting work whose terminal evidence cannot be tracked.
    auto saturated = coordinator.Submit(local, route, 1, payload, sizeof(payload), 101, 500);
    assert(saturated.Submission.Disposition == Mesh::ForwardingSubmissionDisposition::ResourceUnavailable);
    assert(saturated.CorrelationDisposition == Mesh::ForwardingRadioCorrelationDisposition::ResourceUnavailable);

    Mesh::ForwardingAttemptAction terminalAction{};
    assert(!coordinator.TryConsumeTerminal(result.Correlation, 110, 500, terminalAction));

    radio.Resolve(remoteAddress, Radio::RadioDirectLinkEvidence::Failed());
    Radio::LogicalTransferTerminalEvidence terminal;
    assert(coordinator.TryConsumeTerminal(result.Correlation, 111, 500, terminalAction, &terminal));
    assert(terminal.Evidence.TransmissionFailed());
    assert(terminalAction == Mesh::ForwardingAttemptAction::RetryCurrentRoute);
    assert(correlation.Size() == 0U);

    // A completed Radio transfer still cannot complete the Mesh hop; authenticated next-hop acceptance remains required.
    radio.NextTransmission = {42};
    result = coordinator.Submit(local, route, 1, payload, sizeof(payload), 120, 500);
    assert(result.CorrelationDisposition == Mesh::ForwardingRadioCorrelationDisposition::Bound);
    radio.Resolve(remoteAddress, Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged());
    assert(coordinator.TryConsumeTerminal(result.Correlation, 121, 500, terminalAction));
    assert(terminalAction == Mesh::ForwardingAttemptAction::AwaitingNextHopAcceptance);

    // Deadline still dominates a later terminal failure.
    radio.NextTransmission = {43};
    result = coordinator.Submit(local, route, 1, payload, sizeof(payload), 130, 150);
    assert(result.CorrelationDisposition == Mesh::ForwardingRadioCorrelationDisposition::Bound);
    radio.Resolve(remoteAddress, Radio::RadioDirectLinkEvidence::Failed());
    assert(coordinator.TryConsumeTerminal(result.Correlation, 150, 150, terminalAction));
    assert(terminalAction == Mesh::ForwardingAttemptAction::StopDeadlineExpired);

    transport.Stop();
    return 0;
}
