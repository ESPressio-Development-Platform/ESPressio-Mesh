#pragma once

#include <cstddef>

#include "ESPressio_AdmissionPromotionCoordinator.hpp"
#include "ESPressio_MeshSecurityAuthority.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"

namespace ESPressio::Mesh {

struct MeshV1AdmissionTransactionOutcome final {
    MeshV1AdmissionResult Result{MeshV1AdmissionResult::Invalid};
    bool RetainHandshakeState{false};
};

/// <summary>Common serialized commit for either authenticated Mesh-v1 handshake direction.</summary>
/// <remarks>
/// The caller continues to own staged handshake state when RetainHandshakeState is true. Otherwise this operation has
/// released or transferred the provider session and completed the corresponding candidate transition. This function
/// must execute in the single Mesh mutation domain so capacity preflight cannot race session install or promotion.
/// </remarks>
template<std::size_t CandidateCapacity,
         std::size_t AuthenticationCapacity,
         std::size_t MembershipCapacity,
         std::size_t SessionCapacity>
MeshV1AdmissionTransactionOutcome CompleteMeshV1AdmissionTransaction(
    NeighbourCandidateHandle candidate,
    const MeshSecurityCandidateContext& candidateContext,
    const AuthenticatedMeshIdentity& identity,
    const MeshSecuritySessionIdentifier& sessionIdentifier,
    MeshSecuritySessionHandle& providerSession,
    AuthenticatedMembershipTable<MembershipCapacity>& memberships,
    AdmissionPromotionCoordinator<CandidateCapacity, AuthenticationCapacity, MembershipCapacity>& promotion,
    MeshSecuritySessionTable<SessionCapacity>& sessions,
    IMeshV1CryptographicProvider& provider,
    const IMeshAdmissionPolicy& admissionPolicy,
    AuthenticatedDirectPeerBinding* establishedBinding = nullptr
) noexcept {
    if (establishedBinding != nullptr) *establishedBinding = {};
    if (!candidate || !candidateContext || candidateContext.Candidate != candidate || !identity ||
        !sessionIdentifier || !providerSession) {
        return {MeshV1AdmissionResult::HandshakeNotAuthenticated, true};
    }

    const MeshAdmissionContext admissionContext{
        candidateContext, identity, memberships.Size(), memberships.MaximumSize()
    };
    const auto admission = admissionPolicy.EvaluateAdmission(admissionContext);
    if (admission == MeshAdmissionDisposition::Invalid || admission == MeshAdmissionDisposition::Reject ||
        admission == MeshAdmissionDisposition::Defer) {
        if (!provider.ReleaseSession(providerSession)) {
            return {MeshV1AdmissionResult::CleanupFailed, true};
        }
        providerSession = {};
        if (admission == MeshAdmissionDisposition::Invalid) {
            promotion.CompleteRejected(candidate);
            return {MeshV1AdmissionResult::Invalid, false};
        }
        if (admission == MeshAdmissionDisposition::Reject) {
            promotion.CompleteRejected(candidate);
            return {MeshV1AdmissionResult::Rejected, false};
        }
        return {promotion.ReleaseRetryable(candidate)
                    ? MeshV1AdmissionResult::AdmissionDeferred
                    : MeshV1AdmissionResult::Invalid,
                false};
    }

    const auto membershipPreflight = memberships.PreflightAuthenticatedUpsert(identity.Device, identity.Incarnation);
    if (membershipPreflight == AuthenticatedMembershipInsertResult::ConflictingIncarnation ||
        membershipPreflight == AuthenticatedMembershipInsertResult::ResourceUnavailable) {
        if (!provider.ReleaseSession(providerSession)) {
            return {MeshV1AdmissionResult::CleanupFailed, true};
        }
        providerSession = {};
        const auto promotionResult = promotion.CompleteAuthenticated(
            candidate, identity.Device, identity.Incarnation);
        return {promotionResult == AdmissionPromotionResult::ConflictingIncarnation
                    ? MeshV1AdmissionResult::ConflictingIncarnation
                    : MeshV1AdmissionResult::MembershipResourceUnavailable,
                false};
    }
    if ((membershipPreflight != AuthenticatedMembershipInsertResult::Inserted &&
         membershipPreflight != AuthenticatedMembershipInsertResult::Updated) ||
        !sessions.CanInstall(identity.Device)) {
        return {MeshV1AdmissionResult::SessionResourceUnavailable, true};
    }

    MeshSecuritySessionRecordHandle installed{};
    if (!sessions.Install(
            identity.Device, identity.Incarnation, sessionIdentifier,
            providerSession, provider, installed)) {
        return {MeshV1AdmissionResult::SessionResourceUnavailable, true};
    }
    providerSession = {};
    const auto promotionResult = promotion.CompleteAuthenticated(
        candidate, identity.Device, identity.Incarnation, establishedBinding);
    if (promotionResult != AdmissionPromotionResult::PromotedToValidating) {
        return {sessions.Release(installed, provider)
                    ? MeshV1AdmissionResult::Invalid
                    : MeshV1AdmissionResult::CleanupFailed,
                false};
    }
    return {MeshV1AdmissionResult::PromotedToValidating, false};
}

} // namespace ESPressio::Mesh
