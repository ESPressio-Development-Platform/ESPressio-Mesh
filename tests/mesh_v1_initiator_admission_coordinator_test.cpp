#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_MeshV1InitiatorAdmissionCoordinator.hpp>
#include <ESPressio_MeshV1ResponderAdmissionCoordinator.hpp>

using namespace ESPressio;

static System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{};
    bytes.back() = tail;
    return System::DeviceIdentifier{bytes};
}

static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes.back() = tail;
    return Mesh::MembershipIncarnation{bytes};
}

static Mesh::MeshIdentifier MeshId(std::uint8_t tail) {
    Mesh::MeshIdentifier::Storage bytes{};
    bytes.back() = tail;
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
    bool FailNonce{false};
    bool FailNextEphemeralRelease{false};

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
        if (FailNonce) return false;
        nonce.Value.fill(0x33U);
        return true;
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
        return expected.Value == signature.Value
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
        session = {_nextSession, 1U};
        _sessions[_nextSession] = true;
        for (std::size_t index = 0; index < identifier.Value.size(); ++index) {
            identifier.Value[index] = static_cast<std::uint8_t>(transcript.Value[index] ^ channel.Value[index]);
        }
        if (!identifier) identifier.Value[0] = 1U;
        ++_nextSession;
        return true;
    }

    bool Seal(
        Mesh::MeshSecuritySessionHandle session,
        Mesh::MeshSecurityTrafficPurpose,
        std::uint64_t sequence,
        const std::uint8_t* aad,
        std::size_t aadBytes,
        const std::uint8_t*,
        std::size_t plaintextBytes,
        std::uint8_t*,
        Mesh::MeshAuthenticationTag& tag
    ) noexcept override {
        if (!session || session.Slot >= _sessions.size() || !_sessions[session.Slot] ||
            sequence != 1U || plaintextBytes != 0U) return false;
        tag = Tag(aad, aadBytes);
        return true;
    }

    bool Open(
        Mesh::MeshSecuritySessionHandle session,
        Mesh::MeshSecurityTrafficPurpose,
        std::uint64_t sequence,
        const std::uint8_t* aad,
        std::size_t aadBytes,
        const std::uint8_t*,
        std::size_t ciphertextBytes,
        const Mesh::MeshAuthenticationTag& tag,
        std::uint8_t*
    ) noexcept override {
        return session && session.Slot < _sessions.size() && _sessions[session.Slot] &&
               sequence == 1U && ciphertextBytes == 0U && Tag(aad, aadBytes).Value == tag.Value;
    }

    bool ReleaseEphemeralKey(Mesh::MeshEphemeralKeyHandle handle) noexcept override {
        if (FailNextEphemeralRelease) {
            FailNextEphemeralRelease = false;
            return false;
        }
        if (!handle || handle.Slot >= _ephemeral.size() || !_ephemeral[handle.Slot]) return false;
        _ephemeral[handle.Slot] = false;
        return true;
    }

    bool ReleaseSession(Mesh::MeshSecuritySessionHandle handle) noexcept override {
        if (!handle || handle.Slot >= _sessions.size() || !_sessions[handle.Slot]) return false;
        _sessions[handle.Slot] = false;
        return true;
    }

    void ResetForControlledShutdown() noexcept override {
        _ephemeral = {};
        _sessions = {};
    }
};

class Admission final : public Mesh::IMeshAdmissionPolicy {
public:
    Mesh::MeshAdmissionDisposition EvaluateAdmission(const Mesh::MeshAdmissionContext& context) const noexcept override {
        assert(context.IsValid());
        return Mesh::MeshAdmissionDisposition::Admit;
    }
};

template<std::size_t Capacity>
static Mesh::NeighbourCandidateHandle Observe(
    Mesh::PendingNeighbourCandidateTable<Capacity>& candidates,
    std::uint8_t peer,
    std::uint8_t identity
) {
    Mesh::NeighbourCandidateHandle handle{};
    assert(candidates.Observe(
        1U, Radio::RadioPeerHandle{peer, 1U}, {Device(identity), Incarnation(identity)}, 10U, handle
    ) == Mesh::PendingCandidateInsertResult::Inserted);
    return handle;
}

int main() {
    constexpr std::size_t Candidates = 4U;
    constexpr std::size_t Authentications = 2U;
    constexpr std::size_t Members = 4U;
    Mesh::PendingNeighbourCandidateTable<Candidates> candidatesA;
    Mesh::PendingNeighbourCandidateTable<Candidates> candidatesB;
    Mesh::InboundAuthenticationReservationTable<Authentications> authenticationsA;
    Mesh::InboundAuthenticationReservationTable<Authentications> authenticationsB;
    Mesh::AuthenticatedMembershipTable<Members> membershipsA;
    Mesh::AuthenticatedMembershipTable<Members> membershipsB;
    Mesh::AdmissionPromotionCoordinator<Candidates, Authentications, Members> promotionA(
        candidatesA, authenticationsA, membershipsA);
    Mesh::AdmissionPromotionCoordinator<Candidates, Authentications, Members> promotionB(
        candidatesB, authenticationsB, membershipsB);
    Mesh::MeshSecuritySessionTable<Members> sessionsA;
    Mesh::MeshSecuritySessionTable<Members> sessionsB;
    Provider providerA;
    Provider providerB;
    Admission admission;
    Mesh::MeshSecurityChannelBinding channel{};
    channel.Value.fill(0x66U);
    const auto mesh = MeshId(1U);
    Mesh::MeshV1InitiatorAdmissionCoordinator<Candidates, Authentications, Members, Members> initiator(
        candidatesA, authenticationsA, membershipsA, promotionA, sessionsA, providerA, admission,
        mesh, channel, Device(1U), Incarnation(1U), 100U);
    Mesh::MeshV1ResponderAdmissionCoordinator<Candidates, Authentications, Members, Members> responder(
        candidatesB, authenticationsB, membershipsB, promotionB, sessionsB, providerB, admission,
        mesh, channel, Device(2U), Incarnation(2U), 100U);

    const auto candidateA = Observe(candidatesA, 2U, 2U);
    const auto candidateB = Observe(candidatesB, 1U, 1U);
    const Mesh::MeshSecurityCandidateContext contextA{
        candidateA, 1U, Radio::RadioPeerHandle{2U, 1U}, {Device(2U), Incarnation(2U)}};
    const Mesh::MeshSecurityCandidateContext contextB{
        candidateB, 1U, Radio::RadioPeerHandle{1U, 1U}, {Device(1U), Incarnation(1U)}};
    assert(initiator.Begin(candidateA, 20U) == Mesh::MeshV1InitiatorBeginResult::InitiatorReady);
    assert(responder.Begin(candidateB, 20U) == Mesh::MeshV1ResponderBeginResult::Started);

    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes> initiatorHello{};
    assert(initiator.CopyInitiatorHello(candidateA, initiatorHello.data(), initiatorHello.size()));
    assert(responder.AcceptInitiatorHello(contextB, initiatorHello.data(), initiatorHello.size()) ==
        Mesh::MeshV1ResponderHelloResult::ResponderReady);
    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::ResponderPacketBytes> responderHello{};
    assert(responder.CopyResponderHello(candidateB, responderHello.data(), responderHello.size()));
    assert(initiator.AcceptResponderHello(contextA, responderHello.data(), responderHello.size()) ==
        Mesh::MeshV1InitiatorResponderResult::FinishReady);
    assert(initiator.AcceptResponderHello(contextA, responderHello.data(), responderHello.size()) ==
        Mesh::MeshV1InitiatorResponderResult::AlreadyReady);
    auto unrelatedResponder = responderHello;
    unrelatedResponder[20] ^= 1U;
    assert(initiator.AcceptResponderHello(contextA, unrelatedResponder.data(), unrelatedResponder.size()) ==
        Mesh::MeshV1InitiatorResponderResult::Invalid);

    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::FinishPacketBytes> finish{};
    assert(initiator.CopyInitiatorFinish(candidateA, finish.data(), finish.size()));
    assert(initiator.CompleteAdmission(candidateA) == Mesh::MeshV1AdmissionResult::HandshakeNotAuthenticated);
    assert(initiator.MarkInitiatorFinishSubmitted(contextA) ==
        Mesh::MeshV1InitiatorFinishSubmissionResult::Authenticated);
    assert(responder.AcceptInitiatorFinish(contextB, finish.data(), finish.size()) ==
        Mesh::MeshV1ResponderFinishResult::Authenticated);
    assert(membershipsA.Empty() && membershipsB.Empty() && sessionsA.Size() == 0U && sessionsB.Size() == 0U);

    assert(initiator.CompleteAdmission(candidateA) == Mesh::MeshV1AdmissionResult::PromotedToValidating);
    assert(responder.CompleteAdmission(candidateB) == Mesh::MeshV1AdmissionResult::PromotedToValidating);
    assert(membershipsA.FindExact(Device(2U), Incarnation(2U)) != nullptr);
    assert(membershipsB.FindExact(Device(1U), Incarnation(1U)) != nullptr);
    assert(sessionsA.Find(Device(2U), Incarnation(2U)));
    assert(sessionsB.Find(Device(1U), Incarnation(1U)));

    // A forged responder signature is terminal and cannot mutate authoritative state.
    const auto rejectedA = Observe(candidatesA, 3U, 3U);
    const auto rejectedB = Observe(candidatesB, 4U, 1U);
    const Mesh::MeshSecurityCandidateContext rejectedContextA{
        rejectedA, 1U, Radio::RadioPeerHandle{3U, 1U}, {Device(3U), Incarnation(3U)}};
    const Mesh::MeshSecurityCandidateContext rejectedContextB{
        rejectedB, 1U, Radio::RadioPeerHandle{4U, 1U}, {Device(1U), Incarnation(1U)}};
    assert(initiator.Begin(rejectedA, 30U) == Mesh::MeshV1InitiatorBeginResult::InitiatorReady);
    assert(responder.Begin(rejectedB, 30U) == Mesh::MeshV1ResponderBeginResult::Started);
    assert(initiator.CopyInitiatorHello(rejectedA, initiatorHello.data(), initiatorHello.size()));
    assert(responder.AcceptInitiatorHello(rejectedContextB, initiatorHello.data(), initiatorHello.size()) ==
        Mesh::MeshV1ResponderHelloResult::ResponderReady);
    assert(responder.CopyResponderHello(rejectedB, responderHello.data(), responderHello.size()));
    responderHello[Mesh::MeshV1SecurityHandshakeCodec::ResponderPacketBytes -
        Mesh::MeshV1SecuritySuite::AuthenticationTagBytes - 1U] ^= 1U;
    assert(initiator.AcceptResponderHello(rejectedContextA, responderHello.data(), responderHello.size()) ==
        Mesh::MeshV1InitiatorResponderResult::Rejected);
    assert(candidatesA.Resolve(rejectedA) == nullptr && !authenticationsA.Contains(rejectedA));
    assert(membershipsA.FindDevice(Device(3U)) == nullptr && !sessionsA.Find(Device(3U), Incarnation(3U)));
    assert(responder.ResetForControlledShutdown());

    // Failed provider cleanup keeps ownership live until reset retries it successfully.
    const auto cleanup = Observe(candidatesA, 5U, 4U);
    providerA.FailNonce = true;
    providerA.FailNextEphemeralRelease = true;
    assert(initiator.Begin(cleanup, 40U) == Mesh::MeshV1InitiatorBeginResult::ResourceUnavailable);
    assert(authenticationsA.Contains(cleanup));
    assert(candidatesA.Resolve(cleanup)->State == Mesh::MembershipState::Authenticating);
    providerA.FailNonce = false;
    assert(initiator.ResetForControlledShutdown());
    assert(!initiator.CopyInitiatorHello(cleanup, initiatorHello.data(), initiatorHello.size()));
    return 0;
}
