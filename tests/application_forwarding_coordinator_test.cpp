#include <cassert>
#include <cstdint>
#include <cstring>

#include <ESPressio_ApplicationForwardingCoordinator.hpp>

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t value) { System::DeviceIdentifier::Storage b{}; b[15] = value; return System::DeviceIdentifier(b); }
Mesh::MembershipIncarnation Incarnation(std::uint8_t value) { Mesh::MembershipIncarnation::Storage b{}; b[15] = value; return Mesh::MembershipIncarnation(b); }

class Repeatable final : public Mesh::IRepeatableSerializedPayloadSource {
    const std::uint8_t* _bytes;
    std::size_t _size;
public:
    Repeatable(const std::uint8_t* bytes, std::size_t size) : _bytes(bytes), _size(size) {}
    std::size_t Size() const noexcept override { return _size; }
    bool Read(std::size_t offset, std::uint8_t* destination, std::size_t length) const noexcept override {
        if (destination == nullptr || offset > _size || length > _size - offset) return false;
        std::memcpy(destination, _bytes + offset, length); return true;
    }
};

class FakeRadio final : public Radio::IRadio {
    Radio::RadioObserverSubscriptions _observers{}; bool _started{false};
    Radio::RadioAddress _local{Radio::RadioAddress::FromBytes(reinterpret_cast<const std::uint8_t*>("L"), 1)};
public:
    const std::uint8_t* LastPayload{nullptr}; std::size_t LastSize{0}; std::size_t Sends{0};
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override { return {Radio::RadioCapability::None, 64, 1, 512}; }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t* payload, std::size_t size) override {
        LastPayload = payload; LastSize = size; ++Sends; return Radio::RadioSendResult::Accepted();
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
};
}

int main() {
    const auto local = Device(1), remote = Device(2); const auto incarnation = Incarnation(9);
    Mesh::AuthenticatedMembershipTable<2> memberships;
    assert(memberships.UpsertAuthenticated(remote, incarnation, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) == Mesh::AuthenticatedMembershipInsertResult::Inserted);

    FakeRadio radio; Radio::RadioTransport transport; assert(transport.AddInterface(radio)); assert(transport.Start());
    const std::uint8_t addressByte = 7; Radio::RadioPeerHandle peer{};
    assert(transport.Peers().Observe(radio, Radio::RadioAddress::FromBytes(&addressByte, 1), peer) == Radio::RadioPeerObserveResult::Observed);
    Mesh::AuthenticatedDirectPeerBindingTable<2> bindings;
    assert(bindings.Bind({remote, incarnation, 1, peer}) == Mesh::DirectPeerBindingResult::Bound);
    Mesh::ForwardingSubmissionCoordinator<2,2,2> forwarding{memberships, bindings, transport};

    Mesh::TopologyLinkIdentity hop{local, 1, remote, 1}; Mesh::ResolvedRoute<2> route; assert(route.Assign(local, remote, &hop, 1));
    const std::uint8_t bytes[]{1,2,3,4}; Mesh::ApplicationTransmissionRecipient recipient{remote, incarnation, 101};

    Mesh::ApplicationTransmissionTable<2,2> transmissions; Mesh::ApplicationTransmissionHandle aggregate{};
    assert(transmissions.Begin(&recipient, 1, Mesh::ApplicationPayload::Borrowed(bytes, sizeof(bytes)), 100, 200, aggregate) == Mesh::ApplicationTransmissionBeginResult::Begun);
    Mesh::ApplicationForwardingCoordinator<2,2,2,2,2> coordinator{transmissions, forwarding};
    auto result = coordinator.SubmitRecipient(aggregate, 0, local, route, 1, 110);
    assert(result.Disposition == Mesh::ApplicationForwardingDisposition::Submitted);
    assert(result.Submission.Disposition == Mesh::ForwardingSubmissionDisposition::Accepted);
    assert(radio.Sends == 1U && radio.LastPayload == bytes && radio.LastSize == sizeof(bytes));

    Mesh::TopologyLinkIdentity wrongHop{remote, 1, local, 1}; Mesh::ResolvedRoute<2> wrong;
    assert(wrong.Assign(remote, local, &wrongHop, 1));
    result = coordinator.SubmitRecipient(aggregate, 0, local, wrong, 1, 111);
    assert(result.Disposition == Mesh::ApplicationForwardingDisposition::RouteMismatch && radio.Sends == 1U);
    assert(transmissions.SetOutcome(aggregate, 101, Mesh::ApplicationRecipientOutcome::Delivered) == Mesh::ApplicationTransmissionUpdateResult::Updated);
    result = coordinator.SubmitRecipient(aggregate, 0, local, route, 1, 112);
    assert(result.Disposition == Mesh::ApplicationForwardingDisposition::RecipientTerminal && radio.Sends == 1U);
    assert(transmissions.Release(aggregate));

    Repeatable repeatable(bytes, sizeof(bytes)); Mesh::ApplicationTransmissionHandle repeatableAggregate{};
    assert(transmissions.Begin(&recipient, 1, Mesh::ApplicationPayload::Repeatable(repeatable), 100, 200, repeatableAggregate) == Mesh::ApplicationTransmissionBeginResult::Begun);
    result = coordinator.SubmitRecipient(repeatableAggregate, 0, local, route, 1, 113);
    assert(result.Disposition == Mesh::ApplicationForwardingDisposition::StagingRequired && radio.Sends == 1U);
    assert(transmissions.Release(repeatableAggregate));

    transport.Stop(); return 0;
}
