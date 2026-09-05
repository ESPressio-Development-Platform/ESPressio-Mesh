#pragma once

#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_ForwardingTransition.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Identity of one locally pending Mesh forwarding transition.</summary>
/// <remarks>
/// This is sender-local bookkeeping only. It is not an acknowledgement wire envelope and does not allocate or imply a
/// Mesh control PrimitiveFamilyId. The next-hop identity/incarnation must already have been authenticated when armed.
/// </remarks>
struct PendingForwardingTransition final {
    System::DeviceIdentifier NextHop{};
    MembershipIncarnation NextHopIncarnation{};
    MeshMessageId MessageId{0};
    std::uint64_t AbsoluteDeadlineMilliseconds{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(NextHop) &&
               static_cast<bool>(NextHopIncarnation) &&
               MessageId != 0U &&
               AbsoluteDeadlineMilliseconds != 0U;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Result of arming one per-delivery pending forwarding transition.</summary>
enum class ForwardingTransitionArmResult : std::uint8_t {
    Armed,
    AlreadyPending,
    DeadlineExpired,
    Invalid
};

/// <summary>Result of applying authenticated next-hop Mesh acceptance evidence.</summary>
enum class ForwardingAcceptanceResult : std::uint8_t {
    Committed,
    NotPending,
    WrongNextHop,
    WrongIncarnation,
    WrongMessage,
    DeadlineExpired,
    HopLimitExhausted,
    Invalid
};

/// <summary>
/// Per-delivery coordinator that commits HopLimit only after authenticated acceptance by the exact next Mesh hop.
/// </summary>
/// <remarks>
/// The coordinator owns exactly one fixed pending record and therefore introduces no hidden global cardinality. A caller
/// should embed one instance in already-bounded active delivery/forwarding state.
///
/// Radio submission, physical transmission completion and Radio peer acknowledgement are deliberately not inputs to
/// AcceptAuthenticated(). The caller must first establish a Mesh-level semantic fact that the exact next-hop
/// DeviceIdentifier + MembershipIncarnation accepted the same MeshMessageId. How that evidence is encoded, authenticated
/// or transported belongs to the Mesh control/security composition and is intentionally not defined here.
///
/// Identity is checked before deadline mutation. Authenticated evidence for another node, incarnation or MeshMessageId is
/// therefore observationally unrelated even after this transition's deadline and cannot consume pending state or HopLimit.
/// Deadline expiry is committed only when evidence identifies the exact pending transition; independent deadline sweeping
/// remains a higher delivery-lifecycle responsibility.
///
/// This next-hop acceptance is also distinct from end-to-end destination delivery acknowledgement tracked by
/// DeliveryAcknowledgementTracker: an intermediate next-hop acceptance transfers forwarding responsibility but does not
/// say that the final destination framework has accepted the message.
/// </remarks>
class ForwardingTransitionCoordinator final {
    PendingForwardingTransition _pending{};

public:
    constexpr bool HasPending() const noexcept { return static_cast<bool>(_pending); }
    constexpr const PendingForwardingTransition& Pending() const noexcept { return _pending; }

    /// <summary>Arms exact next-hop acceptance tracking using the delivery's immutable absolute deadline.</summary>
    ForwardingTransitionArmResult Arm(
        const System::DeviceIdentifier& nextHop,
        const MembershipIncarnation& nextHopIncarnation,
        MeshMessageId messageId,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) noexcept {
        if (!nextHop || !nextHopIncarnation || messageId == 0U || absoluteDeadlineMilliseconds == 0U) {
            return ForwardingTransitionArmResult::Invalid;
        }
        if (nowMilliseconds >= absoluteDeadlineMilliseconds) {
            return ForwardingTransitionArmResult::DeadlineExpired;
        }
        if (_pending) return ForwardingTransitionArmResult::AlreadyPending;
        _pending = PendingForwardingTransition{
            nextHop,
            nextHopIncarnation,
            messageId,
            absoluteDeadlineMilliseconds
        };
        return ForwardingTransitionArmResult::Armed;
    }

    /// <summary>
    /// Applies already-authenticated Mesh-level acceptance evidence and commits exactly one successful hop transition.
    /// </summary>
    ForwardingAcceptanceResult AcceptAuthenticated(
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acceptedMessageId,
        std::uint64_t nowMilliseconds,
        RemainingHopLimit& remainingHopLimit
    ) noexcept {
        if (!authenticatedSource || !authenticatedSourceIncarnation || acceptedMessageId == 0U) {
            return ForwardingAcceptanceResult::Invalid;
        }
        if (!_pending) return ForwardingAcceptanceResult::NotPending;
        if (authenticatedSource != _pending.NextHop) return ForwardingAcceptanceResult::WrongNextHop;
        if (authenticatedSourceIncarnation != _pending.NextHopIncarnation) {
            return ForwardingAcceptanceResult::WrongIncarnation;
        }
        if (acceptedMessageId != _pending.MessageId) return ForwardingAcceptanceResult::WrongMessage;
        if (nowMilliseconds >= _pending.AbsoluteDeadlineMilliseconds) {
            _pending = {};
            return ForwardingAcceptanceResult::DeadlineExpired;
        }

        if (CommitSuccessfulForwardingTransition(remainingHopLimit) != ForwardingTransitionResult::Committed) {
            _pending = {};
            return ForwardingAcceptanceResult::HopLimitExhausted;
        }
        _pending = {};
        return ForwardingAcceptanceResult::Committed;
    }

    /// <summary>Releases pending transition state after cancellation or definitive local forwarding failure.</summary>
    bool Cancel() noexcept {
        if (!_pending) return false;
        _pending = {};
        return true;
    }
};

} // namespace ESPressio::Mesh
