#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ForwardingRadioAttemptCoordinator.hpp"
#include "ESPressio_OutboundDeliveryLifecycle.hpp"

namespace ESPressio::Mesh {

/// <summary>Outcome of one outbound Radio-backed forwarding submission.</summary>
struct OutboundRadioForwardingResult final {
    ForwardingSubmissionResult Submission{};
    OutboundForwardingAction Action{OutboundForwardingAction::Invalid};
    ForwardingRadioCorrelationDisposition CorrelationDisposition{ForwardingRadioCorrelationDisposition::NotRequired};
};

/// <summary>
/// Owns the retained Radio-terminal correlation handle for one active outbound delivery while delegating route-attempt,
/// exact-next-hop acceptance and destination-acknowledgement semantics to their existing narrow coordinators.
/// </summary>
/// <remarks>
/// This is composition, not a new transport. Correlation is reserved before Radio submission by
/// ForwardingRadioAttemptCoordinator and is relinquished deterministically when terminal Radio evidence is consumed,
/// authenticated Mesh next-hop acceptance supersedes it, a retry/replan/stop action abandons the attempt, or the delivery
/// is reset/cancelled. Radio terminal completion or peer ACK never commits HopLimit; only authenticated next-hop acceptance
/// delegated to OutboundDeliveryLifecycle can do that.
/// </remarks>
template<
    std::size_t AcknowledgementCapacity,
    std::size_t CorrelationCapacity,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class OutboundRadioDeliveryCoordinator final {
    OutboundDeliveryLifecycle<AcknowledgementCapacity>& _delivery;
    ForwardingRadioAttemptCoordinator<CorrelationCapacity, MembershipCapacity, BindingCapacity, HopCapacity>& _radioAttempts;
    ForwardingRadioCorrelationHandle _correlation{};

    static OutboundForwardingAction Map(ForwardingAttemptAction action) noexcept {
        switch (action) {
            case ForwardingAttemptAction::AwaitingNextHopAcceptance: return OutboundForwardingAction::AwaitingNextHopAcceptance;
            case ForwardingAttemptAction::RetryCurrentRoute: return OutboundForwardingAction::RetryCurrentRoute;
            case ForwardingAttemptAction::ReplanDistinctRoute: return OutboundForwardingAction::ReplanDistinctRoute;
            case ForwardingAttemptAction::StopDeadlineExpired: return OutboundForwardingAction::StopDeadlineExpired;
            case ForwardingAttemptAction::StopPermanentFailure: return OutboundForwardingAction::StopPermanentFailure;
            case ForwardingAttemptAction::StopAttemptLimit: return OutboundForwardingAction::StopAttemptLimit;
        }
        return OutboundForwardingAction::Invalid;
    }

    void ReleaseCorrelation() noexcept {
        if (_correlation) (void)_radioAttempts.Release(_correlation);
        _correlation = {};
    }

public:
    OutboundRadioDeliveryCoordinator(
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery,
        ForwardingRadioAttemptCoordinator<CorrelationCapacity, MembershipCapacity, BindingCapacity, HopCapacity>& radioAttempts
    ) noexcept : _delivery(delivery), _radioAttempts(radioAttempts) {}

    /// <summary>Returns whether the composed outbound delivery lifecycle is currently active.</summary>
    bool IsActive() const noexcept { return _delivery.IsActive(); }

    /// <summary>Returns the active Mesh MessageId, or zero when no outbound delivery is active.</summary>
    MeshMessageId MessageId() const noexcept { return _delivery.MessageId(); }

    constexpr bool HasPendingRadioTerminalCorrelation() const noexcept { return static_cast<bool>(_correlation); }
    constexpr ForwardingRadioCorrelationHandle PendingRadioTerminalCorrelation() const noexcept { return _correlation; }

    OutboundRadioForwardingResult Submit(
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint64_t nowMilliseconds
    ) {
        OutboundRadioForwardingResult result;
        if (!_delivery.IsActive() || _delivery.AwaitingNextHopAcceptance() || _correlation) return result;

        const auto coordinated = _radioAttempts.Submit(
            localDevice,
            route,
            remainingHopLimit,
            payload,
            payloadSize,
            nowMilliseconds,
            _delivery.AbsoluteDeadlineMilliseconds()
        );
        result.Submission = coordinated.Submission;
        result.CorrelationDisposition = coordinated.CorrelationDisposition;
        result.Action = Map(coordinated.Action);

        if (coordinated.Correlation) _correlation = coordinated.Correlation;
        if (coordinated.Action != ForwardingAttemptAction::AwaitingNextHopAcceptance) {
            ReleaseCorrelation();
            return result;
        }
        if (!coordinated.Submission.NextHop || !coordinated.Submission.NextHopIncarnation) {
            ReleaseCorrelation();
            result.Action = OutboundForwardingAction::Invalid;
            return result;
        }

        result.Action = _delivery.ArmAcceptedSubmission(
            coordinated.Submission.NextHop,
            coordinated.Submission.NextHopIncarnation,
            nowMilliseconds
        );
        if (result.Action != OutboundForwardingAction::AwaitingNextHopAcceptance) ReleaseCorrelation();
        return result;
    }

    /// <summary>
    /// Consumes deferred Radio terminal evidence when available. Completion/ACK keeps exact Mesh acceptance pending;
    /// failure/retry/replan/stop cancels that pending acceptance and releases correlation through consumption.
    /// </summary>
    bool TryConsumeRadioTerminal(
        std::uint64_t nowMilliseconds,
        OutboundForwardingAction& action,
        Radio::LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept {
        if (!_correlation) return false;
        ForwardingAttemptAction forwardingAction{};
        const auto handle = _correlation;
        if (!_radioAttempts.TryConsumeTerminal(
                handle,
                nowMilliseconds,
                _delivery.AbsoluteDeadlineMilliseconds(),
                forwardingAction,
                terminal)) return false;
        _correlation = {};
        action = Map(forwardingAction);
        if (forwardingAction != ForwardingAttemptAction::AwaitingNextHopAcceptance) {
            _delivery.CancelPendingNextHopAcceptance();
        }
        return true;
    }

    /// <summary>
    /// Applies authenticated exact-next-hop acceptance. Any outstanding Radio-terminal correlation is abandoned because
    /// stronger Mesh acceptance has already established the forwarding transition.
    /// </summary>
    ForwardingAcceptanceAction AcceptNextHopAuthenticated(
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acceptedMessageId,
        std::uint64_t nowMilliseconds,
        RemainingHopLimit& remainingHopLimit
    ) noexcept {
        ReleaseCorrelation();
        return _delivery.AcceptNextHopAuthenticated(
            authenticatedSource,
            authenticatedSourceIncarnation,
            acceptedMessageId,
            nowMilliseconds,
            remainingHopLimit
        );
    }

    /// <summary>Releases attempt correlation and all per-delivery state for completion, cancellation or definitive stop.</summary>
    void Reset() noexcept {
        ReleaseCorrelation();
        _delivery.Reset();
    }
};

} // namespace ESPressio::Mesh
