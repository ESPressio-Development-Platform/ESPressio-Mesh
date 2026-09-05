#pragma once

#include <cstdint>
#include <limits>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Non-wrapping monotonic MeshMessageId issuer for one authenticated DeviceIdentifier + MembershipIncarnation scope.
/// </summary>
/// <remarks>
/// The owning membership context provides the semantic scope. A new generator therefore begins at 1 for a genuinely
/// new MembershipIncarnation. Exhaustion is terminal for that scope: identifier reuse is never permitted.
/// </remarks>
class MeshMessageIdGenerator final {
    MeshMessageId _lastIssued{0};

public:
    /// <summary>Returns the last issued identifier, or zero before the first successful issue.</summary>
    constexpr MeshMessageId LastIssued() const noexcept { return _lastIssued; }

    /// <summary>Returns whether this source/incarnation sequence has reached its non-wrapping terminal value.</summary>
    constexpr bool IsExhausted() const noexcept {
        return _lastIssued == std::numeric_limits<MeshMessageId>::max();
    }

    /// <summary>
    /// Attempts to issue the next non-zero monotonically increasing MeshMessageId.
    /// </summary>
    /// <param name="identifier">Receives the fresh identifier on success and is left unchanged on exhaustion.</param>
    /// <returns><c>true</c> when a fresh identifier was issued; <c>false</c> after terminal exhaustion.</returns>
    bool TryIssue(MeshMessageId& identifier) noexcept {
        if (IsExhausted()) return false;
        ++_lastIssued;
        identifier = _lastIssued;
        return true;
    }

    /// <summary>
    /// Restores an already issued high-water value when the owning membership context deliberately restores continuity.
    /// </summary>
    /// <remarks>
    /// This operation does not decide whether continuity is legitimate. The caller must only use it after the security/
    /// membership authority has established authenticated continuation of the same MembershipIncarnation.
    /// </remarks>
    bool RestoreHighWater(MeshMessageId lastIssued) noexcept {
        if (lastIssued < _lastIssued) return false;
        _lastIssued = lastIssued;
        return true;
    }

    /// <summary>Restarts identifier issuance for a security-authorized genuinely new membership incarnation.</summary>
    /// <remarks>
    /// This operation must never be used to recover or restart the same incarnation because doing so would reuse
    /// conceptual message identifiers. LocalMeshIdentityLifecycleCoordinator is the normal composition boundary.
    /// </remarks>
    void ResetForNewIncarnation() noexcept { _lastIssued = 0U; }
};

static_assert(sizeof(MeshMessageIdGenerator) == sizeof(MeshMessageId),
              "MeshMessageIdGenerator must retain only the source/incarnation-scoped high-water value.");

} // namespace ESPressio::Mesh
