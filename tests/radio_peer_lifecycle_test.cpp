#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_RadioPeerLifecycleCoordinator.hpp"

using namespace ESPressio;

class FakeRadio final : public Radio::IRadio {
public:
    explicit FakeRadio(std::uint8_t address) : _address(Radio::RadioAddress::FromBytes(&address, 1)) {}

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::HardwareAddressing, 64, 1, 512};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _address; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        return Radio::RadioSendResult::Accepted();
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

private:
    Radio::RadioAddress _address{};
    bool _started{false};
    Radio::RadioObserverSubscriptions _observers{};
};

static System::DeviceIdentifier Device(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return Mesh::MembershipIncarnation{bytes};
}

int main() {
    FakeRadio radioA{0xA1};
    FakeRadio radioB{0xB1};

    Mesh::MeshRadioRegistry<2> radios;
    Mesh::RadioIdentifier idA = 0;
    Mesh::RadioIdentifier idB = 0;
    assert(radios.Register(radioA, idA) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(radios.Register(radioB, idB) == Mesh::MeshRadioRegistrationResult::Registered);

    Mesh::PendingNeighbourCandidateTable<4> candidates;
    Mesh::InboundAuthenticationReservationTable<2> authentications;
    Mesh::RadioPeerLifecycleCoordinator<4, 2, 2> lifecycle{radios, candidates, authentications};

    Radio::RadioTransport transport;
    auto registration = transport.Observers().Subscribe<Radio::IRadioTransportPeerObserver>(&lifecycle);

    const Radio::RadioPeerHandle peerA{1, 10};
    const Radio::RadioPeerHandle peerB{2, 10};
    const Mesh::UntrustedMembershipClaim claimA{Device(1), Incarnation(1)};
    const Mesh::UntrustedMembershipClaim claimB{Device(2), Incarnation(2)};

    Mesh::NeighbourCandidateHandle a1{};
    Mesh::NeighbourCandidateHandle a2{};
    Mesh::NeighbourCandidateHandle b1{};
    assert(candidates.Observe(idA, peerA, claimA, 100, a1) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.Observe(idA, peerA, claimB, 101, a2) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.Observe(idB, peerB, claimB, 102, b1) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.Size() == 3);

    assert(candidates.SetState(a1, Mesh::MembershipState::Authenticating));
    assert(authentications.TryReserve(a1) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(authentications.Contains(a1));

    lifecycle.OnRadioPeerInvalidated(
        transport,
        radioA,
        peerA,
        radioA.LocalAddress(),
        Radio::RadioPeerInvalidationReason::Explicit
    );

    // Every pre-auth candidate bound to the invalidated Radio/peer is removed, including expensive auth work.
    assert(candidates.Resolve(a1) == nullptr);
    assert(candidates.Resolve(a2) == nullptr);
    assert(!authentications.Contains(a1));
    assert(candidates.Size() == 1);

    // Unrelated Radio/peer bindings remain untouched.
    assert(candidates.Resolve(b1) != nullptr);
    assert(candidates.Resolve(b1)->Radio == idB);
    assert(candidates.Resolve(b1)->Peer == peerB);

    // An invalidation from a Radio that is no longer registered cannot accidentally clean another Radio's state.
    assert(radios.Remove(radioA));
    Mesh::NeighbourCandidateHandle remaining = b1;
    lifecycle.OnRadioPeerInvalidated(
        transport,
        radioA,
        peerB,
        radioA.LocalAddress(),
        Radio::RadioPeerInvalidationReason::InterfaceRemoved
    );
    assert(candidates.Resolve(remaining) != nullptr);

    // Critically, this coordinator contains no authenticated-membership reference and therefore cannot turn
    // link-local peer loss into authoritative membership removal.
    return 0;
}
