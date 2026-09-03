#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_PreAuthenticationPeerLifecycleObserver.hpp"

using namespace ESPressio;

class FakeRadio final : public Radio::IRadio {
public:
    explicit FakeRadio(std::uint8_t address) : _address(Radio::RadioAddress::FromBytes(&address, 1)) {}

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::HardwareAddressing, 32, 1, 256};
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

static Radio::RadioAddress Address(std::uint8_t value) {
    return Radio::RadioAddress::FromBytes(&value, 1);
}

int main() {
    FakeRadio radio{0xA1};
    Mesh::MeshRadioRegistry<1> radios;
    Mesh::RadioIdentifier radioIdentifier = 0;
    assert(radios.Register(radio, radioIdentifier) == Mesh::MeshRadioRegistrationResult::Registered);

    Mesh::PendingNeighbourCandidateTable<3> candidates;
    Mesh::InboundAuthenticationReservationTable<2> authentications;
    Mesh::AuthenticatedMembershipTable<2> memberships;

    // Authenticated authority is intentionally separate and must survive any direct-link peer invalidation.
    const auto authenticatedDevice = Device(9);
    const auto authenticatedIncarnation = Incarnation(9);
    assert(memberships.UpsertAuthenticated(
        authenticatedDevice,
        authenticatedIncarnation,
        Mesh::MembershipState::Active,
        Mesh::ReachabilityState::Reachable
    ) == Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Radio::RadioTransport transport;
    assert(transport.AddInterface(radio));

    Mesh::PreAuthenticationPeerLifecycleObserver<1, 3, 2> lifecycle{
        radios,
        candidates,
        authentications
    };
    auto subscription = transport.Observers().Subscribe<Radio::IRadioTransportPeerObserver>(&lifecycle);
    assert(subscription);

    Radio::RadioPeerHandle peerA{};
    Radio::RadioPeerHandle peerB{};
    assert(transport.Peers().Observe(radio, Address(1), peerA) == Radio::RadioPeerObserveResult::Observed);
    assert(transport.Peers().Observe(radio, Address(2), peerB) == Radio::RadioPeerObserveResult::Observed);

    Mesh::NeighbourCandidateHandle candidateA{};
    Mesh::NeighbourCandidateHandle candidateASecondClaim{};
    Mesh::NeighbourCandidateHandle candidateB{};
    assert(candidates.Observe(
        radioIdentifier,
        peerA,
        {Device(1), Incarnation(1)},
        100,
        candidateA
    ) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.Observe(
        radioIdentifier,
        peerA,
        {Device(2), Incarnation(2)},
        101,
        candidateASecondClaim
    ) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(candidates.Observe(
        radioIdentifier,
        peerB,
        {Device(3), Incarnation(3)},
        102,
        candidateB
    ) == Mesh::PendingCandidateInsertResult::Inserted);

    assert(authentications.TryReserve(candidateA) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(authentications.TryReserve(candidateB) == Mesh::InboundAuthenticationReservationResult::Reserved);
    assert(candidates.SetState(candidateA, Mesh::MembershipState::Authenticating));
    assert(candidates.SetState(candidateB, Mesh::MembershipState::Authenticating));

    // Exact peer invalidation removes every candidate bound to that peer and releases any auth reservation.
    assert(transport.InvalidatePeer(peerA));
    assert(candidates.Resolve(candidateA) == nullptr);
    assert(candidates.Resolve(candidateASecondClaim) == nullptr);
    assert(!authentications.Contains(candidateA));
    assert(candidates.Resolve(candidateB) != nullptr);
    assert(authentications.Contains(candidateB));

    // Link-local loss never removes or supersedes authenticated membership authority.
    assert(memberships.FindExact(authenticatedDevice, authenticatedIncarnation) != nullptr);
    assert(memberships.FindExact(authenticatedDevice, authenticatedIncarnation)->State == Mesh::MembershipState::Active);

    // Interface removal emits peer invalidation before the RadioIdentifier mapping is removed by Mesh composition.
    assert(transport.RemoveInterface(radio));
    assert(candidates.Resolve(candidateB) == nullptr);
    assert(!authentications.Contains(candidateB));
    assert(memberships.FindExact(authenticatedDevice, authenticatedIncarnation) != nullptr);

    return 0;
}
