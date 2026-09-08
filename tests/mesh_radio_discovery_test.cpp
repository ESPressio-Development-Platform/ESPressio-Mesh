#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_NeighbourDiscoveryCoordinator.hpp"

using namespace ESPressio;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _address (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _observers (Radio::RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Total Memory: 24 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
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

int main() {
    FakeRadio radioA(0xA1);
    FakeRadio radioB(0xB1);
    FakeRadio radioC(0xC1);

    Mesh::MeshRadioRegistry<2> radios;
    Mesh::RadioIdentifier idA = 0;
    Mesh::RadioIdentifier idB = 0;
    assert(radios.Register(radioA, idA) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(idA == 1);
    assert(radios.Register(radioB, idB) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(idB == 2);

    Mesh::RadioIdentifier duplicate = 0;
    assert(radios.Register(radioA, duplicate) == Mesh::MeshRadioRegistrationResult::AlreadyRegistered);
    assert(duplicate == idA);
    assert(radios.Resolve(idA) == &radioA);
    assert(radios.IdentifierOf(radioB) == idB);

    // Removal frees capacity but never recycles the identifier within this membership incarnation.
    assert(radios.Remove(radioA));
    Mesh::RadioIdentifier idC = 0;
    assert(radios.Register(radioC, idC) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(idC == 3);
    assert(radios.Resolve(idA) == nullptr);
    assert(radios.Resolve(idC) == &radioC);

    Mesh::PendingNeighbourCandidateTable<2> candidates;
    Mesh::NeighbourDiscoveryCoordinator<2, 2> discovery{radios, candidates};

    Radio::RadioTransportMessageView transfer{};
    transfer.SourcePeer = Radio::RadioPeerHandle{3, 7};
    transfer.TransferId = 1;

    Mesh::NeighbourCandidateHandle candidate{};
    const Mesh::UntrustedMembershipClaim claim{Device(1), Incarnation(1)};
    assert(discovery.ObserveClaim(radioC, transfer, claim, 100, candidate) ==
           Mesh::NeighbourDiscoveryResult::Inserted);
    assert(candidate);
    const auto* pending = candidates.Resolve(candidate);
    assert(pending != nullptr);
    assert(pending->Radio == idC);
    assert(pending->Peer == transfer.SourcePeer);
    assert(pending->Claim.Device == claim.Device);

    Mesh::NeighbourCandidateHandle refreshed{};
    assert(discovery.ObserveClaim(radioC, transfer, claim, 120, refreshed) ==
           Mesh::NeighbourDiscoveryResult::Refreshed);
    assert(refreshed == candidate);

    // A peer handle from an unregistered local Radio cannot create candidate authority.
    Mesh::NeighbourCandidateHandle invalid{};
    assert(discovery.ObserveClaim(radioA, transfer, claim, 130, invalid) ==
           Mesh::NeighbourDiscoveryResult::RadioNotRegistered);
    assert(!invalid);

    // Starting a genuinely new MembershipIncarnation is the only time RadioIdentifier allocation restarts.
    radios.ResetForNewIncarnation();
    Mesh::RadioIdentifier newIncarnationId = 0;
    assert(radios.Register(radioA, newIncarnationId) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(newIncarnationId == 1);

    return 0;
}
