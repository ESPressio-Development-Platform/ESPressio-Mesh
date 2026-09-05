#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_MeshV1ResponderAdmissionCoordinator.hpp>

using namespace ESPressio;

static System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{}; bytes.back() = tail;
    return System::DeviceIdentifier{bytes};
}
static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    Mesh::MembershipIncarnation::Storage bytes{}; bytes.back() = tail;
    return Mesh::MembershipIncarnation{bytes};
}
static Mesh::MeshIdentifier MeshId(std::uint8_t tail) {
    Mesh::MeshIdentifier::Storage bytes{}; bytes.back() = tail;
    return Mesh::MeshIdentifier{bytes};
}

class Provider final : public Mesh::IMeshV1CryptographicProvider {
    std::array<bool, 8> _ephemeral{};
    std::array<bool, 8> _sessions{};
    std::uint16_t _nextEphemeral{1U};
    std::uint16_t _nextSession{1U};

    static Mesh::MeshAuthenticationTag Tag(const std::uint8_t* aad, std::size_t bytes) {
        Mesh::MeshAuthenticationTag tag{};
        for (std::size_t index = 0; index < tag.Value.size(); ++index) {
            tag.Value[index] = static_cast<std::uint8_t>(0xA5U ^ (bytes == 0U ? 0U : aad[index % bytes]));
        }
        return tag;
    }
public:
    std::size_t ReleasedSessions{0U};
    bool VerifySignatures{true};

    static Mesh::MeshAuthenticationTag Confirmation(const Mesh::MeshSecurityDigest& digest) {
        return Tag(digest.Value.data(), digest.Value.size());
    }

    Mesh::MeshSecuritySessionHandle CreateSessionForTest() {
        assert(_nextSession < _sessions.size());
        const Mesh::MeshSecuritySessionHandle handle{_nextSession, 1U};
        _sessions[_nextSession] = true;
        ++_nextSession;
        return handle;
    }

    bool GenerateEphemeralKey(
        Mesh::MeshEphemeralKeyHandle& handle,
        Mesh::MeshEphemeralPublicKey& publicKey
    ) noexcept override {
        if (_nextEphemeral >= _ephemeral.size()) return false;
        handle = {_nextEphemeral, 1U};
        _ephemeral[_nextEphemeral] = true;
        publicKey.Value.fill(static_cast<std::uint8_t>(_nextEphemeral + 10U));
        publicKey.Value[0] = 0x04U;
        ++_nextEphemeral;
        return true;
    }
    bool GenerateHandshakeNonce(Mesh::MeshHandshakeNonce& nonce) noexcept override {
        nonce.Value.fill(0x33U); return true;
    }
    bool Hash(const std::uint8_t* bytes, std::size_t size, Mesh::MeshSecurityDigest& digest) noexcept override {
        if (bytes == nullptr && size != 0U) return false;
        for (std::size_t index = 0; index < digest.Value.size(); ++index) {
            digest.Value[index] = static_cast<std::uint8_t>(index + 1U);
        }
        for (std::size_t index = 0; index < size; ++index) digest.Value[index % digest.Value.size()] ^= bytes[index];
        if (!digest) digest.Value[0] = 1U;
        return true;
    }
    bool SignIdentityDigest(
        const System::DeviceIdentifier&,
        const Mesh::MeshSecurityDigest& digest,
        Mesh::MeshIdentitySignature& signature
    ) noexcept override {
        for (std::size_t index = 0; index < signature.Value.size(); ++index) {
            signature.Value[index] = static_cast<std::uint8_t>(digest.Value[index % digest.Value.size()] ^ 0x5AU);
        }
        return true;
    }
    Mesh::MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(
        const System::DeviceIdentifier&,
        const Mesh::MeshSecurityDigest& digest,
        const Mesh::MeshIdentitySignature& signature
    ) noexcept override {
        Mesh::MeshIdentitySignature expected{};
        SignIdentityDigest({}, digest, expected);
        return VerifySignatures && expected.Value == signature.Value
            ? Mesh::MeshIdentityVerificationResult::Verified
            : Mesh::MeshIdentityVerificationResult::InvalidSignature;
    }
    bool DeriveSession(
        Mesh::MeshEphemeralKeyHandle ephemeral,
        const Mesh::MeshEphemeralPublicKey&,
        const Mesh::MeshIdentifier&,
        const Mesh::MeshSecurityChannelBinding& channel,
        const System::DeviceIdentifier&,
        const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&,
        const System::DeviceIdentifier&,
        const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&,
        const Mesh::MeshSecurityDigest& transcript,
        Mesh::MeshSecuritySessionRole,
        Mesh::MeshSecuritySessionHandle& session,
        Mesh::MeshSecuritySessionIdentifier& identifier
    ) noexcept override {
        if (!ephemeral || ephemeral.Slot >= _ephemeral.size() || !_ephemeral[ephemeral.Slot] ||
            _nextSession >= _sessions.size()) return false;
        session = {_nextSession, 1U}; _sessions[_nextSession] = true;
        for (std::size_t index = 0; index < identifier.Value.size(); ++index) {
            identifier.Value[index] = static_cast<std::uint8_t>(transcript.Value[index] ^ channel.Value[index]);
        }
        if (!identifier) identifier.Value[0] = 1U;
        ++_nextSession;
        return true;
    }
    bool Seal(
        Mesh::MeshSecuritySessionHandle session, Mesh::MeshSecurityTrafficPurpose,
        std::uint64_t sequence, const std::uint8_t* aad, std::size_t aadBytes,
        const std::uint8_t*, std::size_t plaintextBytes, std::uint8_t*,
        Mesh::MeshAuthenticationTag& tag
    ) noexcept override {
        if (!session || session.Slot >= _sessions.size() || !_sessions[session.Slot] ||
            sequence != 1U || plaintextBytes != 0U) return false;
        tag = Tag(aad, aadBytes); return true;
    }
    bool Open(
        Mesh::MeshSecuritySessionHandle session, Mesh::MeshSecurityTrafficPurpose,
        std::uint64_t sequence, const std::uint8_t* aad, std::size_t aadBytes,
        const std::uint8_t*, std::size_t ciphertextBytes,
        const Mesh::MeshAuthenticationTag& tag, std::uint8_t*
    ) noexcept override {
        return session && session.Slot < _sessions.size() && _sessions[session.Slot] &&
               sequence == 1U && ciphertextBytes == 0U && Tag(aad, aadBytes).Value == tag.Value;
    }
    bool ReleaseEphemeralKey(Mesh::MeshEphemeralKeyHandle handle) noexcept override {
        if (!handle || handle.Slot >= _ephemeral.size() || !_ephemeral[handle.Slot]) return false;
        _ephemeral[handle.Slot] = false; return true;
    }
    bool ReleaseSession(Mesh::MeshSecuritySessionHandle handle) noexcept override {
        if (!handle || handle.Slot >= _sessions.size() || !_sessions[handle.Slot]) return false;
        _sessions[handle.Slot] = false; ++ReleasedSessions; return true;
    }
    void ResetForControlledShutdown() noexcept override { _ephemeral = {}; _sessions = {}; }
};

class Admission final : public Mesh::IMeshAdmissionPolicy {
public:
    Mesh::MeshAdmissionDisposition Next{Mesh::MeshAdmissionDisposition::Admit};
    Mesh::MeshAdmissionDisposition EvaluateAdmission(const Mesh::MeshAdmissionContext& context) const noexcept override {
        assert(context.IsValid()); return Next;
    }
};

template<std::size_t CandidateCapacity>
static Mesh::NeighbourCandidateHandle Observe(
    Mesh::PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
    std::uint8_t peer,
    std::uint8_t identity
) {
    Mesh::NeighbourCandidateHandle handle{};
    assert(candidates.Observe(
        1U, Radio::RadioPeerHandle{peer, 1U}, {Device(identity), Incarnation(identity)}, 10U, handle
    ) == Mesh::PendingCandidateInsertResult::Inserted);
    return handle;
}

static std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes> InitiatorPacket(
    Provider& provider,
    const Mesh::MeshIdentifier& mesh,
    const System::DeviceIdentifier& device,
    const Mesh::MembershipIncarnation& incarnation
) {
    Mesh::MeshV1InitiatorHello hello{};
    hello.Mesh = mesh; hello.Device = device; hello.Incarnation = incarnation;
    hello.EphemeralPublicKey.Value.fill(0x44U); hello.EphemeralPublicKey.Value[0] = 0x04U;
    hello.Nonce.Value.fill(0x55U);
    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::HeaderBytes +
        Mesh::MeshV1SecurityHandshakeCodec::InitiatorUnsignedBodyBytes> unsignedPacket{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeInitiatorUnsigned(
        hello, unsignedPacket.data(), unsignedPacket.size()));
    Mesh::MeshSecurityDigest digest{};
    assert(provider.Hash(unsignedPacket.data(), unsignedPacket.size(), digest));
    assert(provider.SignIdentityDigest(device, digest, hello.Signature));
    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes> packet{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeInitiator(hello, packet.data(), packet.size()));
    return packet;
}

static std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::FinishPacketBytes> FinishPacket(
    Provider& provider,
    const std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes>& initiator,
    const std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::ResponderPacketBytes>& responder
) {
    constexpr std::size_t SignedResponderBytes = Mesh::MeshV1SecurityHandshakeCodec::ResponderPacketBytes -
        Mesh::MeshV1SecuritySuite::AuthenticationTagBytes;
    std::array<std::uint8_t,
        Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes + SignedResponderBytes> transcript{};
    for (std::size_t index = 0; index < initiator.size(); ++index) transcript[index] = initiator[index];
    for (std::size_t index = 0; index < SignedResponderBytes; ++index) transcript[initiator.size() + index] = responder[index];
    Mesh::MeshV1InitiatorFinish finish{};
    assert(provider.Hash(transcript.data(), transcript.size(), finish.HandshakeTranscriptDigest));
    finish.ConfirmationTag = Provider::Confirmation(finish.HandshakeTranscriptDigest);
    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::FinishPacketBytes> packet{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeFinish(finish, packet.data(), packet.size()));
    return packet;
}

int main() {
    constexpr std::size_t Candidates = 3U;
    Mesh::PendingNeighbourCandidateTable<Candidates> candidates;
    Mesh::InboundAuthenticationReservationTable<1> authentications;
    Mesh::AuthenticatedMembershipTable<2> memberships;
    Mesh::AdmissionPromotionCoordinator<Candidates, 1, 2> promotion(candidates, authentications, memberships);
    Mesh::MeshSecuritySessionTable<2> sessions;
    Provider provider;
    Admission admission;
    Mesh::MeshSecurityChannelBinding channel{}; channel.Value.fill(0x66U);
    const auto mesh = MeshId(1U);
    Mesh::MeshV1ResponderAdmissionCoordinator<Candidates, 1, 2, 2> coordinator(
        candidates, authentications, memberships, promotion, sessions, provider, admission,
        mesh, channel, Device(9U), Incarnation(9U), 100U
    );

    const auto candidate = Observe(candidates, 0U, 2U);
    assert(coordinator.Begin(candidate, 20U) == Mesh::MeshV1ResponderBeginResult::Started);
    const auto context = Mesh::MeshSecurityCandidateContext{
        candidate, 1U, Radio::RadioPeerHandle{0U, 1U}, {Device(2U), Incarnation(2U)}
    };
    const auto initiator = InitiatorPacket(provider, mesh, Device(2U), Incarnation(2U));
    assert(coordinator.AcceptInitiatorHello(context, initiator.data(), initiator.size()) ==
        Mesh::MeshV1ResponderHelloResult::ResponderReady);
    assert(coordinator.AcceptInitiatorHello(context, initiator.data(), initiator.size()) ==
        Mesh::MeshV1ResponderHelloResult::AlreadyReady);
    auto unrelatedRetransmission = initiator; unrelatedRetransmission[20] ^= 1U;
    assert(coordinator.AcceptInitiatorHello(
        context, unrelatedRetransmission.data(), unrelatedRetransmission.size()) ==
        Mesh::MeshV1ResponderHelloResult::Invalid);
    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::ResponderPacketBytes> responder{};
    assert(coordinator.CopyResponderHello(candidate, responder.data(), responder.size()));
    const auto finish = FinishPacket(provider, initiator, responder);
    assert(coordinator.AcceptInitiatorFinish(context, finish.data(), finish.size()) ==
        Mesh::MeshV1ResponderFinishResult::Authenticated);
    assert(coordinator.AcceptInitiatorFinish(context, finish.data(), finish.size()) ==
        Mesh::MeshV1ResponderFinishResult::AlreadyAuthenticated);
    auto unrelatedFinish = finish; unrelatedFinish.back() ^= 1U;
    assert(coordinator.AcceptInitiatorFinish(context, unrelatedFinish.data(), unrelatedFinish.size()) ==
        Mesh::MeshV1ResponderFinishResult::Invalid);
    assert(memberships.Empty() && sessions.Size() == 0U);
    Mesh::AuthenticatedDirectPeerBinding established{};
    assert(coordinator.CompleteAdmission(candidate, &established) ==
        Mesh::MeshV1AdmissionResult::PromotedToValidating);
    assert(memberships.FindExact(Device(2U), Incarnation(2U)) != nullptr);
    assert(sessions.Find(Device(2U), Incarnation(2U)));
    assert(established.IsValid() && established.Neighbour == Device(2U));
    assert(candidates.Resolve(candidate) == nullptr && !authentications.Contains(candidate));

    // Deferral releases the staged provider session before returning the candidate to Discovered.
    const auto deferred = Observe(candidates, 1U, 3U);
    assert(coordinator.Begin(deferred, 30U) == Mesh::MeshV1ResponderBeginResult::Started);
    const Mesh::MeshSecurityCandidateContext deferredContext{
        deferred, 1U, Radio::RadioPeerHandle{1U, 1U}, {Device(3U), Incarnation(3U)}
    };
    const auto deferredInitiator = InitiatorPacket(provider, mesh, Device(3U), Incarnation(3U));
    assert(coordinator.AcceptInitiatorHello(deferredContext, deferredInitiator.data(), deferredInitiator.size()) ==
        Mesh::MeshV1ResponderHelloResult::ResponderReady);
    assert(coordinator.CopyResponderHello(deferred, responder.data(), responder.size()));
    const auto deferredFinish = FinishPacket(provider, deferredInitiator, responder);
    assert(coordinator.AcceptInitiatorFinish(deferredContext, deferredFinish.data(), deferredFinish.size()) ==
        Mesh::MeshV1ResponderFinishResult::Authenticated);
    const auto releasesBeforeDeferral = provider.ReleasedSessions;
    admission.Next = Mesh::MeshAdmissionDisposition::Defer;
    assert(coordinator.CompleteAdmission(deferred) == Mesh::MeshV1AdmissionResult::AdmissionDeferred);
    assert(provider.ReleasedSessions == releasesBeforeDeferral + 1U);
    assert(candidates.Resolve(deferred)->State == Mesh::MembershipState::Discovered);
    assert(!authentications.Contains(deferred));
    assert(!sessions.Find(Device(3U), Incarnation(3U)));

    // A bad identity signature is terminal and can never mutate authoritative membership/session state.
    const auto rejected = Observe(candidates, 2U, 4U);
    assert(coordinator.Begin(rejected, 40U) == Mesh::MeshV1ResponderBeginResult::Started);
    const Mesh::MeshSecurityCandidateContext rejectedContext{
        rejected, 1U, Radio::RadioPeerHandle{2U, 1U}, {Device(4U), Incarnation(4U)}
    };
    auto badInitiator = InitiatorPacket(provider, mesh, Device(4U), Incarnation(4U));
    badInitiator.back() ^= 1U;
    assert(coordinator.AcceptInitiatorHello(rejectedContext, badInitiator.data(), badInitiator.size()) ==
        Mesh::MeshV1ResponderHelloResult::Rejected);
    assert(candidates.Resolve(rejected) == nullptr && !authentications.Contains(rejected));
    assert(memberships.FindDevice(Device(4U)) == nullptr && !sessions.Find(Device(4U), Incarnation(4U)));

    // Session-table saturation retains the fully authenticated transaction for an exact retry; it does not promote.
    Mesh::MeshSecuritySessionIdentifier occupiedIdentifier{}; occupiedIdentifier.Value.fill(0x77U);
    Mesh::MeshSecuritySessionRecordHandle occupied{};
    assert(sessions.Install(
        Device(8U), Incarnation(8U), occupiedIdentifier, provider.CreateSessionForTest(), provider, occupied));
    admission.Next = Mesh::MeshAdmissionDisposition::Admit;
    const auto saturated = Observe(candidates, 2U, 5U);
    assert(coordinator.Begin(saturated, 50U) == Mesh::MeshV1ResponderBeginResult::Started);
    const Mesh::MeshSecurityCandidateContext saturatedContext{
        saturated, 1U, Radio::RadioPeerHandle{2U, 1U}, {Device(5U), Incarnation(5U)}
    };
    const auto saturatedInitiator = InitiatorPacket(provider, mesh, Device(5U), Incarnation(5U));
    assert(coordinator.AcceptInitiatorHello(saturatedContext, saturatedInitiator.data(), saturatedInitiator.size()) ==
        Mesh::MeshV1ResponderHelloResult::ResponderReady);
    assert(coordinator.CopyResponderHello(saturated, responder.data(), responder.size()));
    const auto saturatedFinish = FinishPacket(provider, saturatedInitiator, responder);
    assert(coordinator.AcceptInitiatorFinish(saturatedContext, saturatedFinish.data(), saturatedFinish.size()) ==
        Mesh::MeshV1ResponderFinishResult::Authenticated);
    assert(coordinator.CompleteAdmission(saturated) ==
        Mesh::MeshV1AdmissionResult::SessionResourceUnavailable);
    assert(authentications.Contains(saturated));
    assert(candidates.Resolve(saturated)->State == Mesh::MembershipState::Authenticating);
    assert(memberships.FindDevice(Device(5U)) == nullptr);
    assert(sessions.Release(occupied, provider));
    assert(coordinator.CompleteAdmission(saturated) ==
        Mesh::MeshV1AdmissionResult::PromotedToValidating);
    assert(memberships.FindExact(Device(5U), Incarnation(5U)) != nullptr);

    // Timeout releases the exact reservation and returns only that candidate to Discovered.
    const auto expired = Observe(candidates, 2U, 6U);
    assert(coordinator.Begin(expired, 60U) == Mesh::MeshV1ResponderBeginResult::Started);
    assert(coordinator.Expire(159U) == 0U);
    assert(coordinator.Expire(160U) == 1U);
    assert(!authentications.Contains(expired));
    assert(candidates.Resolve(expired)->State == Mesh::MembershipState::Discovered);
    return 0;
}
