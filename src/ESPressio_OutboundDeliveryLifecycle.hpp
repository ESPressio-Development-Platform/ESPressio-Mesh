#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_DeliveryAcknowledgementCoordinator.hpp"
#include "ESPressio_ForwardingAttemptLifecycle.hpp"
#include "ESPressio_ForwardingTransitionCoordinator.hpp"
#include "ESPressio_RouteAttemptCoordinator.hpp"

namespace ESPressio::Mesh {

enum class OutboundDeliveryBeginResult : std::uint8_t { Begun, AlreadyActive, ResourceUnavailable, DeadlineExpired, Invalid };
enum class OutboundForwardingAction : std::uint8_t { AwaitingNextHopAcceptance, RetryCurrentRoute, ReplanDistinctRoute, StopDeadlineExpired, StopPermanentFailure, StopAttemptLimit, Invalid };
enum class OutboundDeliveryAcknowledgementAction : std::uint8_t { DeliveryConfirmed, IgnoreUnrelatedAcknowledgement, StopDeadlineExpired, Invalid };

/// <summary>
/// Per-delivery wire-neutral composition of bounded route attempts, exact next-hop acceptance and optional end-to-end
/// destination acknowledgement tracking.
/// </summary>
/// <remarks>
/// This type owns no payload, route, queue, task, timer, Radio transport or wire encoding. Radio submission, Radio
/// completion/peer acknowledgement, authenticated next-hop acceptance and final destination acknowledgement remain
/// separate stages. The `ArmAcceptedSubmission` entry point exists for a higher composition which has already evaluated
/// the submission through `ForwardingRadioAttemptCoordinator`; it arms acceptance without evaluating the route attempt a
/// second time.
/// </remarks>
template<std::size_t AcknowledgementCapacity>
class OutboundDeliveryLifecycle final {
    RouteAttemptCoordinator& _attempts;
    DeliveryAcknowledgementCoordinator<AcknowledgementCapacity>& _acknowledgements;
    ForwardingTransitionCoordinator _forwarding{};
    System::DeviceIdentifier _destination{};
    MembershipIncarnation _destinationIncarnation{};
    MeshMessageId _messageId{0};
    std::uint64_t _absoluteDeadlineMilliseconds{0};
    bool _acknowledgementReserved{false};
    bool _active{false};

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

public:
    OutboundDeliveryLifecycle(RouteAttemptCoordinator& attempts, DeliveryAcknowledgementCoordinator<AcknowledgementCapacity>& acknowledgements) noexcept
        : _attempts(attempts), _acknowledgements(acknowledgements) {}

    constexpr bool IsActive() const noexcept { return _active; }
    constexpr bool AwaitingNextHopAcceptance() const noexcept { return _forwarding.HasPending(); }
    constexpr bool AwaitingDestinationAcknowledgement() const noexcept { return _acknowledgementReserved; }
    constexpr MeshMessageId MessageId() const noexcept { return _messageId; }
    constexpr std::uint64_t AbsoluteDeadlineMilliseconds() const noexcept { return _absoluteDeadlineMilliseconds; }

    OutboundDeliveryBeginResult Begin(
        const System::DeviceIdentifier& destination,
        const MembershipIncarnation& destinationIncarnation,
        MeshMessageId messageId,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds,
        bool requireDestinationAcknowledgement
    ) noexcept {
        if (_active) return OutboundDeliveryBeginResult::AlreadyActive;
        if (!destination || !destinationIncarnation || messageId == 0U || absoluteDeadlineMilliseconds == 0U) return OutboundDeliveryBeginResult::Invalid;
        if (nowMilliseconds >= absoluteDeadlineMilliseconds) return OutboundDeliveryBeginResult::DeadlineExpired;
        if (requireDestinationAcknowledgement) {
            const auto reserve = _acknowledgements.ReservePending(destination, destinationIncarnation, messageId, nowMilliseconds, absoluteDeadlineMilliseconds);
            switch (reserve) {
                case DeliveryAcknowledgementReserveResult::Reserved: _acknowledgementReserved = true; break;
                case DeliveryAcknowledgementReserveResult::ResourceUnavailable: return OutboundDeliveryBeginResult::ResourceUnavailable;
                case DeliveryAcknowledgementReserveResult::DeadlineExpired: return OutboundDeliveryBeginResult::DeadlineExpired;
                case DeliveryAcknowledgementReserveResult::AlreadyPending:
                case DeliveryAcknowledgementReserveResult::Invalid: return OutboundDeliveryBeginResult::Invalid;
            }
        }
        _destination = destination;
        _destinationIncarnation = destinationIncarnation;
        _messageId = messageId;
        _absoluteDeadlineMilliseconds = absoluteDeadlineMilliseconds;
        _attempts.Reset();
        _active = true;
        return OutboundDeliveryBeginResult::Begun;
    }

    bool BeginDistinctRouteAttempt(std::uint64_t nowMilliseconds) noexcept {
        return _active && !_forwarding.HasPending() && _attempts.BeginDistinctRouteAttempt(nowMilliseconds, _absoluteDeadlineMilliseconds);
    }
    bool BeginCurrentRouteRetry(std::uint64_t nowMilliseconds) noexcept {
        return _active && !_forwarding.HasPending() && _attempts.BeginCurrentRouteRetry(nowMilliseconds, _absoluteDeadlineMilliseconds);
    }

    /// <summary>Arms exact authenticated next-hop acceptance after a submission already evaluated by the route-attempt lifecycle.</summary>
    OutboundForwardingAction ArmAcceptedSubmission(
        const System::DeviceIdentifier& nextHop,
        const MembershipIncarnation& nextHopIncarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!_active) return OutboundForwardingAction::Invalid;
        const auto arm = _forwarding.Arm(nextHop, nextHopIncarnation, _messageId, nowMilliseconds, _absoluteDeadlineMilliseconds);
        switch (arm) {
            case ForwardingTransitionArmResult::Armed: return OutboundForwardingAction::AwaitingNextHopAcceptance;
            case ForwardingTransitionArmResult::DeadlineExpired: return OutboundForwardingAction::StopDeadlineExpired;
            case ForwardingTransitionArmResult::AlreadyPending:
            case ForwardingTransitionArmResult::Invalid: return OutboundForwardingAction::Invalid;
        }
        return OutboundForwardingAction::Invalid;
    }

    /// <summary>Cancels only the pending exact-next-hop acceptance transition; destination-ACK and delivery state remain intact.</summary>
    void CancelPendingNextHopAcceptance() noexcept { _forwarding.Cancel(); }

    OutboundForwardingAction AfterSubmission(
        const ForwardingSubmissionResult& submission,
        const System::DeviceIdentifier& nextHop,
        const MembershipIncarnation& nextHopIncarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!_active) return OutboundForwardingAction::Invalid;
        const auto action = ForwardingAttemptLifecycle::AfterSubmission(submission, _attempts, nowMilliseconds, _absoluteDeadlineMilliseconds);
        if (action != ForwardingAttemptAction::AwaitingNextHopAcceptance) return Map(action);
        return ArmAcceptedSubmission(nextHop, nextHopIncarnation, nowMilliseconds);
    }

    ForwardingAcceptanceAction AcceptNextHopAuthenticated(
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acceptedMessageId,
        std::uint64_t nowMilliseconds,
        RemainingHopLimit& remainingHopLimit
    ) noexcept {
        if (!_active) return ForwardingAcceptanceAction::StopPermanentFailure;
        const auto acceptance = _forwarding.AcceptAuthenticated(authenticatedSource, authenticatedSourceIncarnation, acceptedMessageId, nowMilliseconds, remainingHopLimit);
        return ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(acceptance, _attempts, nowMilliseconds, _absoluteDeadlineMilliseconds);
    }

    OutboundDeliveryAcknowledgementAction ApplyDestinationAcknowledgementAuthenticated(
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acknowledgedMessageId,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!_active || !_acknowledgementReserved) return OutboundDeliveryAcknowledgementAction::IgnoreUnrelatedAcknowledgement;
        const auto result = _acknowledgements.ApplyAuthenticated(authenticatedSource, authenticatedSourceIncarnation, acknowledgedMessageId, nowMilliseconds);
        switch (result) {
            case DeliveryAcknowledgementApplyResult::Acknowledged: _acknowledgementReserved = false; return OutboundDeliveryAcknowledgementAction::DeliveryConfirmed;
            case DeliveryAcknowledgementApplyResult::DeadlineExpired: _acknowledgementReserved = false; return OutboundDeliveryAcknowledgementAction::StopDeadlineExpired;
            case DeliveryAcknowledgementApplyResult::NotPending: return OutboundDeliveryAcknowledgementAction::IgnoreUnrelatedAcknowledgement;
            case DeliveryAcknowledgementApplyResult::Invalid: return OutboundDeliveryAcknowledgementAction::Invalid;
        }
        return OutboundDeliveryAcknowledgementAction::Invalid;
    }

    void Reset() noexcept {
        _forwarding.Cancel();
        if (_acknowledgementReserved) _acknowledgements.ReleasePending(_destination, _destinationIncarnation, _messageId);
        _attempts.Reset();
        _destination = {};
        _destinationIncarnation = {};
        _messageId = 0U;
        _absoluteDeadlineMilliseconds = 0U;
        _acknowledgementReserved = false;
        _active = false;
    }
};

} // namespace ESPressio::Mesh
