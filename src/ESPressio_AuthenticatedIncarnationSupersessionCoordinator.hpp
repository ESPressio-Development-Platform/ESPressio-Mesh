#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_MembershipLifecycleCoordinator.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of retiring an older authenticated incarnation before committing its authenticated replacement.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class AuthenticatedIncarnationSupersessionResult : std::uint8_t {
    /// <summary>No older retained incarnation exists, or the retained incarnation is already the replacement.</summary>
    NoSupersessionRequired,
    /// <summary>The old provider session, exact direct bindings and membership were retired in order.</summary>
    Superseded,
    /// <summary>The old provider-owned session could not be released; no membership/binding mutation was performed.</summary>
    SessionReleaseFailed,
    /// <summary>A post-release invariant failed inside the required serialized Mesh mutation domain.</summary>
    InvariantViolation,
    Invalid
};

/// <summary>
/// Coordinates local retirement of an authenticated old incarnation after a different replacement incarnation has
/// already been authenticated, but before that replacement is committed into membership/session tables.
/// </summary>
/// <remarks>
/// This is deliberately separate from discovery and admission policy. A merely claimed/newer incarnation must never
/// call it. The caller must invoke it only after the replacement handshake has established authenticated identity.
///
/// The operation is intended for the same serialized Mesh mutation domain as membership/session admission. It first
/// releases the old provider-owned security session. If that can fail, the method returns without changing membership
/// or direct bindings. Once release succeeds, exact old-incarnation bindings are removed and membership retirement is
/// deterministic: MembershipRetentionCoordinator guarantees tombstone saturation cannot block retirement, and the
/// prevalidated exact membership cannot disappear in the serialized domain. Failure after session release is therefore
/// classified as an invariant violation rather than a retryable partial transition.
///
/// Lifecycle notification remains owned by MembershipLifecycleCoordinator and is emitted only after exact membership
/// retirement has committed. Observers therefore cannot see a SupersededIncarnation notification while an executable
/// old direct binding or old security session is still retained by these stores.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _memberships (MembershipTable&): 4 bytes [0 bytes dynamic allocation]
 * - _lifecycle (MembershipLifecycle&): 4 bytes [0 bytes dynamic allocation]
 * - _sessions (SessionTable&): 4 bytes [0 bytes dynamic allocation]
 * - _bindings (BindingTable&): 4 bytes [0 bytes dynamic allocation]
 * - _provider (IMeshV1CryptographicProvider&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 20 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t TombstoneCapacity = Limits::MaxMembershipTombstones,
    std::size_t SessionCapacity = Limits::MaxMeshNodes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks
>
class AuthenticatedIncarnationSupersessionCoordinator final {
public:
    using MembershipTable = AuthenticatedMembershipTable<MembershipCapacity>;
    using MembershipLifecycle = MembershipLifecycleCoordinator<MembershipCapacity, TombstoneCapacity>;
    using SessionTable = MeshSecuritySessionTable<SessionCapacity>;
    using BindingTable = AuthenticatedDirectPeerBindingTable<BindingCapacity>;

private:
    MembershipTable& _memberships;
    MembershipLifecycle& _lifecycle;
    SessionTable& _sessions;
    BindingTable& _bindings;
    IMeshV1CryptographicProvider& _provider;

public:
    AuthenticatedIncarnationSupersessionCoordinator(
        MembershipTable& memberships,
        MembershipLifecycle& lifecycle,
        SessionTable& sessions,
        BindingTable& bindings,
        IMeshV1CryptographicProvider& provider
    ) noexcept :
        _memberships(memberships),
        _lifecycle(lifecycle),
        _sessions(sessions),
        _bindings(bindings),
        _provider(provider) {}

    /// <summary>
    /// Prepares one already-authenticated replacement incarnation by retiring any different retained incarnation.
    /// </summary>
    AuthenticatedIncarnationSupersessionResult PrepareAuthenticatedReplacement(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& replacementIncarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!device || !replacementIncarnation || nowMilliseconds == 0U) {
            return AuthenticatedIncarnationSupersessionResult::Invalid;
        }

        const auto* retained = _memberships.FindDevice(device);
        if (retained == nullptr || retained->Incarnation == replacementIncarnation) {
            return AuthenticatedIncarnationSupersessionResult::NoSupersessionRequired;
        }

        const auto oldIncarnation = retained->Incarnation;
        if (!oldIncarnation || _memberships.FindExact(device, oldIncarnation) == nullptr) {
            return AuthenticatedIncarnationSupersessionResult::InvariantViolation;
        }

        const auto oldSession = _sessions.Find(device, oldIncarnation);
        if (oldSession && !_sessions.Release(oldSession, _provider)) {
            return AuthenticatedIncarnationSupersessionResult::SessionReleaseFailed;
        }

        // No new direct binding can legitimately exist yet: replacement admission has not committed. Remove only
        // bindings carrying the exact superseded incarnation so this operation remains safe if the table later grows
        // support for multiple simultaneously represented incarnations during staged transitions.
        (void)_bindings.RemoveNeighbour(device, oldIncarnation);

        const auto retirement = _lifecycle.RecordSupersededIncarnation(
            device, oldIncarnation, nowMilliseconds);
        if (retirement != MembershipLifecycleResult::RetiredSupersededIncarnation) {
            return AuthenticatedIncarnationSupersessionResult::InvariantViolation;
        }

        return AuthenticatedIncarnationSupersessionResult::Superseded;
    }
};

} // namespace ESPressio::Mesh
