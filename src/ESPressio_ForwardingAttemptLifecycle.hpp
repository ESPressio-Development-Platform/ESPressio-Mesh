#pragma once

#include <cstdint>

#include "ESPressio_ForwardingAttemptEvidence.hpp"
#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_ForwardingTransitionCoordinator.hpp"
#include "ESPressio_RouteAttemptCoordinator.hpp"

namespace ESPressio::Mesh {

enum class ForwardingAttemptAction : std::uint8_t {
    AwaitingNextHopAcceptance,
    RetryCurrentRoute,
    ReplanDistinctRoute,
    StopDeadlineExpired,
    StopPermanentFailure,
    StopAttemptLimit
};

enum class ForwardingAcceptanceAction : std::uint8_t {
    ForwardingComplete,
    IgnoreUnrelatedEvidence,
    StopDeadlineExpired,
    StopPermanentFailure
};

/// <summary>Joins final Radio submission/terminal facts, authenticated Mesh acceptance and bounded route-attempt policy.</summary>
class ForwardingAttemptLifecycle final {
    static ForwardingAttemptAction MapAction(RouteAttemptAction action) noexcept {
        switch(action){
            case RouteAttemptAction::RetryCurrentRoute:return ForwardingAttemptAction::RetryCurrentRoute;
            case RouteAttemptAction::ReplanDistinctRoute:return ForwardingAttemptAction::ReplanDistinctRoute;
            case RouteAttemptAction::StopDeadlineExpired:return ForwardingAttemptAction::StopDeadlineExpired;
            case RouteAttemptAction::StopPermanentFailure:return ForwardingAttemptAction::StopPermanentFailure;
            case RouteAttemptAction::StopAttemptLimit:return ForwardingAttemptAction::StopAttemptLimit;
            case RouteAttemptAction::Complete:return ForwardingAttemptAction::StopPermanentFailure;
        }
        return ForwardingAttemptAction::StopPermanentFailure;
    }
    static RouteAttemptOutcome SubmissionFailureOutcome(ForwardingSubmissionDisposition disposition) noexcept {
        switch(disposition){
            case ForwardingSubmissionDisposition::DeadlineExpired:return RouteAttemptOutcome::DeadlineExpired;
            case ForwardingSubmissionDisposition::MembershipUnavailable:
            case ForwardingSubmissionDisposition::PeerUnavailable:return RouteAttemptOutcome::RouteUnavailable;
            case ForwardingSubmissionDisposition::ResourceUnavailable:return RouteAttemptOutcome::ResourceUnavailable;
            case ForwardingSubmissionDisposition::RetryableFailure:return RouteAttemptOutcome::RetryableFailure;
            case ForwardingSubmissionDisposition::HopLimitExhausted:
            case ForwardingSubmissionDisposition::PermanentFailure:
            case ForwardingSubmissionDisposition::Invalid:
            case ForwardingSubmissionDisposition::Accepted:return RouteAttemptOutcome::PermanentFailure;
        }
        return RouteAttemptOutcome::PermanentFailure;
    }
public:
    static ForwardingAttemptAction AfterSubmission(
        const ForwardingSubmissionResult& submission,const RouteAttemptCoordinator& attempts,
        std::uint64_t nowMilliseconds,std::uint64_t absoluteDeadlineMilliseconds) noexcept {
        if(submission.Disposition==ForwardingSubmissionDisposition::Accepted)
            return ForwardingAttemptAction::AwaitingNextHopAcceptance;
        return MapAction(attempts.Decide(SubmissionFailureOutcome(submission.Disposition),nowMilliseconds,absoluteDeadlineMilliseconds));
    }

    static ForwardingAttemptAction AfterRadioTerminalEvidence(
        const Radio::RadioRuntimeTransferResult& terminal,const RouteAttemptCoordinator& attempts,
        std::uint64_t nowMilliseconds,std::uint64_t absoluteDeadlineMilliseconds) noexcept {
        const auto disposition=ClassifyRadioRuntimeTerminal(terminal,nowMilliseconds,absoluteDeadlineMilliseconds);
        if(disposition==ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance)
            return ForwardingAttemptAction::AwaitingNextHopAcceptance;
        RouteAttemptOutcome outcome{};
        if(!TryMapRouteAttemptOutcome(disposition,outcome)) return ForwardingAttemptAction::AwaitingNextHopAcceptance;
        return MapAction(attempts.Decide(outcome,nowMilliseconds,absoluteDeadlineMilliseconds));
    }

    static ForwardingAcceptanceAction AfterAuthenticatedAcceptance(
        ForwardingAcceptanceResult acceptance,const RouteAttemptCoordinator& attempts,
        std::uint64_t nowMilliseconds,std::uint64_t absoluteDeadlineMilliseconds) noexcept {
        switch(acceptance){
            case ForwardingAcceptanceResult::Committed:
                return attempts.Decide(RouteAttemptOutcome::Delivered,nowMilliseconds,absoluteDeadlineMilliseconds)==RouteAttemptAction::Complete
                    ?ForwardingAcceptanceAction::ForwardingComplete:ForwardingAcceptanceAction::StopPermanentFailure;
            case ForwardingAcceptanceResult::WrongNextHop:
            case ForwardingAcceptanceResult::WrongIncarnation:
            case ForwardingAcceptanceResult::WrongMessage:
            case ForwardingAcceptanceResult::NotPending:return ForwardingAcceptanceAction::IgnoreUnrelatedEvidence;
            case ForwardingAcceptanceResult::DeadlineExpired:return ForwardingAcceptanceAction::StopDeadlineExpired;
            case ForwardingAcceptanceResult::HopLimitExhausted:
            case ForwardingAcceptanceResult::Invalid:return ForwardingAcceptanceAction::StopPermanentFailure;
        }
        return ForwardingAcceptanceAction::StopPermanentFailure;
    }
};

} // namespace ESPressio::Mesh
