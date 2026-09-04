#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"

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

/// <summary>Narrow boundary that promotes externally authenticated identity into bounded Mesh membership.</summary>
template<std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
         std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes>
class AdmissionPromotionCoordinator final {
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;

public:
    AdmissionPromotionCoordinator(PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
                                  InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications,
                                  AuthenticatedMembershipTable<MembershipCapacity>& memberships) noexcept
        : _candidates(candidates), _authentications(authentications), _memberships(memberships) {}

    /// <summary>
    /// Promotes externally established authenticated identity into Validating membership and optionally returns the
    /// exact local Radio peer binding which produced the authenticated candidate.
    /// </summary>
    /// <remarks>
    /// The optional binding is populated only after successful authenticated membership promotion. Its identity fields
    /// use the separately authenticated DeviceIdentifier/MembershipIncarnation, never the candidate's untrusted claim.
    /// The caller may then retain it in AuthenticatedDirectPeerBindingTable. This coordinator performs no cryptography.
    /// </remarks>
    AdmissionPromotionResult CompleteAuthenticated(
        NeighbourCandidateHandle candidateHandle,
        const System::DeviceIdentifier& authenticatedDevice,
        const MembershipIncarnation& authenticatedIncarnation,
        AuthenticatedDirectPeerBinding* establishedBinding = nullptr
    ) noexcept {
        if (establishedBinding != nullptr) *establishedBinding = {};
        if (!authenticatedDevice || !authenticatedIncarnation) {
            if (_authentications.Contains(candidateHandle)) _authentications.Release(candidateHandle);
            if (auto* candidate = _candidates.Resolve(candidateHandle)) candidate->State = MembershipState::Discovered;
            return AdmissionPromotionResult::InvalidAuthenticatedIdentity;
        }

        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return AdmissionPromotionResult::CandidateNotFound;
        if (!_authentications.Contains(candidateHandle)) return AdmissionPromotionResult::AuthenticationNotReserved;
        if (candidate->State != MembershipState::Authenticating) {
            _authentications.Release(candidateHandle);
            candidate->State = MembershipState::Discovered;
            return AdmissionPromotionResult::CandidateNotAuthenticating;
        }

        const RadioIdentifier radio = candidate->Radio;
        const Radio::RadioPeerHandle peer = candidate->Peer;
        const auto result = _memberships.UpsertAuthenticated(authenticatedDevice, authenticatedIncarnation,
                                                              MembershipState::Validating,
                                                              ReachabilityState::Reachable);
        _authentications.Release(candidateHandle);

        switch (result) {
            case AuthenticatedMembershipInsertResult::Inserted:
            case AuthenticatedMembershipInsertResult::Updated:
                if (establishedBinding != nullptr) {
                    *establishedBinding = AuthenticatedDirectPeerBinding{
                        authenticatedDevice, authenticatedIncarnation, radio, peer
                    };
                }
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

    bool CompleteRejected(NeighbourCandidateHandle candidateHandle) noexcept {
        const bool hadCandidate = _candidates.Resolve(candidateHandle) != nullptr;
        _authentications.Release(candidateHandle);
        if (hadCandidate) _candidates.Remove(candidateHandle);
        return hadCandidate;
    }

    bool ReleaseRetryable(NeighbourCandidateHandle candidateHandle) noexcept {
        auto* candidate = _candidates.Resolve(candidateHandle);
        if (candidate == nullptr) return false;
        const bool released = _authentications.Release(candidateHandle);
        candidate->State = MembershipState::Discovered;
        return released;
    }
};

} // namespace ESPressio::Mesh
