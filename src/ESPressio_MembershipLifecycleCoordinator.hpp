#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MembershipRetentionCoordinator.hpp"
#include "ESPressio_MeshLifecycleObservers.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of one policy-driven membership lifecycle evaluation.</summary>
enum class MembershipLifecycleResult : std::uint8_t {
    NoChange,
    ReachabilityChanged,
    ActivatedAuthenticated,
    RetiredLocallyForgotten,
    RetiredAuthoritativeLeave,
    RetiredSupersededIncarnation,
    MembershipNotFound,
    Invalid
};

/// <summary>
/// Narrow coordinator joining authenticated activation, liveness classification, bounded retention and lifecycle notification.
/// </summary>
/// <remarks>
/// Liveness policy decides reachability; MembershipLivenessTracker retains authenticated-evidence timing;
/// MembershipRetentionCoordinator owns tombstone-before-release retirement. Observable notifications are advisory views
/// of already-committed transitions and never grant authority or alter the result of a lifecycle operation.
/// </remarks>
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t TombstoneCapacity = Limits::MaxMembershipTombstones
>
class MembershipLifecycleCoordinator final {
public:
    using MembershipTable = AuthenticatedMembershipTable<MembershipCapacity>;
    using LivenessTracker = MembershipLivenessTracker<MembershipCapacity>;
    using RetentionCoordinator = MembershipRetentionCoordinator<MembershipCapacity, TombstoneCapacity>;

private:
    MembershipTable& _memberships;
    LivenessTracker& _liveness;
    RetentionCoordinator& _retention;
    MeshLifecycleNotifications* _notifications{nullptr};

    MeshNodeLifecycleNotification Snapshot(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        MeshNodeLifecycleReason reason = MeshNodeLifecycleReason::None
    ) const noexcept {
        const auto* member = _memberships.FindExact(device, incarnation);
        return {
            device,
            incarnation,
            member == nullptr ? MembershipState::Unknown : member->State,
            member == nullptr ? ReachabilityState::Unknown : member->Reachability,
            reason
        };
    }

public:
    MembershipLifecycleCoordinator(
        MembershipTable& memberships,
        LivenessTracker& liveness,
        RetentionCoordinator& retention,
        MeshLifecycleNotifications* notifications = nullptr
    ) noexcept :
        _memberships(memberships), _liveness(liveness), _retention(retention),
        _notifications(notifications) {}

    void SetNotifications(MeshLifecycleNotifications* notifications) noexcept {
        _notifications = notifications;
    }

    /// <summary>
    /// Commits the post-admission Joining -> Active transition and establishes the initial bounded authenticated liveness lease.
    /// </summary>
    /// <remarks>
    /// Authentication/admission has already established the exact membership record before this method is called. The
    /// activation timestamp is valid liveness evidence because successful session establishment is authenticated evidence;
    /// it is intentionally not refreshed by unauthenticated discovery/Hello traffic.
    /// </remarks>
    MembershipLifecycleResult ActivateAuthenticated(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!device || !incarnation || nowMilliseconds == 0U) return MembershipLifecycleResult::Invalid;
        auto* member = _memberships.FindExact(device, incarnation);
        if (member == nullptr) return MembershipLifecycleResult::MembershipNotFound;
        if (member->State != MembershipState::Validating &&
            member->State != MembershipState::Joining &&
            member->State != MembershipState::Active) {
            return MembershipLifecycleResult::Invalid;
        }

        if (member->State != MembershipState::Active) {
            if (!_memberships.SetMembershipState(device, incarnation, MembershipState::Joining)) {
                return MembershipLifecycleResult::Invalid;
            }
            if (_notifications != nullptr) {
                _notifications->NotifyJoining(Snapshot(device, incarnation));
            }
            if (!_memberships.SetMembershipState(device, incarnation, MembershipState::Active)) {
                return MembershipLifecycleResult::Invalid;
            }
        }
        if (!_memberships.SetReachability(device, incarnation, ReachabilityState::Reachable) ||
            !_liveness.ObserveAuthenticatedEvidence(device, incarnation, nowMilliseconds)) {
            return MembershipLifecycleResult::Invalid;
        }
        if (_notifications != nullptr) {
            _notifications->NotifyAuthenticated(Snapshot(device, incarnation));
        }
        return MembershipLifecycleResult::ActivatedAuthenticated;
    }

    /// <summary>
    /// Applies liveness policy and locally forgets an exact incarnation only after full-record retention expires.
    /// </summary>
    MembershipLifecycleResult Evaluate(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t unreachableRetentionMilliseconds =
            Limits::UnreachableMemberRetentionMilliseconds,
        std::uint64_t tombstoneRetentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        if (!device || !incarnation || nowMilliseconds == 0U ||
            unreachableRetentionMilliseconds == 0U || tombstoneRetentionMilliseconds == 0U) {
            return MembershipLifecycleResult::Invalid;
        }

        auto* before = _memberships.FindExact(device, incarnation);
        if (before == nullptr) return MembershipLifecycleResult::MembershipNotFound;
        const auto previousReachability = before->Reachability;

        const auto currentReachability = _liveness.Evaluate(device, incarnation, nowMilliseconds);
        if (_memberships.FindExact(device, incarnation) == nullptr) {
            return MembershipLifecycleResult::MembershipNotFound;
        }

        if (currentReachability != previousReachability &&
            currentReachability == ReachabilityState::Unreachable && _notifications != nullptr) {
            _notifications->NotifyUnavailable(Snapshot(device, incarnation));
        }

        if (_liveness.IsUnreachableRetentionElapsed(
                device,
                incarnation,
                nowMilliseconds,
                unreachableRetentionMilliseconds)) {
            const auto beforeRetirement = Snapshot(
                device, incarnation, MeshNodeLifecycleReason::UnreachableTimeout);
            const auto retirement = _retention.RecordLocallyForgotten(
                device,
                incarnation,
                nowMilliseconds,
                tombstoneRetentionMilliseconds
            );
            if (retirement == MembershipRetirementResult::Retired) {
                _liveness.Forget(device, incarnation);
                if (_notifications != nullptr) _notifications->NotifyLost(beforeRetirement);
                return MembershipLifecycleResult::RetiredLocallyForgotten;
            }
            if (retirement == MembershipRetirementResult::MembershipNotFound) {
                return MembershipLifecycleResult::MembershipNotFound;
            }
            return MembershipLifecycleResult::Invalid;
        }

        return currentReachability != previousReachability
            ? MembershipLifecycleResult::ReachabilityChanged
            : MembershipLifecycleResult::NoChange;
    }

    /// <summary>Commits an already-authenticated authoritative graceful Leave and emits Disconnected after retirement.</summary>
    MembershipLifecycleResult RecordAuthoritativeLeave(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t tombstoneRetentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        if (!device || !incarnation || nowMilliseconds == 0U || tombstoneRetentionMilliseconds == 0U) {
            return MembershipLifecycleResult::Invalid;
        }
        if (_memberships.FindExact(device, incarnation) == nullptr) {
            return MembershipLifecycleResult::MembershipNotFound;
        }
        const auto event = Snapshot(device, incarnation, MeshNodeLifecycleReason::AuthoritativeLeave);
        const auto retirement = _retention.RecordAuthoritativeLeave(
            device, incarnation, nowMilliseconds, tombstoneRetentionMilliseconds);
        if (retirement == MembershipRetirementResult::MembershipNotFound) {
            return MembershipLifecycleResult::MembershipNotFound;
        }
        if (retirement != MembershipRetirementResult::Retired) return MembershipLifecycleResult::Invalid;
        _liveness.Forget(device, incarnation);
        if (_notifications != nullptr) _notifications->NotifyDisconnected(event);
        return MembershipLifecycleResult::RetiredAuthoritativeLeave;
    }

    /// <summary>
    /// Retires an older incarnation only after a higher authentication path has established a different incarnation
    /// for the same DeviceIdentifier. The old record is tombstoned before release and observers see it as Lost with
    /// SupersededIncarnation reason; an unauthenticated discovery claim must never call this operation.
    /// </summary>
    MembershipLifecycleResult RecordSupersededIncarnation(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t tombstoneRetentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        if (!device || !incarnation || nowMilliseconds == 0U || tombstoneRetentionMilliseconds == 0U) {
            return MembershipLifecycleResult::Invalid;
        }
        if (_memberships.FindExact(device, incarnation) == nullptr) {
            return MembershipLifecycleResult::MembershipNotFound;
        }
        const auto event = Snapshot(device, incarnation, MeshNodeLifecycleReason::SupersededIncarnation);
        const auto retirement = _retention.RecordSupersededIncarnation(
            device, incarnation, nowMilliseconds, tombstoneRetentionMilliseconds);
        if (retirement == MembershipRetirementResult::MembershipNotFound) {
            return MembershipLifecycleResult::MembershipNotFound;
        }
        if (retirement != MembershipRetirementResult::Retired) return MembershipLifecycleResult::Invalid;
        _liveness.Forget(device, incarnation);
        if (_notifications != nullptr) _notifications->NotifyLost(event);
        return MembershipLifecycleResult::RetiredSupersededIncarnation;
    }
};

} // namespace ESPressio::Mesh