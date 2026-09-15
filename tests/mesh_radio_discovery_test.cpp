#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_NeighbourDiscoveryCoordinator.hpp"

using namespace ESPressio;

class FakeRadio final : public Radio::IRadio {
public:
    explicit FakeRadio(std::uint8_t address, std::uint32_t domain)
        : _address(Radio::RadioAddress::FromBytes(&address, 1)), _domain{domain} {}

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::HardwareAddressing, 32, 1, 256};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _address; }
    Radio::RadioContentionDomainId ContentionDomain() const noexcept override { return _domain; }
    Radio::RadioProviderResourceProfile ProviderResources() const noexcept override { return {4, 2, 1, 0}; }
    bool IsTransmitReady() const noexcept override { return _started; }
    Radio::RadioTransmissionCost EstimateTransmissionCost(
        const Radio::RadioAddress&, std::size_t payloadBytes, const Radio::RadioServiceProfile&) const noexcept override {
        return {payloadBytes == 0 ? 1U : payloadBytes, 0U, Radio::RadioCostEstimateQuality::RelativeOnly};
    }
    Radio::RadioSendResult Send(
        const Radio::RadioAddress&, const std::uint8_t*, std::size_t) noexcept override {
        return Radio::RadioSendResult::Accepted(Radio::RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetRuntimeSink(Radio::IRadioRuntimeSink*) noexcept override {}
    Radio::ManagedRadioIngressServiceResult ServiceInbound(std::size_t = 0U) noexcept override { return {}; }

private:
    Radio::RadioAddress _address{};
    Radio::RadioContentionDomainId _domain{};
    bool _started{false};
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
    FakeRadio radioA(0xA1, 1);
    FakeRadio radioB(0xB1, 1);
    FakeRadio radioC(0xC1, 2);

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

    assert(radios.Remove(radioA));
    Mesh::RadioIdentifier idC = 0;
    assert(radios.Register(radioC, idC) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(idC == 3);
    assert(radios.Resolve(idA) == nullptr);
    assert(radios.Resolve(idC) == &radioC);

    Mesh::PendingNeighbourCandidateTable<2> candidates;
    Mesh::NeighbourDiscoveryCoordinator<2, 2> discovery{radios, candidates};

    Radio::RadioInboundTransferHandle transfer{
        &radioC,
        Radio::RadioPeerHandle{3, 7},
        radioC.LocalAddress(),
        1,
        Radio::RadioServiceClass::BestEffort
    };

    Mesh::NeighbourCandidateHandle candidate{};
    const Mesh::UntrustedMembershipClaim claim{Device(1), Incarnation(1)};
    assert(discovery.ObserveClaim(transfer, claim, 100, candidate) == Mesh::NeighbourDiscoveryResult::Inserted);
    assert(candidate);
    const auto* pending = candidates.Resolve(candidate);
    assert(pending != nullptr);
    assert(pending->Radio == idC);
    assert(pending->Peer == transfer.DirectPeer);
    assert(pending->Claim.Device == claim.Device);

    Mesh::NeighbourCandidateHandle refreshed{};
    assert(discovery.ObserveClaim(transfer, claim, 120, refreshed) == Mesh::NeighbourDiscoveryResult::Refreshed);
    assert(refreshed == candidate);

    Radio::RadioInboundTransferHandle unregistered = transfer;
    unregistered.Provider = &radioA;
    Mesh::NeighbourCandidateHandle invalid{};
    assert(discovery.ObserveClaim(unregistered, claim, 130, invalid) ==
           Mesh::NeighbourDiscoveryResult::RadioNotRegistered);
    assert(!invalid);

    Radio::RadioInboundTransferHandle noPeer = transfer;
    noPeer.DirectPeer = {};
    assert(discovery.ObserveClaim(noPeer, claim, 131, invalid) == Mesh::NeighbourDiscoveryResult::InvalidPeer);

    radios.ResetForNewIncarnation();
    Mesh::RadioIdentifier newIncarnationId = 0;
    assert(radios.Register(radioA, newIncarnationId) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(newIncarnationId == 1);

    return 0;
}
