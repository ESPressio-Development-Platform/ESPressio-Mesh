#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_MeshV1BroadcastCoordinator.hpp>

using namespace ESPressio;

namespace {

System::DeviceIdentifier Device(std::uint8_t value) {
    System::DeviceIdentifier::Storage bytes{};
    bytes.back() = value;
    return System::DeviceIdentifier{bytes};
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes.back() = value;
    return Mesh::MembershipIncarnation{bytes};
}

Mesh::MeshIdentifier MeshId(std::uint8_t value) {
    Mesh::MeshIdentifier::Storage bytes{};
    bytes.back() = value;
    return Mesh::MeshIdentifier{bytes};
}

Mesh::MeshSecuritySessionIdentifier SessionId(std::uint8_t value) {
    Mesh::MeshSecuritySessionIdentifier result{};
    result.Value.fill(value);
    return result;
}

class Provider final : public Mesh::IMeshV1CryptographicProvider {
    std::array<bool, 8> _sessions{};

    static Mesh::MeshAuthenticationTag Tag(
        Mesh::MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence,
        const std::uint8_t* aad,
        std::size_t aadBytes,
        const std::uint8_t* ciphertext,
        std::size_t ciphertextBytes
    ) noexcept {
        Mesh::MeshAuthenticationTag tag{};
        tag.Value[0] = static_cast<std::uint8_t>(purpose);
        for (std::size_t index = 0U; index < 8U; ++index) {
            tag.Value[index + 1U] ^= static_cast<std::uint8_t>(sequence >> (index * 8U));
        }
        for (std::size_t index = 0U; index < aadBytes; ++index) {
            tag.Value[index % tag.Value.size()] ^= aad[index];
        }
        for (std::size_t index = 0U; index < ciphertextBytes; ++index) {
            tag.Value[(index + 5U) % tag.Value.size()] ^= ciphertext[index];
        }
        return tag;
    }

public:
    bool PermitIdentityVerification{true};

    Mesh::MeshSecuritySessionHandle CreateSession(std::size_t slot) noexcept {
        assert(slot != 0U && slot < _sessions.size() && !_sessions[slot]);
        _sessions[slot] = true;
        return {static_cast<std::uint16_t>(slot), 1U};
    }

    bool GenerateEphemeralKey(
        Mesh::MeshEphemeralKeyHandle&, Mesh::MeshEphemeralPublicKey&) noexcept override { return false; }
    bool GenerateHandshakeNonce(Mesh::MeshHandshakeNonce&) noexcept override { return false; }
    bool Hash(
        const std::uint8_t* bytes,
        std::size_t size,
        Mesh::MeshSecurityDigest& digest
    ) noexcept override {
        if (bytes == nullptr || size == 0U) return false;
        digest = {};
        for (std::size_t index = 0U; index < size; ++index) {
            digest.Value[index % digest.Value.size()] ^=
                static_cast<std::uint8_t>(bytes[index] + static_cast<std::uint8_t>(index));
        }
        digest.Value[0] ^= 0xA5U;
        return true;
    }
    bool SignIdentityDigest(
        const System::DeviceIdentifier& localDevice,
        const Mesh::MeshSecurityDigest& digest,
        Mesh::MeshIdentitySignature& signature
    ) noexcept override {
        if (!localDevice || !digest) return false;
        signature = {};
        std::memcpy(signature.Value.data(), digest.Value.data(), digest.Value.size());
        std::memcpy(signature.Value.data() + digest.Value.size(),
                    localDevice.Bytes().data(), localDevice.Bytes().size());
        signature.Value.back() = 0x5AU;
        return true;
    }
    Mesh::MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(
        const System::DeviceIdentifier& claimedDevice,
        const Mesh::MeshSecurityDigest& digest,
        const Mesh::MeshIdentitySignature& signature
    ) noexcept override {
        if (!PermitIdentityVerification) return Mesh::MeshIdentityVerificationResult::ResourceUnavailable;
        Mesh::MeshIdentitySignature expected{};
        if (!SignIdentityDigest(claimedDevice, digest, expected)) {
            return Mesh::MeshIdentityVerificationResult::Invalid;
        }
        return expected.Value == signature.Value
            ? Mesh::MeshIdentityVerificationResult::Verified
            : Mesh::MeshIdentityVerificationResult::InvalidSignature;
    }
    bool DeriveSession(
        Mesh::MeshEphemeralKeyHandle, const Mesh::MeshEphemeralPublicKey&, const Mesh::MeshIdentifier&,
        const Mesh::MeshSecurityChannelBinding&, const System::DeviceIdentifier&,
        const Mesh::MembershipIncarnation&, const Mesh::MeshHandshakeNonce&,
        const System::DeviceIdentifier&, const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&, const Mesh::MeshSecurityDigest&,
        Mesh::MeshSecuritySessionRole, Mesh::MeshSecuritySessionHandle&,
        Mesh::MeshSecuritySessionIdentifier&) noexcept override { return false; }
    bool Seal(
        Mesh::MeshSecuritySessionHandle session,
        Mesh::MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence,
        const std::uint8_t* aad,
        std::size_t aadBytes,
        const std::uint8_t* plaintext,
        std::size_t plaintextBytes,
        std::uint8_t* ciphertext,
        Mesh::MeshAuthenticationTag& tag
    ) noexcept override {
        if (!session || session.Slot >= _sessions.size() || !_sessions[session.Slot] ||
            sequence == 0U || aad == nullptr || aadBytes == 0U || plaintext == nullptr ||
            ciphertext == nullptr || plaintextBytes == 0U) return false;
        std::memcpy(ciphertext, plaintext, plaintextBytes);
        tag = Tag(purpose, sequence, aad, aadBytes, ciphertext, plaintextBytes);
        return true;
    }
    bool Open(
        Mesh::MeshSecuritySessionHandle session,
        Mesh::MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence,
        const std::uint8_t* aad,
        std::size_t aadBytes,
        const std::uint8_t* ciphertext,
        std::size_t ciphertextBytes,
        const Mesh::MeshAuthenticationTag& tag,
        std::uint8_t* plaintext
    ) noexcept override {
        if (!session || session.Slot >= _sessions.size() || !_sessions[session.Slot] ||
            sequence == 0U || aad == nullptr || aadBytes == 0U || ciphertext == nullptr ||
            plaintext == nullptr || ciphertextBytes == 0U ||
            Tag(purpose, sequence, aad, aadBytes, ciphertext, ciphertextBytes).Value != tag.Value) return false;
        std::memcpy(plaintext, ciphertext, ciphertextBytes);
        return true;
    }
    bool ReleaseEphemeralKey(Mesh::MeshEphemeralKeyHandle) noexcept override { return false; }
    bool ReleaseSession(Mesh::MeshSecuritySessionHandle session) noexcept override {
        if (!session || session.Slot >= _sessions.size() || !_sessions[session.Slot]) return false;
        _sessions[session.Slot] = false;
        return true;
    }
    void ResetForControlledShutdown() noexcept override { _sessions = {}; }
};

class FakeRadio final : public Radio::IRadio {
    Radio::RadioObserverSubscriptions _observers{};
    Radio::RadioAddress _local;
    bool _started{false};

public:
    std::array<std::uint8_t, 512> LastPhysicalPacket{};
    std::size_t LastPhysicalPacketBytes{0U};
    std::size_t Sends{0U};

    explicit FakeRadio(std::uint8_t address) noexcept :
        _local(Radio::RadioAddress::FromBytes(&address, 1U)) {}
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::None, 512U, 1U,
                static_cast<std::uint16_t>(LastPhysicalPacket.size())};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(
        const Radio::RadioAddress&,
        const std::uint8_t* payload,
        std::size_t payloadBytes
    ) override {
        assert(payload != nullptr && payloadBytes <= LastPhysicalPacket.size());
        std::memcpy(LastPhysicalPacket.data(), payload, payloadBytes);
        LastPhysicalPacketBytes = payloadBytes;
        ++Sends;
        return Radio::RadioSendResult::Accepted();
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
};

class Receiver final : public Mesh::IPrimitiveReceiver {
public:
    Mesh::PrimitiveReceiveDisposition Next{Mesh::PrimitiveReceiveDisposition::Accepted};
    Mesh::MeshReceiveContext LastContext{};
    std::array<std::uint8_t, 16> LastPayload{};
    std::size_t LastPayloadBytes{0U};
    std::size_t Calls{0U};

    Mesh::PrimitiveReceiveDisposition Receive(
        const Mesh::MeshReceiveContext& context,
        Primitive::PrimitiveProtocolVersion version,
        Mesh::PrimitivePayloadView payload
    ) noexcept override {
        assert(version == 1U && payload.Size <= LastPayload.size());
        LastContext = context;
        LastPayloadBytes = payload.Size;
        std::memcpy(LastPayload.data(), payload.Data, payload.Size);
        ++Calls;
        return Next;
    }
};

template<std::size_t MembershipCapacity, std::size_t SessionCapacity>
void AddPeer(
    Mesh::AuthenticatedMembershipTable<MembershipCapacity>& memberships,
    Mesh::MeshSecuritySessionTable<SessionCapacity>& sessions,
    Provider& provider,
    const System::DeviceIdentifier& device,
    const Mesh::MembershipIncarnation& incarnation,
    std::uint8_t sessionValue,
    std::size_t providerSlot
) {
    assert(memberships.UpsertAuthenticated(
        device, incarnation, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) ==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);
    Mesh::MeshSecuritySessionRecordHandle handle{};
    assert(sessions.Install(
        device, incarnation, SessionId(sessionValue), provider.CreateSession(providerSlot), provider, handle));
}

Radio::RadioPeerHandle AddBinding(
    Radio::RadioTransport& transport,
    FakeRadio& radio,
    Mesh::AuthenticatedDirectPeerBindingTable<4>& bindings,
    const System::DeviceIdentifier& device,
    const Mesh::MembershipIncarnation& incarnation,
    std::uint8_t address
) {
    Radio::RadioPeerHandle peer{};
    assert(transport.Peers().Observe(
        radio, Radio::RadioAddress::FromBytes(&address, 1U), peer) ==
        Radio::RadioPeerObserveResult::Observed);
    assert(bindings.Bind({device, incarnation, 1U, peer}) == Mesh::DirectPeerBindingResult::Bound);
    return peer;
}

template<std::size_t Capacity>
void RegisterReceiver(Mesh::PrimitiveReceiverRegistry<Capacity>& registry, Receiver& receiver) {
    Mesh::PrimitiveReceiverHandle handle{};
    assert(registry.Register(
        {Primitive::FamilyIds::Event, {1U, 1U}, {}, Mesh::PrimitiveReceiverExposure::Advertised},
        receiver, handle) == Mesh::PrimitiveReceiverRegistrationResult::Registered);
}

} // namespace

int main() {
    constexpr std::size_t RadioHeaderBytes = 11U;
    const auto mesh = MeshId(9U);
    const auto a = Device(1U);
    const auto b = Device(2U);
    const auto c = Device(3U);
    const auto ia = Incarnation(1U);
    const auto ib = Incarnation(2U);
    const auto ic = Incarnation(3U);

    Mesh::AuthenticatedMembershipTable<2> membersA;
    Mesh::AuthenticatedMembershipTable<2> membersB;
    Mesh::AuthenticatedMembershipTable<2> membersC;
    Provider providerA;
    Provider providerB;
    Provider providerC;
    Mesh::MeshSecuritySessionTable<2> sessionsA;
    Mesh::MeshSecuritySessionTable<2> sessionsB;
    Mesh::MeshSecuritySessionTable<2> sessionsC;
    AddPeer(membersA, sessionsA, providerA, b, ib, 0xABU, 1U);
    AddPeer(membersA, sessionsA, providerA, c, ic, 0xCAU, 2U);
    AddPeer(membersB, sessionsB, providerB, a, ia, 0xABU, 1U);
    AddPeer(membersB, sessionsB, providerB, c, ic, 0xBCU, 2U);
    AddPeer(membersC, sessionsC, providerC, b, ib, 0xBCU, 1U);
    AddPeer(membersC, sessionsC, providerC, a, ia, 0xCAU, 2U);

    FakeRadio radioA(1U);
    FakeRadio radioB(2U);
    FakeRadio radioC(3U);
    Radio::RadioTransport transportA;
    Radio::RadioTransport transportB;
    Radio::RadioTransport transportC;
    assert(transportA.AddInterface(radioA) && transportA.Start());
    assert(transportB.AddInterface(radioB) && transportB.Start());
    assert(transportC.AddInterface(radioC) && transportC.Start());
    Mesh::AuthenticatedDirectPeerBindingTable<4> bindingsA;
    Mesh::AuthenticatedDirectPeerBindingTable<4> bindingsB;
    Mesh::AuthenticatedDirectPeerBindingTable<4> bindingsC;
    const auto peerAB = AddBinding(transportA, radioA, bindingsA, b, ib, 12U);
    const auto peerBA = AddBinding(transportB, radioB, bindingsB, a, ia, 21U);
    const auto peerBC = AddBinding(transportB, radioB, bindingsB, c, ic, 23U);
    const auto peerCB = AddBinding(transportC, radioC, bindingsC, b, ib, 32U);
    const auto peerCA = AddBinding(transportC, radioC, bindingsC, a, ia, 31U);

    Mesh::MeshBroadcastFanoutPlan<2> planA;
    Mesh::MeshBroadcastFanoutPlan<2> planB;
    Mesh::MeshBroadcastFanoutPlan<2> planC;
    assert(planA.TryAdd({b, ib, 1U, peerAB}));
    assert(planB.TryAdd({a, ia, 1U, peerBA}));
    assert(planB.TryAdd({c, ic, 1U, peerBC}));
    assert(planC.TryAdd({b, ib, 1U, peerCB}));
    assert(planC.TryAdd({a, ia, 1U, peerCA}));
    assert(!planC.TryAdd({a, ia, 1U, peerCA}));

    Mesh::PrimitiveReceiverRegistry<1> receiversA;
    Mesh::PrimitiveReceiverRegistry<1> receiversB;
    Mesh::PrimitiveReceiverRegistry<1> receiversC;
    Receiver receiverA;
    Receiver receiverB;
    Receiver receiverC;
    receiverC.Next = Mesh::PrimitiveReceiveDisposition::TemporarilyUnavailable;
    RegisterReceiver(receiversA, receiverA);
    RegisterReceiver(receiversB, receiverB);
    RegisterReceiver(receiversC, receiverC);
    Mesh::DefaultMeshTrafficGovernor trafficA;
    Mesh::DefaultMeshTrafficGovernor trafficB;
    Mesh::DefaultMeshTrafficGovernor trafficC;
    Mesh::MeshV1FrameWorkspace<256, 512> workspaceA;
    Mesh::MeshV1FrameWorkspace<256, 512> workspaceB;
    Mesh::MeshV1FrameWorkspace<256, 512> workspaceC;
    Mesh::MeshMessageIdGenerator messageIdsA;
    Mesh::MeshMessageIdGenerator messageIdsB;
    Mesh::MeshMessageIdGenerator messageIdsC;

    using Coordinator = Mesh::MeshV1BroadcastCoordinator<256, 512, 2, 2, 4, 1, 2>;
    Coordinator coordinatorA(
        membersA, bindingsA, sessionsA, providerA, receiversA, transportA, trafficA,
        workspaceA, messageIdsA, mesh, a, ia);
    Coordinator coordinatorB(
        membersB, bindingsB, sessionsB, providerB, receiversB, transportB, trafficB,
        workspaceB, messageIdsB, mesh, b, ib);
    Coordinator coordinatorC(
        membersC, bindingsC, sessionsC, providerC, receiversC, transportC, trafficC,
        workspaceC, messageIdsC, mesh, c, ic);

    const std::array<std::uint8_t, 4> payload{{4U, 3U, 2U, 1U}};
    auto resultA = coordinatorA.Submit(
        {Primitive::FamilyIds::Event, 1U}, Mesh::ApplicationPayload::Borrowed(payload.data(), payload.size()),
        100U, 200U, 3U, planA);
    assert(resultA.Disposition == Mesh::MeshV1BroadcastDisposition::Completed);
    assert(resultA.MessageId == 1U && resultA.FanoutAttempted == 1U && resultA.FanoutAccepted == 1U);
    assert(receiverA.Calls == 1U && receiverA.LastContext.Broadcast && receiverA.LastContext.RemainingHops == 3U);
    assert(radioA.LastPhysicalPacketBytes > RadioHeaderBytes);

    auto resultB = coordinatorB.Receive(
        radioA.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioA.LastPhysicalPacketBytes - RadioHeaderBytes, 101U, planB);
    assert(resultB.Disposition == Mesh::MeshV1BroadcastDisposition::Completed);
    assert(resultB.MessageId == 1U && resultB.FanoutAttempted == 1U && resultB.FanoutAccepted == 1U);
    assert(receiverB.Calls == 1U && receiverB.LastContext.Broadcast && receiverB.LastContext.RemainingHops == 3U);
    assert(std::memcmp(receiverB.LastPayload.data(), payload.data(), payload.size()) == 0);

    // Application saturation and origin-verification pressure commit neither replay nor Broadcast deduplication.
    std::array<Mesh::MeshTrafficReservation, Mesh::Limits::ApplicationTransmissionCapacity> saturation{};
    for (auto& reservation : saturation) {
        assert(trafficC.TryAcquire(Mesh::MeshTrafficClass::Application, reservation) ==
               Mesh::MeshTrafficAdmissionResult::Admitted);
    }
    auto resultC = coordinatorC.Receive(
        radioB.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioB.LastPhysicalPacketBytes - RadioHeaderBytes, 102U, planC);
    assert(resultC.Disposition == Mesh::MeshV1BroadcastDisposition::ResourceUnavailable);
    for (const auto reservation : saturation) assert(trafficC.Release(reservation));
    providerC.PermitIdentityVerification = false;
    resultC = coordinatorC.Receive(
        radioB.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioB.LastPhysicalPacketBytes - RadioHeaderBytes, 102U, planC);
    assert(resultC.Disposition == Mesh::MeshV1BroadcastDisposition::ResourceUnavailable);
    providerC.PermitIdentityVerification = true;
    resultC = coordinatorC.Receive(
        radioB.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioB.LastPhysicalPacketBytes - RadioHeaderBytes, 102U, planC);
    assert(resultC.Disposition == Mesh::MeshV1BroadcastDisposition::Completed);
    assert(resultC.FanoutAttempted == 1U && resultC.FanoutAccepted == 1U);
    assert(receiverC.Calls == 1U && receiverC.LastContext.RemainingHops == 2U);
    assert(resultC.ReceiverDisposition == Mesh::PrimitiveReceiveDisposition::TemporarilyUnavailable);

    // The successful receive commits both Hop replay and source-scoped Broadcast deduplication.
    const auto replay = coordinatorC.Receive(
        radioB.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioB.LastPhysicalPacketBytes - RadioHeaderBytes, 103U, planC);
    assert(replay.Disposition == Mesh::MeshV1BroadcastDisposition::ReplayRejected);
    assert(receiverC.Calls == 1U);

    // A cycle back to the origin is authenticated and dropped without local redispatch or further fan-out.
    const auto loop = coordinatorA.Receive(
        radioC.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioC.LastPhysicalPacketBytes - RadioHeaderBytes, 103U, planA);
    assert(loop.Disposition == Mesh::MeshV1BroadcastDisposition::Duplicate);
    assert(receiverA.Calls == 1U && radioA.Sends == 1U);

    // The signed immutable deadline suppresses both local delivery and onward fan-out at a late relay.
    resultA = coordinatorA.Submit(
        {Primitive::FamilyIds::Event, 1U}, Mesh::ApplicationPayload::Borrowed(payload.data(), payload.size()),
        150U, 200U, 3U, planA);
    assert(resultA.Disposition == Mesh::MeshV1BroadcastDisposition::Completed && resultA.MessageId == 2U);
    const auto late = coordinatorB.Receive(
        radioA.LastPhysicalPacket.data() + RadioHeaderBytes,
        radioA.LastPhysicalPacketBytes - RadioHeaderBytes, 200U, planB);
    assert(late.Disposition == Mesh::MeshV1BroadcastDisposition::DeadlineExpired);
    assert(receiverB.Calls == 1U && radioB.Sends == 1U);

    assert(trafficA.Active(Mesh::MeshTrafficClass::Application) == 0U);
    assert(trafficB.Active(Mesh::MeshTrafficClass::Application) == 0U);
    assert(trafficC.Active(Mesh::MeshTrafficClass::Application) == 0U);

    return 0;
}
