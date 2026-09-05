#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationRecipientLifecycleCoordinator.hpp>
#include <ESPressio_ForwardingRadioAttemptCoordinator.hpp>
#include <ESPressio_OutboundRadioDeliveryCoordinator.hpp>
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
    Radio::RadioTransmissionHandle Transmission{71};

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::None, 64, 1, 512};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        return Radio::RadioSendResult::Accepted({}, Transmission);
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
    const std::uint8_t payloadBytes[]{1, 2, 3};
    const auto payload = Mesh::ApplicationPayload::Borrowed(payloadBytes, sizeof(payloadBytes));

    Mesh::AuthenticatedMembershipTable<2> memberships;
    assert(memberships.UpsertAuthenticated(
        remote,
        incarnation,
        Mesh::MembershipState::Active,
        Mesh::ReachabilityState::Reachable
    ) == Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Radio::DeferredLogicalTransferTracker<2> radioTracker;
    Mesh::ForwardingRadioTerminalCorrelation<2> correlations;
    Radio::RadioTransport transport{radioTracker, correlations};
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

    Mesh::ApplicationTransmissionTable<2, 2> transmissions;
    Mesh::DefaultMeshTrafficGovernor traffic;
    Mesh::ApplicationTransmissionCoordinator<2, 2> aggregate(transmissions, traffic);
    Mesh::ApplicationRecipientLifecycleCoordinator<2, 2, 2> lifecycle(aggregate);

    Mesh::DefaultRouteAttemptPolicy routePolicy;
    Mesh::DefaultRetryPolicy retryPolicy;
    Mesh::RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    Mesh::DeliveryAcknowledgementTracker<2> acknowledgementTracker;
    Mesh::DeliveryAcknowledgementCoordinator<2> acknowledgements(acknowledgementTracker);
    Mesh::OutboundDeliveryLifecycle<2> delivery(attempts, acknowledgements);
    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> submission(memberships, bindings, transport);
    Mesh::ForwardingRadioAttemptCoordinator<2, 2, 2, 2> radioAttempts(submission, correlations, attempts);
    Mesh::OutboundRadioDeliveryCoordinator<2, 2, 2, 2, 2> radioDelivery(delivery, radioAttempts);

    Mesh::ApplicationTransmissionRecipient recipients[] = {{remote, incarnation, 501}};
    Mesh::ApplicationTransmissionHandle handle{};
    assert(aggregate.Begin(recipients, 1, payload, 100, 200, handle) ==
        Mesh::ApplicationTransmissionAdmissionResult::Begun);
    assert(aggregate.BeginRecipient(handle, 0, 101, true, delivery) ==
        Mesh::ApplicationRecipientBeginResult::Begun);
    assert(acknowledgementTracker.Size() == 1U);
    assert(delivery.BeginDistinctRouteAttempt(102));

    const auto submitted = radioDelivery.Submit(local, route, 3, payloadBytes, sizeof(payloadBytes), 103);
    assert(submitted.Action == Mesh::OutboundForwardingAction::AwaitingNextHopAcceptance);
    assert(radioDelivery.HasPendingRadioTerminalCorrelation());
    assert(correlations.Size() == 1U);
    assert(radioTracker.Size() == 1U);

    const auto sweep = lifecycle.ExpireDueAndRetire(
        200,
        [&](Mesh::ApplicationTransmissionHandle candidate, Mesh::MeshMessageId messageId) noexcept
            -> Mesh::OutboundRadioDeliveryCoordinator<2, 2, 2, 2, 2>* {
            if (!(candidate == handle) || messageId != 501) return nullptr;
            return &radioDelivery;
        }
    );

    assert(sweep.ExpiredTransmissions == 1U);
    assert(sweep.ExpiredRecipients == 1U);
    assert(sweep.RetiredExternalLifecycles == 1U);
    assert(sweep.ExternalLifecycleMismatches == 0U);
    assert(!radioDelivery.IsActive());
    assert(!radioDelivery.HasPendingRadioTerminalCorrelation());
    assert(correlations.Size() == 0U);
    assert(acknowledgementTracker.Size() == 0U);

    Mesh::ApplicationRecipientOutcome outcome{};
    assert(aggregate.TryGetRecipientOutcome(handle, 501, outcome));
    assert(outcome == Mesh::ApplicationRecipientOutcome::DeadlineExpired);
    assert(traffic.Active(Mesh::MeshTrafficClass::Application) == 0U);

    // RadioTransport owns its deferred fragment correlation independently. The logical transfer can remain unresolved
    // until RadioTransport is reset; Mesh application/ACK/forwarding capacity is nevertheless released immediately.
    assert(radioTracker.Size() == 1U);
    assert(aggregate.Release(handle));
    transport.Stop();
    assert(radioTracker.Size() == 0U);
    return 0;
}
