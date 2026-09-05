#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_MeshV1ApplicationProtectionCoordinator.hpp>

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
        for (std::size_t index = 0; index < 8U; ++index) {
            tag.Value[(index + 1U) % tag.Value.size()] ^=
                static_cast<std::uint8_t>(sequence >> (index * 8U));
        }
        for (std::size_t index = 0; index < aadBytes; ++index) tag.Value[index % tag.Value.size()] ^= aad[index];
        for (std::size_t index = 0; index < ciphertextBytes; ++index) {
            tag.Value[(index + 5U) % tag.Value.size()] ^= ciphertext[index];
        }
        return tag;
    }

public:
    Mesh::MeshSecuritySessionHandle CreateSession(std::size_t slot) noexcept {
        assert(slot < _sessions.size() && slot != 0U && !_sessions[slot]);
        _sessions[slot] = true;
        return {static_cast<std::uint16_t>(slot), 1U};
    }

    bool GenerateEphemeralKey(Mesh::MeshEphemeralKeyHandle&, Mesh::MeshEphemeralPublicKey&) noexcept override {
        return false;
    }
    bool GenerateHandshakeNonce(Mesh::MeshHandshakeNonce&) noexcept override { return false; }
    bool Hash(const std::uint8_t*, std::size_t, Mesh::MeshSecurityDigest&) noexcept override { return false; }
    bool SignIdentityDigest(
        const System::DeviceIdentifier&, const Mesh::MeshSecurityDigest&, Mesh::MeshIdentitySignature&) noexcept override {
        return false;
    }
    Mesh::MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(
        const System::DeviceIdentifier&, const Mesh::MeshSecurityDigest&,
        const Mesh::MeshIdentitySignature&) noexcept override {
        return Mesh::MeshIdentityVerificationResult::InvalidSignature;
    }
    bool DeriveSession(
        Mesh::MeshEphemeralKeyHandle, const Mesh::MeshEphemeralPublicKey&, const Mesh::MeshIdentifier&,
        const Mesh::MeshSecurityChannelBinding&, const System::DeviceIdentifier&,
        const Mesh::MembershipIncarnation&, const Mesh::MeshHandshakeNonce&,
        const System::DeviceIdentifier&, const Mesh::MembershipIncarnation&, const Mesh::MeshHandshakeNonce&,
        const Mesh::MeshSecurityDigest&, Mesh::MeshSecuritySessionRole,
        Mesh::MeshSecuritySessionHandle&, Mesh::MeshSecuritySessionIdentifier&) noexcept override {
        return false;
    }
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
            sequence == 0U || aad == nullptr || aadBytes == 0U ||
            (plaintext == nullptr && plaintextBytes != 0U) ||
            (ciphertext == nullptr && plaintextBytes != 0U)) return false;
        if (plaintextBytes != 0U) std::memcpy(ciphertext, plaintext, plaintextBytes);
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
            sequence == 0U || aad == nullptr || aadBytes == 0U ||
            (ciphertext == nullptr && ciphertextBytes != 0U) ||
            (plaintext == nullptr && ciphertextBytes != 0U) ||
            Tag(purpose, sequence, aad, aadBytes, ciphertext, ciphertextBytes).Value != tag.Value) return false;
        if (ciphertextBytes != 0U) std::memcpy(plaintext, ciphertext, ciphertextBytes);
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
    bool _started{false};
    Radio::RadioAddress _local{Radio::RadioAddress::FromBytes(reinterpret_cast<const std::uint8_t*>("L"), 1U)};
public:
    std::array<std::uint8_t, 512> LastPhysicalPacket{};
    std::size_t LastPhysicalPacketBytes{0U};
    std::size_t Sends{0U};

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::None, 512U, 1U,
                static_cast<std::uint16_t>(LastPhysicalPacket.size())};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(
        const Radio::RadioAddress&, const std::uint8_t* payload, std::size_t payloadBytes) override {
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
    std::array<std::uint8_t, 32> LastPayload{};
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
        if (payload.Size != 0U) std::memcpy(LastPayload.data(), payload.Data, payload.Size);
        ++Calls;
        return Next;
    }
};

class Repeatable final : public Mesh::IRepeatableSerializedPayloadSource {
    const std::uint8_t* _bytes;
    std::size_t _size;
public:
    Repeatable(const std::uint8_t* bytes, std::size_t size) noexcept : _bytes(bytes), _size(size) {}
    std::size_t Size() const noexcept override { return _size; }
    bool Read(std::size_t offset, std::uint8_t* destination, std::size_t length) const noexcept override {
        if (destination == nullptr || offset > _size || length > _size - offset) return false;
        std::memcpy(destination, _bytes + offset, length);
        return true;
    }
};

} // namespace

int main() {
    constexpr std::size_t RadioHeaderBytes = 11U;
    const auto source = Device(1U);
    const auto destination = Device(2U);
    const auto sourceIncarnation = Incarnation(1U);
    const auto destinationIncarnation = Incarnation(2U);
    const auto mesh = MeshId(9U);
    Mesh::MeshSecuritySessionIdentifier sessionIdentifier{};
    sessionIdentifier.Value.fill(0x44U);

    Mesh::AuthenticatedMembershipTable<2> sourceMemberships;
    Mesh::AuthenticatedMembershipTable<2> destinationMemberships;
    assert(sourceMemberships.UpsertAuthenticated(
        destination, destinationIncarnation, Mesh::MembershipState::Active,
        Mesh::ReachabilityState::Reachable) == Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(destinationMemberships.UpsertAuthenticated(
        source, sourceIncarnation, Mesh::MembershipState::Active,
        Mesh::ReachabilityState::Reachable) == Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Provider sourceProvider;
    Provider destinationProvider;
    Mesh::MeshSecuritySessionTable<2> sourceSessions;
    Mesh::MeshSecuritySessionTable<2> destinationSessions;
    Mesh::MeshSecuritySessionRecordHandle sourceSession{};
    Mesh::MeshSecuritySessionRecordHandle destinationSession{};
    assert(sourceSessions.Install(
        destination, destinationIncarnation, sessionIdentifier, sourceProvider.CreateSession(1U),
        sourceProvider, sourceSession));
    assert(destinationSessions.Install(
        source, sourceIncarnation, sessionIdentifier, destinationProvider.CreateSession(1U),
        destinationProvider, destinationSession));

    FakeRadio radio;
    Radio::RadioTransport transport;
    assert(transport.AddInterface(radio) && transport.Start());
    const std::uint8_t remoteAddress = 7U;
    Radio::RadioPeerHandle peer{};
    assert(transport.Peers().Observe(
        radio, Radio::RadioAddress::FromBytes(&remoteAddress, 1U), peer) ==
        Radio::RadioPeerObserveResult::Observed);
    Mesh::AuthenticatedDirectPeerBindingTable<2> bindings;
    assert(bindings.Bind({destination, destinationIncarnation, 1U, peer}) ==
        Mesh::DirectPeerBindingResult::Bound);
    Mesh::ForwardingSubmissionCoordinator<2, 2, 2> forwarding(sourceMemberships, bindings, transport);
    Mesh::TopologyLinkIdentity link{source, 1U, destination, 1U};
    Mesh::ResolvedRoute<2> route;
    assert(route.Assign(source, destination, &link, 1U));

    const std::array<std::uint8_t, 4> payload{{1U, 2U, 3U, 4U}};
    Mesh::ApplicationTransmissionTable<2, 1> transmissions;
    Mesh::ApplicationTransmissionHandle transmission{};
    const Mesh::ApplicationTransmissionRecipient recipient{destination, destinationIncarnation, 101U};
    assert(transmissions.Begin(
        &recipient, 1U, {Primitive::FamilyIds::Event, 1U},
        Mesh::ApplicationPayload::Borrowed(payload.data(), payload.size()), 100U, 200U, transmission) ==
        Mesh::ApplicationTransmissionBeginResult::Begun);

    Mesh::MeshV1FrameWorkspace<256, 512> sourceWorkspace;
    Mesh::MeshV1ProtectedApplicationSubmissionCoordinator<256, 512, 2, 1, 2, 2, 2, 2> protector(
        transmissions, sourceMemberships, sourceSessions, sourceProvider, forwarding, sourceWorkspace,
        mesh, source, sourceIncarnation);
    Mesh::InboundDeliveryReservationTable<2> reservations;
    Mesh::InboundDeliveryCoordinator<2, 2> inbound(destinationMemberships, reservations);
    Mesh::PrimitiveReceiverRegistry<1> receivers;
    Receiver receiver;
    Mesh::PrimitiveReceiverHandle receiverHandle{};
    assert(receivers.Register(
        {Primitive::FamilyIds::Event, {1U, 1U}, {}, Mesh::PrimitiveReceiverExposure::Advertised},
        receiver, receiverHandle) == Mesh::PrimitiveReceiverRegistrationResult::Registered);
    Mesh::MeshV1FrameWorkspace<256, 512> destinationWorkspace;
    Mesh::MeshV1ProtectedDestinationCoordinator<256, 512, 2, 2, 1, 2> opener(
        destinationMemberships, destinationSessions, destinationProvider, inbound, receivers,
        destinationWorkspace, mesh, destination, destinationIncarnation);

    auto submission = protector.SubmitRecipient(transmission, 0U, route, 3U, 110U);
    assert(submission && radio.Sends == 1U && radio.LastPhysicalPacketBytes > RadioHeaderBytes);
    auto received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 110U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::Dispatched);
    assert(receiver.Calls == 1U && receiver.LastPayloadBytes == payload.size());
    assert(std::memcmp(receiver.LastPayload.data(), payload.data(), payload.size()) == 0);
    assert(receiver.LastContext.Source == source && receiver.LastContext.SourceIncarnation == sourceIncarnation);
    assert(receiver.LastContext.DeliveryMessageId == 101U && receiver.LastContext.RemainingHops == 2U);

    // A freshly protected retry consumes new security sequences but is deduplicated by authenticated MessageId.
    submission = protector.SubmitRecipient(transmission, 0U, route, 3U, 111U);
    assert(submission && radio.Sends == 2U);
    received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 111U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::Duplicate);
    assert(receiver.Calls == 1U);

    // Hop authentication failure cannot reach end-to-end replay state, deduplication or family dispatch.
    submission = protector.SubmitRecipient(transmission, 0U, route, 3U, 112U);
    assert(submission && radio.Sends == 3U);
    radio.LastPhysicalPacket[radio.LastPhysicalPacketBytes - 1U] ^= 1U;
    received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 112U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::AuthenticationFailed);
    assert(receiver.Calls == 1U && reservations.Empty());

    // Receiver backpressure releases only semantic reservation; a new protected retry may dispatch the same MessageId.
    const Mesh::ApplicationTransmissionRecipient retryRecipient{destination, destinationIncarnation, 102U};
    Mesh::ApplicationTransmissionHandle retryTransmission{};
    assert(transmissions.Begin(
        &retryRecipient, 1U, {Primitive::FamilyIds::Event, 1U},
        Mesh::ApplicationPayload::Borrowed(payload.data(), payload.size()), 100U, 200U, retryTransmission) ==
        Mesh::ApplicationTransmissionBeginResult::Begun);
    receiver.Next = Mesh::PrimitiveReceiveDisposition::ResourceUnavailable;
    submission = protector.SubmitRecipient(retryTransmission, 0U, route, 3U, 113U);
    assert(submission);
    received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 113U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::RetryableReceiver);
    assert(reservations.Empty() && receiver.Calls == 2U);
    receiver.Next = Mesh::PrimitiveReceiveDisposition::Accepted;
    submission = protector.SubmitRecipient(retryTransmission, 0U, route, 3U, 114U);
    assert(submission);
    received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 114U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::Dispatched);
    assert(receiver.Calls == 3U && reservations.Empty());

    // The authenticated immutable deadline remains authoritative at the destination.
    const Mesh::ApplicationTransmissionRecipient expiredRecipient{destination, destinationIncarnation, 103U};
    Mesh::ApplicationTransmissionHandle expiredTransmission{};
    assert(transmissions.Release(transmission));
    assert(transmissions.Begin(
        &expiredRecipient, 1U, {Primitive::FamilyIds::Event, 1U},
        Mesh::ApplicationPayload::Borrowed(payload.data(), payload.size()), 100U, 150U, expiredTransmission) ==
        Mesh::ApplicationTransmissionBeginResult::Begun);
    submission = protector.SubmitRecipient(expiredTransmission, 0U, route, 3U, 149U);
    assert(submission);
    received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 150U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::DeadlineExpired);
    assert(receiver.Calls == 3U && reservations.Empty());

    // Repeatable immutable sources materialize inside the already-accounted packet workspace, not a plaintext path.
    assert(transmissions.Release(retryTransmission));
    Repeatable repeatable(payload.data(), payload.size());
    const Mesh::ApplicationTransmissionRecipient repeatableRecipient{
        destination, destinationIncarnation, 104U};
    Mesh::ApplicationTransmissionHandle repeatableTransmission{};
    assert(transmissions.Begin(
        &repeatableRecipient, 1U, {Primitive::FamilyIds::Event, 1U},
        Mesh::ApplicationPayload::Repeatable(repeatable), 100U, 250U, repeatableTransmission) ==
        Mesh::ApplicationTransmissionBeginResult::Begun);
    submission = protector.SubmitRecipient(repeatableTransmission, 0U, route, 3U, 160U);
    assert(submission);
    received = opener.Receive(
        radio.LastPhysicalPacket.data() + RadioHeaderBytes,
        radio.LastPhysicalPacketBytes - RadioHeaderBytes, 160U);
    assert(received.Disposition == Mesh::MeshV1ProtectedDestinationDisposition::Dispatched);
    assert(receiver.Calls == 4U && receiver.LastContext.DeliveryMessageId == 104U);
    assert(std::memcmp(receiver.LastPayload.data(), payload.data(), payload.size()) == 0);

    transport.Stop();
    return 0;
}
