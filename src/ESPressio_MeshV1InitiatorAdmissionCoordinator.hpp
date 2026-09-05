#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshV1AdmissionTransaction.hpp"
#include "ESPressio_MeshSecurityAuthority.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"

namespace ESPressio::Mesh {

enum class MeshV1InitiatorBeginResult : std::uint8_t {
    InitiatorReady, AlreadyStarted, AuthenticationResourceUnavailable, StateResourceUnavailable,
    ResourceUnavailable, CandidateNotFound, Invalid
};

enum class MeshV1InitiatorResponderResult : std::uint8_t {
    FinishReady, AlreadyReady, ResourceUnavailable, Rejected, CandidateNotFound, Invalid
};

enum class MeshV1InitiatorFinishSubmissionResult : std::uint8_t {
    Authenticated, AlreadyAuthenticated, FinishNotReady, CandidateNotFound, Invalid
};

/// <summary>Bounded transactional initiator-side Mesh-v1 handshake and authenticated admission.</summary>
/// <remarks>
/// Each fixed state record owns one exact inbound-authentication reservation even though the local node initiated the
/// exchange: the remote identity claim remains untrusted until its signed ResponderHello and responder confirmation
/// authenticate. The InitiatorFinish is exposed as immutable bounded bytes and must be marked submitted before local
/// admission may commit. A derived provider session remains staged until the same serialized membership/session
/// preflight and promotion transaction used by the responder direction succeeds.
/// </remarks>
template<std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
         std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1InitiatorAdmissionCoordinator final : public IMeshPendingAuthenticationReset {
    enum class Stage : std::uint8_t { Empty, Building, InitiatorReady, FinishReady, Authenticated, CleanupRequired };

    struct State final {
        MeshSecurityCandidateContext Candidate{};
        AuthenticatedMeshIdentity Identity{};
        MeshEphemeralKeyHandle Ephemeral{};
        MeshSecuritySessionHandle ProviderSession{};
        MeshSecuritySessionIdentifier SessionIdentifier{};
        MeshSecurityDigest InitiatorPacketDigest{};
        MeshSecurityDigest ResponderPacketDigest{};
        MeshSecurityDigest TranscriptDigest{};
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::InitiatorPacketBytes> InitiatorPacket{};
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::FinishPacketBytes> FinishPacket{};
        MeshV1InitiatorHello Initiator{};
        std::uint64_t StartedAtMilliseconds{0U};
        Stage Current{Stage::Empty};
    };

    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    AdmissionPromotionCoordinator<CandidateCapacity, AuthenticationCapacity, MembershipCapacity>& _promotion;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    const IMeshAdmissionPolicy& _admission;
    MeshIdentifier _mesh;
    MeshSecurityChannelBinding _channelBinding;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;
    std::uint64_t _timeoutMilliseconds;
    std::array<State, AuthenticationCapacity> _states{};

    static bool SameContext(
        const MeshSecurityCandidateContext& left,
        const MeshSecurityCandidateContext& right
    ) noexcept {
        return left.Candidate == right.Candidate && left.Radio == right.Radio && left.Peer == right.Peer &&
               left.Claim.Device == right.Claim.Device && left.Claim.Incarnation == right.Claim.Incarnation;
    }

    template<std::size_t Size>
    static bool Equal(const MeshSecurityBytes<Size>& left, const MeshSecurityBytes<Size>& right) noexcept {
        std::uint8_t difference = 0U;
        for (std::size_t index = 0; index < Size; ++index) {
            difference |= static_cast<std::uint8_t>(left.Value[index] ^ right.Value[index]);
        }
        return difference == 0U;
    }

    MeshSecurityCandidateContext ContextFor(const PendingNeighbourCandidate& candidate) const noexcept {
        return {candidate.Handle, candidate.Radio, candidate.Peer, candidate.Claim};
    }

    State* Find(NeighbourCandidateHandle candidate) noexcept {
        if (!candidate) return nullptr;
        for (auto& state : _states) {
            if (state.Current != Stage::Empty && state.Candidate.Candidate == candidate) return &state;
        }
        return nullptr;
    }

    const State* Find(NeighbourCandidateHandle candidate) const noexcept {
        if (!candidate) return nullptr;
        for (const auto& state : _states) {
            if (state.Current != Stage::Empty && state.Candidate.Candidate == candidate) return &state;
        }
        return nullptr;
    }

    static void Clear(State& state) noexcept { state = {}; }

    bool ReleaseCryptography(State& state) noexcept {
        if (state.Ephemeral && !_provider.ReleaseEphemeralKey(state.Ephemeral)) return false;
        state.Ephemeral = {};
        if (state.ProviderSession && !_provider.ReleaseSession(state.ProviderSession)) return false;
        state.ProviderSession = {};
        return true;
    }

    MeshV1InitiatorResponderResult RejectResponder(State& state) noexcept {
        const auto candidate = state.Candidate.Candidate;
        state.Current = Stage::CleanupRequired;
        if (!ReleaseCryptography(state)) return MeshV1InitiatorResponderResult::ResourceUnavailable;
        Clear(state);
        _promotion.CompleteRejected(candidate);
        return MeshV1InitiatorResponderResult::Rejected;
    }

public:
    MeshV1InitiatorAdmissionCoordinator(
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications,
        AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        AdmissionPromotionCoordinator<CandidateCapacity, AuthenticationCapacity, MembershipCapacity>& promotion,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        const IMeshAdmissionPolicy& admission,
        const MeshIdentifier& mesh,
        const MeshSecurityChannelBinding& channelBinding,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation,
        std::uint64_t timeoutMilliseconds
    ) noexcept :
        _candidates(candidates), _authentications(authentications), _memberships(memberships),
        _promotion(promotion), _sessions(sessions), _provider(provider), _admission(admission),
        _mesh(mesh), _channelBinding(channelBinding), _localDevice(localDevice),
        _localIncarnation(localIncarnation), _timeoutMilliseconds(timeoutMilliseconds) {}

    MeshV1InitiatorBeginResult Begin(
        NeighbourCandidateHandle candidateHandle,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!candidateHandle || nowMilliseconds == 0U || !_mesh || !_channelBinding || !_localDevice ||
            !_localIncarnation || _timeoutMilliseconds == 0U) return MeshV1InitiatorBeginResult::Invalid;
        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return MeshV1InitiatorBeginResult::CandidateNotFound;
        if (Find(candidateHandle) != nullptr) return MeshV1InitiatorBeginResult::AlreadyStarted;
        if (candidate->State != MembershipState::Discovered) return MeshV1InitiatorBeginResult::Invalid;
        const auto reserved = _authentications.TryReserve(candidateHandle);
        if (reserved == InboundAuthenticationReservationResult::ResourceUnavailable) {
            return MeshV1InitiatorBeginResult::AuthenticationResourceUnavailable;
        }
        if (reserved != InboundAuthenticationReservationResult::Reserved) return MeshV1InitiatorBeginResult::Invalid;
        State* target = nullptr;
        for (auto& state : _states) if (state.Current == Stage::Empty) { target = &state; break; }
        if (target == nullptr) {
            _authentications.Release(candidateHandle);
            return MeshV1InitiatorBeginResult::StateResourceUnavailable;
        }
        if (!_candidates.SetState(candidateHandle, MembershipState::Authenticating)) {
            _authentications.Release(candidateHandle);
            return MeshV1InitiatorBeginResult::Invalid;
        }
        target->Candidate = ContextFor(*candidate);
        target->StartedAtMilliseconds = nowMilliseconds;
        target->Current = Stage::Building;
        target->Initiator.Mesh = _mesh;
        target->Initiator.Device = _localDevice;
        target->Initiator.Incarnation = _localIncarnation;
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::HeaderBytes +
            MeshV1SecurityHandshakeCodec::InitiatorUnsignedBodyBytes> unsignedPacket{};
        MeshSecurityDigest signatureDigest{};
        const bool built = _provider.GenerateEphemeralKey(
                target->Ephemeral, target->Initiator.EphemeralPublicKey) &&
            _provider.GenerateHandshakeNonce(target->Initiator.Nonce) &&
            MeshV1SecurityHandshakeCodec::EncodeInitiatorUnsigned(
                target->Initiator, unsignedPacket.data(), unsignedPacket.size()) &&
            _provider.Hash(unsignedPacket.data(), unsignedPacket.size(), signatureDigest) &&
            _provider.SignIdentityDigest(_localDevice, signatureDigest, target->Initiator.Signature) &&
            MeshV1SecurityHandshakeCodec::EncodeInitiator(
                target->Initiator, target->InitiatorPacket.data(), target->InitiatorPacket.size()) &&
            _provider.Hash(target->InitiatorPacket.data(), target->InitiatorPacket.size(),
                           target->InitiatorPacketDigest);
        if (!built) {
            const bool released = ReleaseCryptography(*target);
            if (!released) {
                target->Current = Stage::CleanupRequired;
                return MeshV1InitiatorBeginResult::ResourceUnavailable;
            }
            Clear(*target);
            return _promotion.ReleaseRetryable(candidateHandle)
                ? MeshV1InitiatorBeginResult::ResourceUnavailable
                : MeshV1InitiatorBeginResult::Invalid;
        }
        target->Current = Stage::InitiatorReady;
        return MeshV1InitiatorBeginResult::InitiatorReady;
    }

    bool CopyInitiatorHello(
        NeighbourCandidateHandle candidate,
        std::uint8_t* output,
        std::size_t outputBytes
    ) const noexcept {
        const auto* state = Find(candidate);
        if (state == nullptr || state->Current != Stage::InitiatorReady || output == nullptr ||
            outputBytes != state->InitiatorPacket.size()) return false;
        for (std::size_t index = 0; index < outputBytes; ++index) output[index] = state->InitiatorPacket[index];
        return true;
    }

    MeshV1InitiatorResponderResult AcceptResponderHello(
        const MeshSecurityCandidateContext& context,
        const std::uint8_t* packet,
        std::size_t packetBytes
    ) noexcept {
        auto* state = Find(context.Candidate);
        if (state == nullptr) return MeshV1InitiatorResponderResult::CandidateNotFound;
        if (!SameContext(state->Candidate, context)) return MeshV1InitiatorResponderResult::Invalid;
        if (state->Current == Stage::FinishReady || state->Current == Stage::Authenticated) {
            MeshSecurityDigest retransmissionDigest{};
            if (packet == nullptr || packetBytes != MeshV1SecurityHandshakeCodec::ResponderPacketBytes ||
                !_provider.Hash(packet, packetBytes, retransmissionDigest)) {
                return MeshV1InitiatorResponderResult::ResourceUnavailable;
            }
            return Equal(retransmissionDigest, state->ResponderPacketDigest)
                ? MeshV1InitiatorResponderResult::AlreadyReady
                : MeshV1InitiatorResponderResult::Invalid;
        }
        if (state->Current == Stage::Building || state->Current == Stage::CleanupRequired) {
            return MeshV1InitiatorResponderResult::ResourceUnavailable;
        }
        if (state->Current != Stage::InitiatorReady) return MeshV1InitiatorResponderResult::Invalid;

        MeshV1ResponderHello responder{};
        if (!MeshV1SecurityHandshakeCodec::DecodeResponder(packet, packetBytes, responder) ||
            responder.Mesh != _mesh || responder.Device != context.Claim.Device ||
            responder.Incarnation != context.Claim.Incarnation || responder.Device == _localDevice ||
            !Equal(responder.InitiatorHelloDigest, state->InitiatorPacketDigest)) {
            return RejectResponder(*state);
        }
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::HeaderBytes +
            MeshV1SecurityHandshakeCodec::ResponderUnsignedBodyBytes> responderUnsigned{};
        MeshSecurityDigest responderSignatureDigest{};
        if (!MeshV1SecurityHandshakeCodec::EncodeResponderUnsigned(
                responder, responderUnsigned.data(), responderUnsigned.size()) ||
            !_provider.Hash(responderUnsigned.data(), responderUnsigned.size(), responderSignatureDigest)) {
            return MeshV1InitiatorResponderResult::ResourceUnavailable;
        }
        const auto verified = _provider.VerifyRegisteredIdentityDigest(
            responder.Device, responderSignatureDigest, responder.Signature
        );
        if (verified == MeshIdentityVerificationResult::ResourceUnavailable) {
            return MeshV1InitiatorResponderResult::ResourceUnavailable;
        }
        if (verified != MeshIdentityVerificationResult::Verified) return RejectResponder(*state);

        constexpr std::size_t SignedResponderBytes =
            MeshV1SecurityHandshakeCodec::ResponderPacketBytes - MeshV1SecuritySuite::AuthenticationTagBytes;
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::InitiatorPacketBytes + SignedResponderBytes> transcript{};
        for (std::size_t index = 0; index < state->InitiatorPacket.size(); ++index) {
            transcript[index] = state->InitiatorPacket[index];
        }
        for (std::size_t index = 0; index < SignedResponderBytes; ++index) {
            transcript[state->InitiatorPacket.size() + index] = packet[index];
        }
        if (!_provider.Hash(transcript.data(), transcript.size(), state->TranscriptDigest) ||
            !_provider.Hash(packet, packetBytes, state->ResponderPacketDigest)) {
            return MeshV1InitiatorResponderResult::ResourceUnavailable;
        }
        if (!_provider.DeriveSession(
                state->Ephemeral, responder.EphemeralPublicKey, _mesh, _channelBinding,
                _localDevice, _localIncarnation, state->Initiator.Nonce,
                responder.Device, responder.Incarnation, responder.Nonce, state->TranscriptDigest,
                MeshSecuritySessionRole::Initiator, state->ProviderSession, state->SessionIdentifier)) {
            if (state->ProviderSession) {
                if (!_provider.ReleaseSession(state->ProviderSession)) {
                    return MeshV1InitiatorResponderResult::ResourceUnavailable;
                }
                state->ProviderSession = {};
            }
            return MeshV1InitiatorResponderResult::ResourceUnavailable;
        }
        if (!_provider.ReleaseEphemeralKey(state->Ephemeral)) return RejectResponder(*state);
        state->Ephemeral = {};
        if (!_provider.Open(
                state->ProviderSession, MeshSecurityTrafficPurpose::KeyConfirmation, 1U,
                state->TranscriptDigest.Value.data(), state->TranscriptDigest.Value.size(),
                nullptr, 0U, responder.ConfirmationTag, nullptr)) return RejectResponder(*state);

        MeshV1InitiatorFinish finish{};
        finish.HandshakeTranscriptDigest = state->TranscriptDigest;
        if (!_provider.Seal(
                state->ProviderSession, MeshSecurityTrafficPurpose::KeyConfirmation, 1U,
                state->TranscriptDigest.Value.data(), state->TranscriptDigest.Value.size(),
                nullptr, 0U, nullptr, finish.ConfirmationTag) ||
            !MeshV1SecurityHandshakeCodec::EncodeFinish(
                finish, state->FinishPacket.data(), state->FinishPacket.size())) return RejectResponder(*state);
        state->Identity = {responder.Device, responder.Incarnation};
        state->Current = Stage::FinishReady;
        return MeshV1InitiatorResponderResult::FinishReady;
    }

    bool CopyInitiatorFinish(
        NeighbourCandidateHandle candidate,
        std::uint8_t* output,
        std::size_t outputBytes
    ) const noexcept {
        const auto* state = Find(candidate);
        if (state == nullptr || state->Current != Stage::FinishReady || output == nullptr ||
            outputBytes != state->FinishPacket.size()) return false;
        for (std::size_t index = 0; index < outputBytes; ++index) output[index] = state->FinishPacket[index];
        return true;
    }

    MeshV1InitiatorFinishSubmissionResult MarkInitiatorFinishSubmitted(
        const MeshSecurityCandidateContext& context
    ) noexcept {
        auto* state = Find(context.Candidate);
        if (state == nullptr) return MeshV1InitiatorFinishSubmissionResult::CandidateNotFound;
        if (!SameContext(state->Candidate, context)) return MeshV1InitiatorFinishSubmissionResult::Invalid;
        if (state->Current == Stage::Authenticated) {
            return MeshV1InitiatorFinishSubmissionResult::AlreadyAuthenticated;
        }
        if (state->Current != Stage::FinishReady) {
            return MeshV1InitiatorFinishSubmissionResult::FinishNotReady;
        }
        state->Current = Stage::Authenticated;
        return MeshV1InitiatorFinishSubmissionResult::Authenticated;
    }

    MeshV1AdmissionResult CompleteAdmission(
        NeighbourCandidateHandle candidateHandle,
        AuthenticatedDirectPeerBinding* establishedBinding = nullptr
    ) noexcept {
        if (establishedBinding != nullptr) *establishedBinding = {};
        auto* state = Find(candidateHandle);
        if (state == nullptr) return MeshV1AdmissionResult::CandidateNotFound;
        if (state->Current != Stage::Authenticated || !state->Identity || !state->ProviderSession ||
            !state->SessionIdentifier) return MeshV1AdmissionResult::HandshakeNotAuthenticated;
        const auto outcome = CompleteMeshV1AdmissionTransaction(
            candidateHandle, state->Candidate, state->Identity, state->SessionIdentifier,
            state->ProviderSession, _memberships, _promotion, _sessions, _provider, _admission,
            establishedBinding);
        if (!outcome.RetainHandshakeState) Clear(*state);
        return outcome.Result;
    }

    std::size_t Expire(std::uint64_t nowMilliseconds) noexcept {
        if (nowMilliseconds == 0U) return 0U;
        std::size_t expired = 0U;
        for (auto& state : _states) {
            if (state.Current == Stage::Empty || nowMilliseconds < state.StartedAtMilliseconds ||
                nowMilliseconds - state.StartedAtMilliseconds < _timeoutMilliseconds) continue;
            const auto candidate = state.Candidate.Candidate;
            if (!ReleaseCryptography(state)) continue;
            Clear(state);
            if (_promotion.ReleaseRetryable(candidate)) ++expired;
        }
        return expired;
    }

    bool ReleasePendingAuthenticationBeforeProviderReset() noexcept override {
        bool releasedAll = true;
        for (auto& state : _states) {
            if (state.Current == Stage::Empty) continue;
            if (!ReleaseCryptography(state)) { releasedAll = false; continue; }
            Clear(state);
        }
        return releasedAll;
    }

    void ClearPendingAuthenticationAfterProviderReset() noexcept override {
        for (auto& state : _states) Clear(state);
    }

    bool ResetForControlledShutdown() noexcept {
        return ReleasePendingAuthenticationBeforeProviderReset();
    }
};

} // namespace ESPressio::Mesh
