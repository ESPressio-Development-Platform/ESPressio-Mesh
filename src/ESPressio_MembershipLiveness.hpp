#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Authenticated liveness evidence retained for one current membership incarnation.</summary>
struct AuthenticatedLivenessEvidence final {
    std::uint64_t LastEvidenceMilliseconds{0};
    std::uint64_t UnreachableSinceMilliseconds{0};

    /// <summary>Returns whether at least one authenticated evidence item has been observed.</summary>
    constexpr bool HasEvidence() const noexcept { return LastEvidenceMilliseconds != 0U; }
};

/// <summary>Policy contract that classifies elapsed time since the last authenticated Mesh evidence.</summary>
class IMeshLivenessPolicy {
public:
    virtual ~IMeshLivenessPolicy() = default;

    /// <summary>
    /// Classifies current reachability from monotonic elapsed time since the last authenticated evidence.
    /// </summary>
    virtual ReachabilityState Classify(std::uint64_t elapsedMilliseconds) const noexcept = 0;
};

/// <summary>
/// Conservative default liveness policy using the architecture's approximate 5 s / 15 s baseline floors.
/// </summary>
/// <remarks>
/// The thresholds are policy configuration, not membership-storage constants. Applications may inject a
/// different policy without changing membership representation or distributed identity semantics.
/// </remarks>
class DefaultMeshLivenessPolicy final : public IMeshLivenessPolicy {
    std::uint64_t _suspectAfterMilliseconds;
    std::uint64_t _unreachableAfterMilliseconds;

public:
    explicit constexpr DefaultMeshLivenessPolicy(
        std::uint64_t suspectAfterMilliseconds = 5'000ULL,
        std::uint64_t unreachableAfterMilliseconds = 15'000ULL
    ) noexcept
        : _suspectAfterMilliseconds(suspectAfterMilliseconds),
          _unreachableAfterMilliseconds(unreachableAfterMilliseconds) {}

    ReachabilityState Classify(std::uint64_t elapsedMilliseconds) const noexcept override {
        if (_suspectAfterMilliseconds == 0U ||
            _unreachableAfterMilliseconds <= _suspectAfterMilliseconds) {
            return ReachabilityState::Unknown;
        }
        if (elapsedMilliseconds >= _unreachableAfterMilliseconds) {
            return ReachabilityState::Unreachable;
        }
        if (elapsedMilliseconds >= _suspectAfterMilliseconds) {
            return ReachabilityState::Suspect;
        }
        return ReachabilityState::Reachable;
    }
};

/// <summary>
/// Applies authenticated evidence and policy-driven reachability transitions to bounded membership records.
/// </summary>
/// <remarks>
/// This service is intended for the serialized Mesh execution domain. It never authenticates evidence itself:
/// callers must invoke ObserveAuthenticatedEvidence only after source/incarnation authentication succeeds.
/// Valid authenticated evidence restores Reachable immediately. Time-based degradation is local evidence only
/// and never changes MembershipState or authoritatively removes a member.
/// </remarks>
template<std::size_t Capacity = Limits::MaxMeshNodes>
class MembershipLivenessTracker final {
    struct Slot final {
        System::DeviceIdentifier Device{};
        MembershipIncarnation Incarnation{};
        AuthenticatedLivenessEvidence Evidence{};
        bool Occupied{false};
    };

    AuthenticatedMembershipTable<Capacity>& _members;
    const IMeshLivenessPolicy& _policy;
    std::array<Slot, Capacity> _slots{};

    Slot* FindSlot(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        for (auto& slot : _slots) {
            if (slot.Occupied && slot.Device == device && slot.Incarnation == incarnation) return &slot;
        }
        return nullptr;
    }

    const Slot* FindSlot(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Device == device && slot.Incarnation == incarnation) return &slot;
        }
        return nullptr;
    }

    Slot* EnsureSlot(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        if (auto* existing = FindSlot(device, incarnation)) return existing;
        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Device = device;
            slot.Incarnation = incarnation;
            slot.Evidence = {};
            slot.Occupied = true;
            return &slot;
        }
        return nullptr;
    }

public:
    MembershipLivenessTracker(
        AuthenticatedMembershipTable<Capacity>& members,
        const IMeshLivenessPolicy& policy
    ) noexcept : _members(members), _policy(policy) {}

    /// <summary>Returns retained authenticated evidence for one exact current incarnation, or null when none exists.</summary>
    const AuthenticatedLivenessEvidence* EvidenceFor(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        const auto* slot = FindSlot(device, incarnation);
        return slot == nullptr ? nullptr : &slot->Evidence;
    }

    /// <summary>Records valid authenticated evidence and restores the exact current incarnation to Reachable.</summary>
    bool ObserveAuthenticatedEvidence(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (nowMilliseconds == 0U) return false;
        auto* member = _members.FindExact(device, incarnation);
        if (member == nullptr) return false;
        auto* slot = EnsureSlot(device, incarnation);
        if (slot == nullptr) return false;

        if (slot->Evidence.HasEvidence() && nowMilliseconds < slot->Evidence.LastEvidenceMilliseconds) {
            return false;
        }
        slot->Evidence.LastEvidenceMilliseconds = nowMilliseconds;
        slot->Evidence.UnreachableSinceMilliseconds = 0U;
        member->Reachability = ReachabilityState::Reachable;
        return true;
    }

    /// <summary>Applies the injected policy to one exact authenticated incarnation at the supplied monotonic time.</summary>
    ReachabilityState Evaluate(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        auto* member = _members.FindExact(device, incarnation);
        auto* slot = FindSlot(device, incarnation);
        if (member == nullptr || slot == nullptr || !slot->Evidence.HasEvidence() ||
            nowMilliseconds < slot->Evidence.LastEvidenceMilliseconds) {
            return member == nullptr ? ReachabilityState::Unknown : member->Reachability;
        }

        const auto elapsed = nowMilliseconds - slot->Evidence.LastEvidenceMilliseconds;
        const auto classified = _policy.Classify(elapsed);
        if (classified == ReachabilityState::Unknown) return member->Reachability;

        if (classified == ReachabilityState::Unreachable) {
            if (member->Reachability != ReachabilityState::Unreachable ||
                slot->Evidence.UnreachableSinceMilliseconds == 0U) {
                slot->Evidence.UnreachableSinceMilliseconds = nowMilliseconds;
            }
        } else {
            slot->Evidence.UnreachableSinceMilliseconds = 0U;
        }
        member->Reachability = classified;
        return classified;
    }

    /// <summary>
    /// Returns whether an exact incarnation has remained Unreachable for the configured full-record retention.
    /// </summary>
    bool IsUnreachableRetentionElapsed(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds = Limits::UnreachableMemberRetentionMilliseconds
    ) noexcept {
        if (retentionMilliseconds == 0U) return false;
        const auto* member = _members.FindExact(device, incarnation);
        auto* slot = FindSlot(device, incarnation);
        if (member == nullptr || slot == nullptr ||
            member->Reachability != ReachabilityState::Unreachable ||
            slot->Evidence.UnreachableSinceMilliseconds == 0U ||
            nowMilliseconds < slot->Evidence.UnreachableSinceMilliseconds) {
            return false;
        }
        return (nowMilliseconds - slot->Evidence.UnreachableSinceMilliseconds) >= retentionMilliseconds;
    }

    /// <summary>Forgets local liveness evidence after the owning membership record has been retired.</summary>
    bool Forget(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        if (auto* slot = FindSlot(device, incarnation)) {
            *slot = {};
            return true;
        }
        return false;
    }
};

} // namespace ESPressio::Mesh
