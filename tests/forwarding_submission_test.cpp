#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_ForwardingSubmissionCoordinator.hpp>

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

class FakeRadio final : public Radio::IRadio {
    Radio::RadioObserverSubscriptions _observers{};
    bool _started{false};
    Radio::RadioAddress _local{Radio::RadioAddress::FromBytes(reinterpret_cast<const std::uint8_t*>("L"), 1)};
public:
    Radio::RadioSendStatus NextSendStatus{Radio::RadioSendStatus::Accepted};
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::None, 64, 1, 512};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        return {NextSendStatus, 0};
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

    FakeRadio radio;
    Radio::RadioTransport transport;
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

    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> coordinator{memberships, bindings, transport};
    const std::uint8_t payload[]{1, 2, 3};

    auto result = coordinator.Submit(local, route, 1, payload, sizeof(payload), 100, 200);
    assert(result.Disposition == Mesh::ForwardingSubmissionDisposition::Accepted);

    // Submission acceptance does not consume hop budget; transition commitment remains separate.
    Mesh::RemainingHopLimit remaining = 1;
    assert(remaining == 1U);

    assert(transport.InvalidatePeer(peer));
    result = coordinator.Submit(local, route, remaining, payload, sizeof(payload), 110, 200);
    assert(result.Disposition == Mesh::ForwardingSubmissionDisposition::PeerUnavailable);

    result = coordinator.Submit(local, route, 0, payload, sizeof(payload), 110, 200);
    assert(result.Disposition == Mesh::ForwardingSubmissionDisposition::HopLimitExhausted);
    result = coordinator.Submit(local, route, 1, payload, sizeof(payload), 200, 200);
    assert(result.Disposition == Mesh::ForwardingSubmissionDisposition::DeadlineExpired);

    transport.Stop();
    return 0;
}
