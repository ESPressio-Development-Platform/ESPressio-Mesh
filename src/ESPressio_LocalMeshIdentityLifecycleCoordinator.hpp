#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshMessageIdGenerator.hpp"
#include "ESPressio_MeshRadioRegistry.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of changing or restoring the local authenticated membership-incarnation scope.</summary>
enum class LocalMeshIdentityLifecycleResult : std::uint8_t {
    StartedNewIncarnation,
    RestoredContinuation,
    InvalidIncarnation,
    IncarnationConflict,
    HighWaterRegression
};

/// <summary>
/// Coordinates the local MembershipIncarnation with its message-ID sequence and RadioIdentifier allocation scope.
/// </summary>
/// <remarks>
/// This class does not generate, authenticate or persist an incarnation. Those are security/composition concerns.
/// Starting a genuinely new incarnation atomically invalidates all local RadioIdentifier bindings and restarts
/// MeshMessageId issuance at one. The application must subsequently re-register its Radio interfaces.
///
/// Authenticated continuation never resets either scope. It may only establish an initially empty composition or
/// advance/equal the retained MessageId high-water value for the same active incarnation; regression is rejected so
/// an older persistence snapshot cannot cause identifier reuse.
/// </remarks>
template<std::size_t RadioCapacity = Limits::MaxRadiosPerNode>
class LocalMeshIdentityLifecycleCoordinator final {
    MembershipIncarnation _incarnation{};
    MeshMessageIdGenerator& _messageIds;
    MeshRadioRegistry<RadioCapacity>& _radios;

public:
    LocalMeshIdentityLifecycleCoordinator(
        MeshMessageIdGenerator& messageIds,
        MeshRadioRegistry<RadioCapacity>& radios
    ) noexcept : _messageIds(messageIds), _radios(radios) {}

    /// <summary>Returns the active local incarnation, or zero when none has been established.</summary>
    constexpr const MembershipIncarnation& Incarnation() const noexcept { return _incarnation; }

    /// <summary>Returns whether a valid local incarnation is currently established.</summary>
    constexpr bool IsActive() const noexcept { return static_cast<bool>(_incarnation); }

    /// <summary>
    /// Starts a genuinely new, externally authorized incarnation and resets every incarnation-scoped local handle.
    /// </summary>
    LocalMeshIdentityLifecycleResult StartNewIncarnation(MembershipIncarnation incarnation) noexcept {
        if (!incarnation) return LocalMeshIdentityLifecycleResult::InvalidIncarnation;
        if (_incarnation == incarnation) return LocalMeshIdentityLifecycleResult::IncarnationConflict;

        _radios.ResetForNewIncarnation();
        _messageIds.ResetForNewIncarnation();
        _incarnation = incarnation;
        return LocalMeshIdentityLifecycleResult::StartedNewIncarnation;
    }

    /// <summary>
    /// Restores authenticated continuity of an existing incarnation without resetting local incarnation-scoped state.
    /// </summary>
    LocalMeshIdentityLifecycleResult RestoreAuthenticatedContinuation(
        MembershipIncarnation incarnation,
        MeshMessageId lastIssued
    ) noexcept {
        if (!incarnation) return LocalMeshIdentityLifecycleResult::InvalidIncarnation;
        if (_incarnation && _incarnation != incarnation) {
            return LocalMeshIdentityLifecycleResult::IncarnationConflict;
        }
        if (!_messageIds.RestoreHighWater(lastIssued)) {
            return LocalMeshIdentityLifecycleResult::HighWaterRegression;
        }

        _incarnation = incarnation;
        return LocalMeshIdentityLifecycleResult::RestoredContinuation;
    }
};

} // namespace ESPressio::Mesh
