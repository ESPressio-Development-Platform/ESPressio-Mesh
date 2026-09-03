#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of completing one externally authenticated/admitted neighbour candidate.</summary>
enum class AdmissionPromotionResult : std::uint8_t {
    PromotedToValidating,
    ConflictingIncarnation,
    MembershipResourceUnavailable,
    CandidateNotFound,
    AuthenticationNotReserved,
    CandidateNotAuthenticating,
    InvalidAuthenticatedIdentity
};

/// <summary>
/// Narrow boundary that promotes externally authenticated identity into bounded Mesh membership.
/// </summary>
/// <remarks>
/// This coordinator performs no cryptography and makes no admission-policy decision. The caller invokes
/// CompleteAuthenticated only after the injected security/admission mechanisms establish an authenticated
/// DeviceIdentifier + MembershipIncarnation. Those authenticated values, not the candidate's untrusted claims,
/// are used to create Validating membership authority. Authentication execution reservation is always released
/// on completion. A successful promotion removes the pre-auth candidate; capacity/conflict leaves it Discovered
/// so policy may retry or resolve supersession explicitly without silently replacing authority.
/// </remarks>
template<
    std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
    std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes
>
class AdmissionPromotionCoordinator final {
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;

public:
    AdmissionPromotionCoordinator(
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications,
        AuthenticatedMembershipTable<MembershipCapacity>& memberships
    ) noexcept :
        _candidates(candidates),
        _authentications(authentications),
        _memberships(memberships) {}

    /// <summary>
    /// Promotes the externally established authenticated identity into Validating membership.
    /// </summary>
    AdmissionPromotionResult CompleteAuthenticated(
        NeighbourCandidateHandle candidateHandle,
        const System::DeviceIdentifier& authenticatedDevice,
        const MembershipIncarnation& authenticatedIncarnation
    ) noexcept {
        if (!authenticatedDevice || !authenticatedIncarnation) {
            if (_authentications.Contains(candidateHandle)) _authentications.Release(candidateHandle);
            if (auto* candidate = _candidates.Resolve(candidateHandle)) {
                candidate->State = MembershipState::Discovered;
            }
            return AdmissionPromotionResult::InvalidAuthenticatedIdentity;
        }

        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return AdmissionPromotionResult::CandidateNotFound;
        if (!_authentications.Contains(candidateHandle)) {
            return AdmissionPromotionResult::AuthenticationNotReserved;
        }
        if (candidate->State != MembershipState::Authenticating) {
            _authentications.Release(candidateHandle);
            candidate->State = MembershipState::Discovered;
            return AdmissionPromotionResult::CandidateNotAuthenticating;
        }

        const auto result = _memberships.UpsertAuthenticated(
            authenticatedDevice,
            authenticatedIncarnation,
            MembershipState::Validating,
            ReachabilityState::Reachable
        );

        _authentications.Release(candidateHandle);

        switch (result) {
            case AuthenticatedMembershipInsertResult::Inserted:
            case AuthenticatedMembershipInsertResult::Updated:
                _candidates.Remove(candidateHandle);
                return AdmissionPromotionResult::PromotedToValidating;
            case AuthenticatedMembershipInsertResult::ConflictingIncarnation:
                candidate->State = MembershipState::Discovered;
                return AdmissionPromotionResult::ConflictingIncarnation;
            case AuthenticatedMembershipInsertResult::ResourceUnavailable:
                candidate->State = MembershipState::Discovered;
                return AdmissionPromotionResult::MembershipResourceUnavailable;
            case AuthenticatedMembershipInsertResult::Invalid:
                candidate->State = MembershipState::Discovered;
                return AdmissionPromotionResult::InvalidAuthenticatedIdentity;
        }
        candidate->State = MembershipState::Discovered;
        return AdmissionPromotionResult::InvalidAuthenticatedIdentity;
    }

    /// <summary>
    /// Completes a definitive authentication/admission rejection and discards its pre-auth candidate.
    /// </summary>
    bool CompleteRejected(NeighbourCandidateHandle candidateHandle) noexcept {
        const bool hadCandidate = _candidates.Resolve(candidateHandle) != nullptr;
        _authentications.Release(candidateHandle);
        if (hadCandidate) _candidates.Remove(candidateHandle);
        return hadCandidate;
    }

    /// <summary>
    /// Abandons the current expensive authentication attempt while retaining the candidate for a later retry.
    /// </summary>
    bool ReleaseRetryable(NeighbourCandidateHandle candidateHandle) noexcept {
        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return false;
        const bool released = _authentications.Release(candidateHandle);
        candidate->State = MembershipState::Discovered;
        return released;
    }
};

} // namespace ESPressio::Mesh
