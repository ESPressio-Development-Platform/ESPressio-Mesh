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
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct LivenessProbeAssessment final {
    MembershipState Membership{MembershipState::Unknown};
    ReachabilityState Reachability{ReachabilityState::Unknown};
    bool HasAuthenticatedEvidence{false};
    std::uint64_t EvidenceAgeMilliseconds{0};
};

/// <summary>Injectable policy deciding whether passive/control evidence is insufficient and an active probe is useful.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic interface/object includes vptr storage where not supplied by a base.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class IMeshLivenessProbePolicy {
public:
    virtual ~IMeshLivenessProbePolicy() = default;

    /// <summary>
    /// Returns true when one exact authenticated incarnation should consume active-probe resources now.
    /// </summary>
    /// <remarks>
    /// Policy owns eligibility thresholds/cadence. The coordinator does not assume that Suspect or Unreachable
    /// necessarily means probe, nor does it assign a universal probing interval.
    /// </remarks>
    virtual bool ShouldProbe(const LivenessProbeAssessment& assessment) const noexcept = 0;
};

/// <summary>Immediate admission result from the technology/control-plane implementation that starts a probe.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class LivenessProbeStartDisposition : std::uint8_t {
    Started,
    TemporarilyUnavailable,
    Rejected
};

/// <summary>
/// Narrow execution boundary for initiating one active probe after policy and bounded-resource admission.
/// </summary>
/// <remarks>
/// This interface deliberately defines no wire format, PrimitiveFamilyId, transport route or cryptographic mechanism.
/// The implementation may use the Mesh control plane once its concrete family/wire contract is allocated. A response
/// becomes liveness evidence only after the owning authentication path validates it and separately calls
/// MembershipLivenessTracker::ObserveAuthenticatedEvidence.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic interface/object includes vptr storage where not supplied by a base.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class ILivenessProbeInitiator {
public:
    virtual ~ILivenessProbeInitiator() = default;
    virtual LivenessProbeStartDisposition TryStartProbe(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        LivenessProbeReservation reservation
    ) noexcept = 0;
};

/// <summary>Result of asking the coordinator to consider one authenticated member for active probing.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class LivenessProbeCoordinatorResult : std::uint8_t {
    Started,
    NotEligible,
    AlreadyInProgress,
    ResourceUnavailable,
    InitiatorTemporarilyUnavailable,
    InitiatorRejected,
    MemberNotFound,
    InvalidTime
};

/// <summary>
/// Composes authenticated membership, passive liveness evidence, injected eligibility policy and bounded probe slots.
/// </summary>
/// <remarks>
/// The coordinator owns no scheduler and no probe protocol. Callers choose when to evaluate candidates. It first
/// evaluates current reachability from authenticated evidence, then asks the injected policy whether active probing is
/// useful. A reservation is held only when the initiator accepts the probe. Temporary/rejected starts release capacity
/// immediately. Probe completion releases only execution capacity; authenticated probe responses must pass through the
/// normal authenticated-evidence path rather than being trusted merely because a probe was outstanding.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _members (AuthenticatedMembershipTable<MembershipCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _liveness (MembershipLivenessTracker<MembershipCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _reservations (LivenessProbeReservationTable<ProbeCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _policy (IMeshLivenessProbePolicy&): 4 bytes [0 bytes dynamic allocation]
 * - _initiator (ILivenessProbeInitiator&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 20 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
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

    /// <summary>Evaluates and, when eligible, begins one active liveness probe.</summary>
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

        switch (_initiator.TryStartProbe(device, incarnation, reservation)) {
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

    /// <summary>Releases one completed/cancelled active-probe execution reservation.</summary>
    bool Complete(LivenessProbeReservation reservation) noexcept {
        return _reservations.Release(reservation);
    }
};

} // namespace ESPressio::Mesh
