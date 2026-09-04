#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AdmissionPromotionCoordinator.hpp"
#include "ESPressio_MeshSecurityAuthority.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of one non-blocking pass through candidate authentication and admission.</summary>
enum class AdmissionLifecycleResult : std::uint8_t {
    /// <summary>Authenticated identity passed admission and entered Validating membership.</summary>
    PromotedToValidating,
    /// <summary>Security work remains active while retaining its bounded authentication reservation.</summary>
    AuthenticationPending,
    /// <summary>Candidate remains Discovered and may be retried later.</summary>
    Retryable,
    /// <summary>Security authority currently cannot accept more work; candidate remains Discovered.</summary>
    SecurityResourceUnavailable,
    /// <summary>Admission policy deferred the authenticated candidate; candidate returns to Discovered.</summary>
    AdmissionDeferred,
    /// <summary>Authentication or admission definitively rejected the candidate and removed pre-auth state.</summary>
    Rejected,
    /// <summary>Authenticated identity conflicts with the currently retained incarnation for that device.</summary>
    ConflictingIncarnation,
    /// <summary>Authenticated membership capacity is exhausted.</summary>
    MembershipResourceUnavailable,
    /// <summary>Inbound authentication execution capacity is exhausted.</summary>
    AuthenticationResourceUnavailable,
    CandidateNotFound,
    Invalid
};

/// <summary>
/// Wire-neutral coordinator for the bounded pre-authentication → authentication → admission → Validating transition.
/// </summary>
/// <remarks>
/// This coordinator defines no cryptography, handshake, packet, control PrimitiveFamilyId, task or timer. It composes
/// existing bounded candidate/authentication resources with injected `IMeshSecurityAuthority` and
/// `IMeshAdmissionPolicy`, then delegates authoritative membership mutation to `AdmissionPromotionCoordinator`.
///
/// Identity claims remain untrusted until `IMeshSecurityAuthority` returns `Authenticated` with a valid
/// `AuthenticatedMeshIdentity`. Admission policy is invoked only after that point. A `Pending` security result retains
/// the existing authentication reservation; Retryable/ResourceUnavailable/Defer release expensive authentication work
/// and return the candidate to Discovered. Definitive rejection removes the candidate. This keeps resource pressure
/// from weakening authentication or silently granting identity authority.
///
/// On successful promotion, `establishedBinding` contains the exact authenticated identity plus the Radio peer that
/// produced the candidate. Retaining that binding in `AuthenticatedDirectPeerBindingTable` is deliberately a separate
/// local-execution concern rather than part of this membership transaction.
/// </remarks>
template<std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
         std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes>
class AdmissionLifecycleCoordinator final {
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    AdmissionPromotionCoordinator<CandidateCapacity, AuthenticationCapacity, MembershipCapacity>& _promotion;
    IMeshSecurityAuthority& _security;
    const IMeshAdmissionPolicy& _admission;

    MeshSecurityCandidateContext ContextFor(const PendingNeighbourCandidate& candidate) const noexcept {
        return MeshSecurityCandidateContext{
            candidate.Handle,
            candidate.Radio,
            candidate.Peer,
            candidate.Claim
        };
    }

    AdmissionLifecycleResult ReleaseRetryable(
        NeighbourCandidateHandle candidate,
        AdmissionLifecycleResult result
    ) noexcept {
        return _promotion.ReleaseRetryable(candidate) ? result : AdmissionLifecycleResult::Invalid;
    }

public:
    AdmissionLifecycleCoordinator(
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications,
        AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        AdmissionPromotionCoordinator<CandidateCapacity, AuthenticationCapacity, MembershipCapacity>& promotion,
        IMeshSecurityAuthority& security,
        const IMeshAdmissionPolicy& admission
    ) noexcept :
        _candidates(candidates),
        _authentications(authentications),
        _memberships(memberships),
        _promotion(promotion),
        _security(security),
        _admission(admission) {}

    /// <summary>
    /// Starts or advances one candidate without blocking. Call again for `AuthenticationPending` only when the
    /// security implementation has new evidence/work progress to evaluate.
    /// </summary>
    AdmissionLifecycleResult Evaluate(
        NeighbourCandidateHandle candidateHandle,
        std::uint64_t nowMilliseconds,
        AuthenticatedDirectPeerBinding* establishedBinding = nullptr
    ) noexcept {
        if (establishedBinding != nullptr) *establishedBinding = {};
        if (!candidateHandle || nowMilliseconds == 0U) return AdmissionLifecycleResult::Invalid;

        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return AdmissionLifecycleResult::CandidateNotFound;

        if (candidate->State == MembershipState::Discovered) {
            const auto reservation = _authentications.TryReserve(candidateHandle);
            switch (reservation) {
                case InboundAuthenticationReservationResult::Reserved:
                    if (!_candidates.SetState(candidateHandle, MembershipState::Authenticating)) {
                        _authentications.Release(candidateHandle);
                        return AdmissionLifecycleResult::Invalid;
                    }
                    candidate = _candidates.Resolve(candidateHandle);
                    break;
                case InboundAuthenticationReservationResult::AlreadyInProgress:
                    if (!_candidates.SetState(candidateHandle, MembershipState::Authenticating)) {
                        return AdmissionLifecycleResult::Invalid;
                    }
                    candidate = _candidates.Resolve(candidateHandle);
                    break;
                case InboundAuthenticationReservationResult::ResourceUnavailable:
                    return AdmissionLifecycleResult::AuthenticationResourceUnavailable;
                case InboundAuthenticationReservationResult::Invalid:
                    return AdmissionLifecycleResult::Invalid;
            }
        } else if (candidate->State != MembershipState::Authenticating ||
                   !_authentications.Contains(candidateHandle)) {
            return AdmissionLifecycleResult::Invalid;
        }

        if (candidate == nullptr) return AdmissionLifecycleResult::CandidateNotFound;
        const MeshSecurityCandidateContext securityContext = ContextFor(*candidate);
        if (!securityContext) {
            _promotion.CompleteRejected(candidateHandle);
            return AdmissionLifecycleResult::Invalid;
        }

        AuthenticatedMeshIdentity identity{};
        const auto authentication = _security.EvaluateCandidate(securityContext, nowMilliseconds, identity);
        switch (authentication) {
            case MeshAuthenticationDisposition::Pending:
                return AdmissionLifecycleResult::AuthenticationPending;
            case MeshAuthenticationDisposition::Retryable:
                return ReleaseRetryable(candidateHandle, AdmissionLifecycleResult::Retryable);
            case MeshAuthenticationDisposition::ResourceUnavailable:
                return ReleaseRetryable(candidateHandle, AdmissionLifecycleResult::SecurityResourceUnavailable);
            case MeshAuthenticationDisposition::Rejected:
                _promotion.CompleteRejected(candidateHandle);
                return AdmissionLifecycleResult::Rejected;
            case MeshAuthenticationDisposition::Invalid:
                _promotion.CompleteRejected(candidateHandle);
                return AdmissionLifecycleResult::Invalid;
            case MeshAuthenticationDisposition::Authenticated:
                break;
        }

        if (!identity) {
            _promotion.CompleteRejected(candidateHandle);
            return AdmissionLifecycleResult::Invalid;
        }

        const MeshAdmissionContext admissionContext{
            securityContext,
            identity,
            _memberships.Size(),
            _memberships.MaximumSize()
        };
        if (!admissionContext.IsValid()) {
            _promotion.CompleteRejected(candidateHandle);
            return AdmissionLifecycleResult::Invalid;
        }

        switch (_admission.EvaluateAdmission(admissionContext)) {
            case MeshAdmissionDisposition::Defer:
                return ReleaseRetryable(candidateHandle, AdmissionLifecycleResult::AdmissionDeferred);
            case MeshAdmissionDisposition::Reject:
                _promotion.CompleteRejected(candidateHandle);
                return AdmissionLifecycleResult::Rejected;
            case MeshAdmissionDisposition::Invalid:
                _promotion.CompleteRejected(candidateHandle);
                return AdmissionLifecycleResult::Invalid;
            case MeshAdmissionDisposition::Admit:
                break;
        }

        const auto promotion = _promotion.CompleteAuthenticated(
            candidateHandle,
            identity.Device,
            identity.Incarnation,
            establishedBinding
        );
        switch (promotion) {
            case AdmissionPromotionResult::PromotedToValidating:
                return AdmissionLifecycleResult::PromotedToValidating;
            case AdmissionPromotionResult::ConflictingIncarnation:
                return AdmissionLifecycleResult::ConflictingIncarnation;
            case AdmissionPromotionResult::MembershipResourceUnavailable:
                return AdmissionLifecycleResult::MembershipResourceUnavailable;
            case AdmissionPromotionResult::CandidateNotFound:
                return AdmissionLifecycleResult::CandidateNotFound;
            case AdmissionPromotionResult::AuthenticationNotReserved:
            case AdmissionPromotionResult::CandidateNotAuthenticating:
            case AdmissionPromotionResult::InvalidAuthenticatedIdentity:
                return AdmissionLifecycleResult::Invalid;
        }
        return AdmissionLifecycleResult::Invalid;
    }
};

} // namespace ESPressio::Mesh
