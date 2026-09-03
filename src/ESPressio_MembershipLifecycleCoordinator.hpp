#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MembershipRetentionCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of one policy-driven membership lifecycle evaluation.</summary>
enum class MembershipLifecycleResult : std::uint8_t {
    NoChange,
    ReachabilityChanged,
    RetiredLocallyForgotten,
    MembershipNotFound,
    Invalid
};

/// <summary>
/// Narrow coordinator joining liveness classification with bounded full-record retention and local forgetting.
/// </summary>
/// <remarks>
/// Liveness policy decides reachability; MembershipLivenessTracker retains authenticated-evidence timing;
/// MembershipRetentionCoordinator owns tombstone-before-release retirement. This coordinator only sequences
/// those existing responsibilities. It does not authenticate evidence, choose policy thresholds, create
/// tombstones directly or redefine authoritative MembershipState.
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

public:
    MembershipLifecycleCoordinator(
        MembershipTable& memberships,
        LivenessTracker& liveness,
        RetentionCoordinator& retention
    ) noexcept : _memberships(memberships), _liveness(liveness), _retention(retention) {}

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

        if (_liveness.IsUnreachableRetentionElapsed(
                device,
                incarnation,
                nowMilliseconds,
                unreachableRetentionMilliseconds)) {
            const auto retirement = _retention.RecordLocallyForgotten(
                device,
                incarnation,
                nowMilliseconds,
                tombstoneRetentionMilliseconds
            );
            if (retirement == MembershipRetirementResult::Retired) {
                _liveness.Forget(device, incarnation);
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
};

} // namespace ESPressio::Mesh
