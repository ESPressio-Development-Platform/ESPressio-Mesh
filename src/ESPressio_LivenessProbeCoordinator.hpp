#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_LivenessProbeReservations.hpp"
#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Read-only local evidence supplied to active-liveness-probe eligibility policy.</summary>
struct LivenessProbeAssessment final {
    MembershipState Membership{MembershipState::Unknown};
    ReachabilityState Reachability{ReachabilityState::Unknown};
    bool HasAuthenticatedEvidence{false};
    std::uint64_t EvidenceAgeMilliseconds{0};
};

/// <summary>Injectable policy deciding whether passive/control evidence is insufficient and an active probe is useful.</summary>
class IMeshLivenessProbePolicy {
public:
    virtual ~IMeshLivenessProbePolicy() = default;
    virtual bool ShouldProbe(const LivenessProbeAssessment& assessment) const noexcept = 0;
};

/// <summary>Immediate admission result from the technology/control-plane implementation that starts a probe.</summary>
enum class LivenessProbeStartDisposition : std::uint8_t {
    Started,
    TemporarilyUnavailable,
    Rejected
};

/// <summary>Narrow execution boundary for initiating one active probe after policy and bounded-resource admission.</summary>
/// <remarks>
/// Monotonic `nowMilliseconds` is supplied by the serialized Mesh caller so the concrete initiator can derive a finite
/// transport residence deadline without owning another clock/task. A response becomes liveness evidence only after the
/// owning authentication path validates it and separately calls MembershipLivenessTracker::ObserveAuthenticatedEvidence.
/// </remarks>
class ILivenessProbeInitiator {
public:
    virtual ~ILivenessProbeInitiator() = default;
    virtual LivenessProbeStartDisposition TryStartProbe(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        LivenessProbeReservation reservation
    ) noexcept = 0;
};

/// <summary>Result of asking the coordinator to consider one authenticated member for active probing.</summary>
enum class LivenessProbeCoordinatorResult : std::uint8_t {
    Started,
    NotEligible,
    AlreadyInProgress,
    ResourceUnavailable,
    InitiatorTemporarilyUnavailable,
    InitiatorRejected,
    MemberNotFound,
    InvalidTime
};

/// <summary>Composes authenticated membership, passive evidence, policy and bounded active-probe reservations.</summary>
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t ProbeCapacity = Limits::MaxActiveLivenessProbes
>
class LivenessProbeCoordinator final {
    AuthenticatedMembershipTable<MembershipCapacity>& _members;
    MembershipLivenessTracker<MembershipCapacity>& _liveness;
    LivenessProbeReservationTable<ProbeCapacity>& _reservations;
    const IMeshLivenessProbePolicy& _policy;
    ILivenessProbeInitiator& _initiator;

public:
    LivenessProbeCoordinator(
        AuthenticatedMembershipTable<MembershipCapacity>& members,
        MembershipLivenessTracker<MembershipCapacity>& liveness,
        LivenessProbeReservationTable<ProbeCapacity>& reservations,
        const IMeshLivenessProbePolicy& policy,
        ILivenessProbeInitiator& initiator
    ) noexcept :
        _members(members),
        _liveness(liveness),
        _reservations(reservations),
        _policy(policy),
        _initiator(initiator) {}

    LivenessProbeCoordinatorResult Consider(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        LivenessProbeReservation& reservation
    ) noexcept {
        reservation = {};
        if (nowMilliseconds == 0U) return LivenessProbeCoordinatorResult::InvalidTime;

        auto* member = _members.FindExact(device, incarnation);
        if (member == nullptr) return LivenessProbeCoordinatorResult::MemberNotFound;

        const auto reachability = _liveness.Evaluate(device, incarnation, nowMilliseconds);
        const auto* evidence = _liveness.EvidenceFor(device, incarnation);
        const bool hasEvidence = evidence != nullptr && evidence->HasEvidence();
        const std::uint64_t evidenceAge = hasEvidence && nowMilliseconds >= evidence->LastEvidenceMilliseconds
            ? nowMilliseconds - evidence->LastEvidenceMilliseconds
            : 0U;

        const LivenessProbeAssessment assessment{
            member->State,
            reachability,
            hasEvidence,
            evidenceAge
        };
        if (!_policy.ShouldProbe(assessment)) return LivenessProbeCoordinatorResult::NotEligible;

        const auto reserveResult = _reservations.TryReserve(device, incarnation, reservation);
        switch (reserveResult) {
            case LivenessProbeReservationResult::AlreadyInProgress:
                return LivenessProbeCoordinatorResult::AlreadyInProgress;
            case LivenessProbeReservationResult::ResourceUnavailable:
                reservation = {};
                return LivenessProbeCoordinatorResult::ResourceUnavailable;
            case LivenessProbeReservationResult::Invalid:
                reservation = {};
                return LivenessProbeCoordinatorResult::MemberNotFound;
            case LivenessProbeReservationResult::Reserved:
                break;
        }

        switch (_initiator.TryStartProbe(device, incarnation, nowMilliseconds, reservation)) {
            case LivenessProbeStartDisposition::Started:
                return LivenessProbeCoordinatorResult::Started;
            case LivenessProbeStartDisposition::TemporarilyUnavailable:
                _reservations.Release(reservation);
                reservation = {};
                return LivenessProbeCoordinatorResult::InitiatorTemporarilyUnavailable;
            case LivenessProbeStartDisposition::Rejected:
                _reservations.Release(reservation);
                reservation = {};
                return LivenessProbeCoordinatorResult::InitiatorRejected;
        }

        _reservations.Release(reservation);
        reservation = {};
        return LivenessProbeCoordinatorResult::InitiatorRejected;
    }

    bool Complete(LivenessProbeReservation reservation) noexcept {
        return _reservations.Release(reservation);
    }
};

} // namespace ESPressio::Mesh
