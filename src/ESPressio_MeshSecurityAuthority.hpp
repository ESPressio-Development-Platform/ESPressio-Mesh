#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AdmissionResources.hpp"

namespace ESPressio::Mesh {

/// <summary>Identity established by an injected Mesh security authority after authentication succeeds.</summary>
/// <remarks>
/// These values are authoritative only because the security authority established them. They must never be populated
/// by copying the candidate's untrusted claim without authentication. DeviceIdentifier itself is not authentication.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - Device (System::DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - Incarnation (MembershipIncarnation): 16 bytes [0 bytes dynamic allocation]
 * Total Memory: 32 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct AuthenticatedMeshIdentity final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Device) && static_cast<bool>(Incarnation);
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Wire-neutral context identifying the pre-authentication neighbour work being evaluated.</summary>
/// <remarks>
/// Claim is explicitly untrusted input. Radio + Peer identify only the local direct-link observation through which the
/// candidate is currently reachable. No field grants identity authority and no field defines a security wire format.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - Candidate (NeighbourCandidateHandle): 4 bytes [0 bytes dynamic allocation]
 * - Radio (RadioIdentifier): 1 bytes [0 bytes dynamic allocation]
 * - Peer (Radio::RadioPeerHandle): 4 bytes [0 bytes dynamic allocation]
 * - Claim (UntrustedMembershipClaim): 32 bytes [0 bytes dynamic allocation]
 * Total Memory: 42 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct MeshSecurityCandidateContext final {
    NeighbourCandidateHandle Candidate{};
    RadioIdentifier Radio{0};
    Radio::RadioPeerHandle Peer{};
    UntrustedMembershipClaim Claim{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Candidate) && Radio != 0U && Radio != 0xFFU &&
               static_cast<bool>(Peer) && static_cast<bool>(Claim.Device) && static_cast<bool>(Claim.Incarnation);
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Outcome of one non-blocking security-authority evaluation of a pending neighbour candidate.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class MeshAuthenticationDisposition : std::uint8_t {
    /// <summary>Exact authenticated DeviceIdentifier + MembershipIncarnation were established.</summary>
    Authenticated,
    /// <summary>Security work remains active; caller retains the current bounded authentication reservation.</summary>
    Pending,
    /// <summary>The attempt can be retried later after releasing current expensive-work reservation.</summary>
    Retryable,
    /// <summary>The candidate definitively failed authentication and should not be promoted.</summary>
    Rejected,
    /// <summary>The security authority cannot currently accept more work.</summary>
    ResourceUnavailable,
    /// <summary>Candidate/context/evidence was invalid for security evaluation.</summary>
    Invalid
};

/// <summary>
/// Injected security authority that establishes authenticated Mesh identity without prescribing cryptography or wire schema.
/// </summary>
/// <remarks>
/// Implementations may compose ESPressio-Security primitives, hardware-backed credentials, application PKI, pre-shared
/// credentials or another policy-approved mechanism. Mesh owns none of those choices. Implementations may maintain their
/// own bounded challenge/session state and receive protocol evidence through their own composition surface; this narrow
/// interface only asks for the current outcome for one Mesh candidate.
///
/// On Authenticated, identity must be valid and is the only identity that may be passed to authenticated membership
/// promotion. For every other disposition, the caller must ignore identity.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IMeshSecurityAuthority {
public:
    virtual ~IMeshSecurityAuthority() = default;

    virtual MeshAuthenticationDisposition EvaluateCandidate(
        const MeshSecurityCandidateContext& candidate,
        std::uint64_t nowMilliseconds,
        AuthenticatedMeshIdentity& identity
    ) noexcept = 0;
};

/// <summary>Optional controlled-reset participant for staged pre-membership cryptographic state.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IMeshPendingAuthenticationReset {
public:
    virtual ~IMeshPendingAuthenticationReset() = default;
    virtual bool ReleasePendingAuthenticationBeforeProviderReset() noexcept = 0;
    virtual void ClearPendingAuthenticationAfterProviderReset() noexcept = 0;
};

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class MeshV1AdmissionResult : std::uint8_t {
    PromotedToValidating, AdmissionDeferred, Rejected, ConflictingIncarnation,
    MembershipResourceUnavailable, SessionResourceUnavailable, CleanupFailed,
    HandshakeNotAuthenticated, CandidateNotFound, Invalid
};

/// <summary>Input supplied to admission policy only after security established exact authenticated identity.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Candidate (MeshSecurityCandidateContext): 42 bytes [0 bytes dynamic allocation]
 * - Identity (AuthenticatedMeshIdentity): 32 bytes [0 bytes dynamic allocation]
 * - CurrentMembershipCount (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - MaximumMembershipCount (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 84 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct MeshAdmissionContext final {
    MeshSecurityCandidateContext Candidate{};
    AuthenticatedMeshIdentity Identity{};
    std::size_t CurrentMembershipCount{0};
    std::size_t MaximumMembershipCount{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Candidate) && static_cast<bool>(Identity) &&
               MaximumMembershipCount != 0U && CurrentMembershipCount <= MaximumMembershipCount;
    }
};

/// <summary>Outcome of applying local/distributed admission policy to an already-authenticated candidate.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class MeshAdmissionDisposition : std::uint8_t {
    Admit,
    Defer,
    Reject,
    Invalid
};

/// <summary>
/// Injected policy deciding whether an already-authenticated candidate may enter Mesh membership validation.
/// </summary>
/// <remarks>
/// The policy performs no cryptography and cannot replace authentication. Additional application/configuration state may
/// be captured by the concrete policy object rather than being embedded into Mesh. Compatibility/MeshSignature checks
/// that are independently defined remain separate prerequisites and are not fabricated by this interface.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IMeshAdmissionPolicy {
public:
    virtual ~IMeshAdmissionPolicy() = default;
    virtual MeshAdmissionDisposition EvaluateAdmission(const MeshAdmissionContext& context) const noexcept = 0;
};

} // namespace ESPressio::Mesh
