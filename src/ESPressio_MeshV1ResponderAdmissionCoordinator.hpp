#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshV1AdmissionTransaction.hpp"
#include "ESPressio_MeshSecurityAuthority.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"

namespace ESPressio::Mesh {

enum class MeshV1ResponderBeginResult : std::uint8_t {
    Started, AlreadyStarted, AuthenticationResourceUnavailable, StateResourceUnavailable,
    CandidateNotFound, Invalid
};

enum class MeshV1ResponderHelloResult : std::uint8_t {
    ResponderReady, AlreadyReady, ResourceUnavailable, Rejected, CandidateNotFound, Invalid
};

enum class MeshV1ResponderFinishResult : std::uint8_t {
    Authenticated, AlreadyAuthenticated, ResourceUnavailable, Rejected, CandidateNotFound, Invalid
};

/// <summary>Bounded transactional Mesh-v1 responder handshake and authenticated admission.</summary>
/// <remarks>
/// One fixed record exists per active inbound-authentication reservation. A derived provider session remains staged
/// here until identity signature, transcript binding and both directional key confirmations have succeeded. Admission
/// deferral/rejection and promotion failure release it before pre-authentication state changes. On the admitted path,
/// membership and session capacity are preflighted in the serialized Mesh domain, the session is installed, and only
/// then is the exact authenticated identity promoted. No untrusted claim mutates membership or session state.
/// </remarks>
template<std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
         std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1ResponderAdmissionCoordinator final : public IMeshPendingAuthenticationReset {
    enum class Stage : std::uint8_t {
        Empty, AwaitingInitiatorHello, ResponderReady, Authenticated, CleanupRequired
    };

    struct State final {
        MeshSecurityCandidateContext Candidate{};
        AuthenticatedMeshIdentity Identity{};
        MeshEphemeralKeyHandle Ephemeral{};
        MeshSecuritySessionHandle ProviderSession{};
        MeshSecuritySessionIdentifier SessionIdentifier{};
        MeshSecurityDigest InitiatorPacketDigest{};
        MeshSecurityDigest TranscriptDigest{};
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::ResponderPacketBytes> Response{};
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

    static bool Equal(const MeshSecurityDigest& left, const MeshSecurityDigest& right) noexcept {
        std::uint8_t difference = 0U;
        for (std::size_t index = 0; index < left.Value.size(); ++index) {
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

    bool ReleaseHelloWorkForRetry(State& state) noexcept {
        const auto candidate = state.Candidate;
        const auto startedAt = state.StartedAtMilliseconds;
        state.Current = Stage::CleanupRequired;
        if (!ReleaseCryptography(state)) return false;
        Clear(state);
        state.Candidate = candidate;
        state.StartedAtMilliseconds = startedAt;
        state.Current = Stage::AwaitingInitiatorHello;
        return true;
    }

    MeshV1ResponderHelloResult RejectHello(State& state) noexcept {
        const auto candidate = state.Candidate.Candidate;
        state.Current = Stage::CleanupRequired;
        if (!ReleaseCryptography(state)) return MeshV1ResponderHelloResult::ResourceUnavailable;
        Clear(state);
        _promotion.CompleteRejected(candidate);
        return MeshV1ResponderHelloResult::Rejected;
    }

    MeshV1ResponderFinishResult RejectFinish(State& state) noexcept {
        const auto candidate = state.Candidate.Candidate;
        state.Current = Stage::CleanupRequired;
        if (!ReleaseCryptography(state)) return MeshV1ResponderFinishResult::ResourceUnavailable;
        Clear(state);
        _promotion.CompleteRejected(candidate);
        return MeshV1ResponderFinishResult::Rejected;
    }

public:
    MeshV1ResponderAdmissionCoordinator(
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

    MeshV1ResponderBeginResult Begin(
        NeighbourCandidateHandle candidateHandle,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!candidateHandle || nowMilliseconds == 0U || !_mesh || !_channelBinding || !_localDevice ||
            !_localIncarnation || _timeoutMilliseconds == 0U) return MeshV1ResponderBeginResult::Invalid;
        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return MeshV1ResponderBeginResult::CandidateNotFound;
        if (Find(candidateHandle) != nullptr) return MeshV1ResponderBeginResult::AlreadyStarted;
        if (candidate->State != MembershipState::Discovered) return MeshV1ResponderBeginResult::Invalid;
        const auto reserved = _authentications.TryReserve(candidateHandle);
        if (reserved == InboundAuthenticationReservationResult::ResourceUnavailable) {
            return MeshV1ResponderBeginResult::AuthenticationResourceUnavailable;
        }
        if (reserved != InboundAuthenticationReservationResult::Reserved) return MeshV1ResponderBeginResult::Invalid;
        State* target = nullptr;
        for (auto& state : _states) if (state.Current == Stage::Empty) { target = &state; break; }
        if (target == nullptr) {
            _authentications.Release(candidateHandle);
            return MeshV1ResponderBeginResult::StateResourceUnavailable;
        }
        if (!_candidates.SetState(candidateHandle, MembershipState::Authenticating)) {
            _authentications.Release(candidateHandle);
            return MeshV1ResponderBeginResult::Invalid;
        }
        target->Candidate = ContextFor(*candidate);
        target->StartedAtMilliseconds = nowMilliseconds;
        target->Current = Stage::AwaitingInitiatorHello;
        return MeshV1ResponderBeginResult::Started;
    }

    MeshV1ResponderHelloResult AcceptInitiatorHello(
        const MeshSecurityCandidateContext& context,
        const std::uint8_t* packet,
        std::size_t packetBytes
    ) noexcept {
        auto* state = Find(context.Candidate);
        if (state == nullptr) return MeshV1ResponderHelloResult::CandidateNotFound;
        if (!SameContext(state->Candidate, context)) return MeshV1ResponderHelloResult::Invalid;
        if (state->Current == Stage::ResponderReady) {
            MeshSecurityDigest retransmissionDigest{};
            if (packet == nullptr || packetBytes != MeshV1SecurityHandshakeCodec::InitiatorPacketBytes ||
                !_provider.Hash(packet, packetBytes, retransmissionDigest)) {
                return MeshV1ResponderHelloResult::ResourceUnavailable;
            }
            return Equal(retransmissionDigest, state->InitiatorPacketDigest)
                ? MeshV1ResponderHelloResult::AlreadyReady
                : MeshV1ResponderHelloResult::Invalid;
        }
        if (state->Current == Stage::CleanupRequired) {
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }
        if (state->Current != Stage::AwaitingInitiatorHello) return MeshV1ResponderHelloResult::Invalid;

        MeshV1InitiatorHello initiator{};
        if (!MeshV1SecurityHandshakeCodec::DecodeInitiator(packet, packetBytes, initiator) ||
            initiator.Mesh != _mesh || initiator.Device != context.Claim.Device ||
            initiator.Incarnation != context.Claim.Incarnation || initiator.Device == _localDevice) {
            return RejectHello(*state);
        }
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::HeaderBytes +
            MeshV1SecurityHandshakeCodec::InitiatorUnsignedBodyBytes> initiatorUnsigned{};
        MeshSecurityDigest initiatorSignatureDigest{};
        if (!MeshV1SecurityHandshakeCodec::EncodeInitiatorUnsigned(
                initiator, initiatorUnsigned.data(), initiatorUnsigned.size()) ||
            !_provider.Hash(initiatorUnsigned.data(), initiatorUnsigned.size(), initiatorSignatureDigest)) {
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }
        const auto verified = _provider.VerifyRegisteredIdentityDigest(
            initiator.Device, initiatorSignatureDigest, initiator.Signature
        );
        if (verified == MeshIdentityVerificationResult::ResourceUnavailable) {
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }
        if (verified != MeshIdentityVerificationResult::Verified) return RejectHello(*state);

        MeshSecurityDigest initiatorPacketDigest{};
        if (!_provider.Hash(packet, packetBytes, initiatorPacketDigest)) {
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }
        state->InitiatorPacketDigest = initiatorPacketDigest;
        MeshV1ResponderHello responder{};
        responder.Mesh = _mesh;
        responder.Device = _localDevice;
        responder.Incarnation = _localIncarnation;
        responder.InitiatorHelloDigest = initiatorPacketDigest;
        if (!_provider.GenerateEphemeralKey(state->Ephemeral, responder.EphemeralPublicKey) ||
            !_provider.GenerateHandshakeNonce(responder.Nonce)) {
            ReleaseHelloWorkForRetry(*state);
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::HeaderBytes +
            MeshV1SecurityHandshakeCodec::ResponderUnsignedBodyBytes> responderUnsigned{};
        MeshSecurityDigest responderSignatureDigest{};
        if (!MeshV1SecurityHandshakeCodec::EncodeResponderUnsigned(
                responder, responderUnsigned.data(), responderUnsigned.size()) ||
            !_provider.Hash(responderUnsigned.data(), responderUnsigned.size(), responderSignatureDigest) ||
            !_provider.SignIdentityDigest(_localDevice, responderSignatureDigest, responder.Signature)) {
            ReleaseHelloWorkForRetry(*state);
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }

        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::ResponderPacketBytes> unsignedResponse{};
        if (!MeshV1SecurityHandshakeCodec::EncodeResponder(
                responder, unsignedResponse.data(), unsignedResponse.size())) return RejectHello(*state);
        constexpr std::size_t SignedResponderBytes =
            MeshV1SecurityHandshakeCodec::ResponderPacketBytes - MeshV1SecuritySuite::AuthenticationTagBytes;
        std::array<std::uint8_t, MeshV1SecurityHandshakeCodec::InitiatorPacketBytes + SignedResponderBytes> transcript{};
        for (std::size_t index = 0; index < packetBytes; ++index) transcript[index] = packet[index];
        for (std::size_t index = 0; index < SignedResponderBytes; ++index) {
            transcript[packetBytes + index] = unsignedResponse[index];
        }
        if (!_provider.Hash(transcript.data(), transcript.size(), state->TranscriptDigest) ||
            !_provider.DeriveSession(
                state->Ephemeral, initiator.EphemeralPublicKey, _mesh, _channelBinding,
                initiator.Device, initiator.Incarnation, initiator.Nonce,
                _localDevice, _localIncarnation, responder.Nonce, state->TranscriptDigest,
                MeshSecuritySessionRole::Responder, state->ProviderSession, state->SessionIdentifier)) {
            ReleaseHelloWorkForRetry(*state);
            return MeshV1ResponderHelloResult::ResourceUnavailable;
        }
        if (!_provider.ReleaseEphemeralKey(state->Ephemeral)) return RejectHello(*state);
        state->Ephemeral = {};
        if (!_provider.Seal(
                state->ProviderSession, MeshSecurityTrafficPurpose::KeyConfirmation, 1U,
                state->TranscriptDigest.Value.data(), state->TranscriptDigest.Value.size(),
                nullptr, 0U, nullptr, responder.ConfirmationTag) ||
            !MeshV1SecurityHandshakeCodec::EncodeResponder(
                responder, state->Response.data(), state->Response.size())) return RejectHello(*state);
        state->Identity = {initiator.Device, initiator.Incarnation};
        state->Current = Stage::ResponderReady;
        return MeshV1ResponderHelloResult::ResponderReady;
    }

    bool CopyResponderHello(
        NeighbourCandidateHandle candidate,
        std::uint8_t* output,
        std::size_t outputBytes
    ) const noexcept {
        const auto* state = Find(candidate);
        if (state == nullptr || state->Current != Stage::ResponderReady || output == nullptr ||
            outputBytes != state->Response.size()) return false;
        for (std::size_t index = 0; index < outputBytes; ++index) output[index] = state->Response[index];
        return true;
    }

    MeshV1ResponderFinishResult AcceptInitiatorFinish(
        const MeshSecurityCandidateContext& context,
        const std::uint8_t* packet,
        std::size_t packetBytes
    ) noexcept {
        auto* state = Find(context.Candidate);
        if (state == nullptr) return MeshV1ResponderFinishResult::CandidateNotFound;
        if (!SameContext(state->Candidate, context)) return MeshV1ResponderFinishResult::Invalid;
        if (state->Current == Stage::CleanupRequired) {
            return MeshV1ResponderFinishResult::ResourceUnavailable;
        }
        if (state->Current != Stage::ResponderReady && state->Current != Stage::Authenticated) {
            return MeshV1ResponderFinishResult::Invalid;
        }
        MeshV1InitiatorFinish finish{};
        const bool confirmed = MeshV1SecurityHandshakeCodec::DecodeFinish(packet, packetBytes, finish) &&
            Equal(finish.HandshakeTranscriptDigest, state->TranscriptDigest) &&
            _provider.Open(
                state->ProviderSession, MeshSecurityTrafficPurpose::KeyConfirmation, 1U,
                state->TranscriptDigest.Value.data(), state->TranscriptDigest.Value.size(),
                nullptr, 0U, finish.ConfirmationTag, nullptr);
        if (state->Current == Stage::Authenticated) {
            return confirmed ? MeshV1ResponderFinishResult::AlreadyAuthenticated
                             : MeshV1ResponderFinishResult::Invalid;
        }
        if (!confirmed) return RejectFinish(*state);
        state->Current = Stage::Authenticated;
        return MeshV1ResponderFinishResult::Authenticated;
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
