#pragma once

#include <cstdint>

#include "ESPressio_ForwardingAttemptEvidence.hpp"
#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_ForwardingTransitionCoordinator.hpp"
#include "ESPressio_RouteAttemptCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Immediate lifecycle action after one local forwarding submission attempt.</summary>
enum class ForwardingAttemptAction : std::uint8_t {
    AwaitingNextHopAcceptance,
    RetryCurrentRoute,
    ReplanDistinctRoute,
    StopDeadlineExpired,
    StopPermanentFailure,
    StopAttemptLimit
};

/// <summary>Lifecycle action after already-authenticated next-hop acceptance evidence is evaluated.</summary>
enum class ForwardingAcceptanceAction : std::uint8_t {
    ForwardingComplete,
    IgnoreUnrelatedEvidence,
    StopDeadlineExpired,
    StopPermanentFailure
};

/// <summary>
/// Wire-neutral composition helpers joining Radio submission, Radio terminal evidence, authenticated next-hop Mesh
/// acceptance and bounded route-attempt policy without collapsing their meanings.
/// </summary>
/// <remarks>
/// An accepted Radio submission remains AwaitingNextHopAcceptance, regardless of whether Radio proved transmission
/// completion or link-layer peer acknowledgement. A later completed Radio logical transfer also remains pending for
/// authenticated Mesh acceptance. A terminal Radio failure may drive bounded retry/replanning. Only
/// `ForwardingAcceptanceResult::Committed`, produced after exact authenticated next-hop DeviceIdentifier +
/// MembershipIncarnation + MeshMessageId acceptance, becomes `RouteAttemptOutcome::Delivered` and consumes one hop.
///
/// Wrong-node/incarnation/message acceptance evidence is ignored because it says nothing about the currently pending
/// route attempt. It must not consume retry budget or prematurely fail a valid in-flight forwarding transition.
///
/// This helper defines no queue, task, timer, wire message, security scheme or PrimitiveFamilyId.
/// </remarks>
class ForwardingAttemptLifecycle final {
    static ForwardingAttemptAction MapAction(RouteAttemptAction action) noexcept {
        switch (action) {
            case RouteAttemptAction::RetryCurrentRoute: return ForwardingAttemptAction::RetryCurrentRoute;
            case RouteAttemptAction::ReplanDistinctRoute: return ForwardingAttemptAction::ReplanDistinctRoute;
            case RouteAttemptAction::StopDeadlineExpired: return ForwardingAttemptAction::StopDeadlineExpired;
            case RouteAttemptAction::StopPermanentFailure: return ForwardingAttemptAction::StopPermanentFailure;
            case RouteAttemptAction::StopAttemptLimit: return ForwardingAttemptAction::StopAttemptLimit;
            case RouteAttemptAction::Complete: return ForwardingAttemptAction::StopPermanentFailure;
        }
        return ForwardingAttemptAction::StopPermanentFailure;
    }

    static RouteAttemptOutcome SubmissionFailureOutcome(ForwardingSubmissionDisposition disposition) noexcept {
        switch (disposition) {
            case ForwardingSubmissionDisposition::DeadlineExpired:
                return RouteAttemptOutcome::DeadlineExpired;
            case ForwardingSubmissionDisposition::MembershipUnavailable:
            case ForwardingSubmissionDisposition::PeerUnavailable:
                return RouteAttemptOutcome::RouteUnavailable;
            case ForwardingSubmissionDisposition::ResourceUnavailable:
                return RouteAttemptOutcome::ResourceUnavailable;
            case ForwardingSubmissionDisposition::RetryableFailure:
                return RouteAttemptOutcome::RetryableFailure;
            case ForwardingSubmissionDisposition::HopLimitExhausted:
            case ForwardingSubmissionDisposition::PermanentFailure:
            case ForwardingSubmissionDisposition::Invalid:
            case ForwardingSubmissionDisposition::Accepted:
                return RouteAttemptOutcome::PermanentFailure;
        }
        return RouteAttemptOutcome::PermanentFailure;
    }

public:
    /// <summary>Evaluates one synchronous forwarding submission result.</summary>
    static ForwardingAttemptAction AfterSubmission(
        const ForwardingSubmissionResult& submission,
        const RouteAttemptCoordinator& attempts,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) noexcept {
        if (submission.Disposition == ForwardingSubmissionDisposition::Accepted) {
            return ForwardingAttemptAction::AwaitingNextHopAcceptance;
        }
        return MapAction(attempts.Decide(
            SubmissionFailureOutcome(submission.Disposition),
            nowMilliseconds,
            absoluteDeadlineMilliseconds
        ));
    }

    /// <summary>
    /// Evaluates one terminal Radio logical-transfer observation. Successful direct-link completion remains pending for
    /// authenticated next-hop Mesh acceptance; terminal Radio failure is allowed to enter bounded retry/replanning.
    /// </summary>
    static ForwardingAttemptAction AfterRadioTerminalEvidence(
        const Radio::LogicalTransferTerminalEvidence& terminal,
        const RouteAttemptCoordinator& attempts,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) noexcept {
        const auto disposition = ClassifyLogicalTransferTerminalEvidence(
            terminal,
            nowMilliseconds,
            absoluteDeadlineMilliseconds
        );
        if (disposition == ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance) {
            return ForwardingAttemptAction::AwaitingNextHopAcceptance;
        }
        RouteAttemptOutcome outcome{};
        if (!TryMapRouteAttemptOutcome(disposition, outcome)) {
            return ForwardingAttemptAction::AwaitingNextHopAcceptance;
        }
        return MapAction(attempts.Decide(outcome, nowMilliseconds, absoluteDeadlineMilliseconds));
    }

    /// <summary>Evaluates the result of already-authenticated next-hop acceptance processing.</summary>
    static ForwardingAcceptanceAction AfterAuthenticatedAcceptance(
        ForwardingAcceptanceResult acceptance,
        const RouteAttemptCoordinator& attempts,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) noexcept {
        switch (acceptance) {
            case ForwardingAcceptanceResult::Committed:
                return attempts.Decide(
                    RouteAttemptOutcome::Delivered,
                    nowMilliseconds,
                    absoluteDeadlineMilliseconds
                ) == RouteAttemptAction::Complete
                    ? ForwardingAcceptanceAction::ForwardingComplete
                    : ForwardingAcceptanceAction::StopPermanentFailure;
            case ForwardingAcceptanceResult::WrongNextHop:
            case ForwardingAcceptanceResult::WrongIncarnation:
            case ForwardingAcceptanceResult::WrongMessage:
            case ForwardingAcceptanceResult::NotPending:
                return ForwardingAcceptanceAction::IgnoreUnrelatedEvidence;
            case ForwardingAcceptanceResult::DeadlineExpired:
                return ForwardingAcceptanceAction::StopDeadlineExpired;
            case ForwardingAcceptanceResult::HopLimitExhausted:
            case ForwardingAcceptanceResult::Invalid:
                return ForwardingAcceptanceAction::StopPermanentFailure;
        }
        return ForwardingAcceptanceAction::StopPermanentFailure;
    }
};

} // namespace ESPressio::Mesh
