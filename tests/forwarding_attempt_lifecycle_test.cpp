#include <cassert>

#include <ESPressio_ForwardingAttemptLifecycle.hpp>

using namespace ESPressio::Mesh;

int main() {
    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    assert(attempts.BeginDistinctRouteAttempt(100, 500));

    // Radio acceptance, even with strongest synchronous link evidence, remains pending Mesh acceptance.
    ForwardingSubmissionResult accepted;
    accepted.Disposition = ForwardingSubmissionDisposition::Accepted;
    accepted.RadioResult = {
        Radio::RadioTransportSendStatus::Accepted,
        Radio::RadioSendResult::Accepted(Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged())
    };
    assert(accepted.DirectLinkEvidence() == ForwardingDirectLinkEvidence::PeerAcknowledged);
    assert(ForwardingAttemptLifecycle::AfterSubmission(accepted, attempts, 110, 500) ==
           ForwardingAttemptAction::AwaitingNextHopAcceptance);

    // Exact authenticated Mesh-level next-hop acceptance is what completes this forwarding attempt.
    assert(ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(
               ForwardingAcceptanceResult::Committed,
               attempts,
               120,
               500) == ForwardingAcceptanceAction::ForwardingComplete);

    // Foreign/stale acknowledgement-like evidence cannot spend retry budget or fail the active attempt.
    assert(ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(
               ForwardingAcceptanceResult::WrongNextHop,
               attempts,
               121,
               500) == ForwardingAcceptanceAction::IgnoreUnrelatedEvidence);
    assert(ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(
               ForwardingAcceptanceResult::WrongIncarnation,
               attempts,
               121,
               500) == ForwardingAcceptanceAction::IgnoreUnrelatedEvidence);
    assert(ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(
               ForwardingAcceptanceResult::WrongMessage,
               attempts,
               121,
               500) == ForwardingAcceptanceAction::IgnoreUnrelatedEvidence);

    // Retryable submission failure follows the frozen same-route attempt policy.
    ForwardingSubmissionResult retryable;
    retryable.Disposition = ForwardingSubmissionDisposition::RetryableFailure;
    assert(ForwardingAttemptLifecycle::AfterSubmission(retryable, attempts, 130, 500) ==
           ForwardingAttemptAction::RetryCurrentRoute);

    // Resource pressure is also retryable while current-route budget remains.
    ForwardingSubmissionResult resource;
    resource.Disposition = ForwardingSubmissionDisposition::ResourceUnavailable;
    assert(ForwardingAttemptLifecycle::AfterSubmission(resource, attempts, 130, 500) ==
           ForwardingAttemptAction::RetryCurrentRoute);

    // Missing membership/peer is a route-unavailable condition, so default policy seeks another route.
    ForwardingSubmissionResult unavailable;
    unavailable.Disposition = ForwardingSubmissionDisposition::PeerUnavailable;
    assert(ForwardingAttemptLifecycle::AfterSubmission(unavailable, attempts, 130, 500) ==
           ForwardingAttemptAction::ReplanDistinctRoute);

    // Deadline and permanent failures stop deterministically.
    ForwardingSubmissionResult expired;
    expired.Disposition = ForwardingSubmissionDisposition::DeadlineExpired;
    assert(ForwardingAttemptLifecycle::AfterSubmission(expired, attempts, 500, 500) ==
           ForwardingAttemptAction::StopDeadlineExpired);

    ForwardingSubmissionResult permanent;
    permanent.Disposition = ForwardingSubmissionDisposition::PermanentFailure;
    assert(ForwardingAttemptLifecycle::AfterSubmission(permanent, attempts, 140, 500) ==
           ForwardingAttemptAction::StopPermanentFailure);

    assert(ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(
               ForwardingAcceptanceResult::DeadlineExpired,
               attempts,
               500,
               500) == ForwardingAcceptanceAction::StopDeadlineExpired);
    assert(ForwardingAttemptLifecycle::AfterAuthenticatedAcceptance(
               ForwardingAcceptanceResult::HopLimitExhausted,
               attempts,
               150,
               500) == ForwardingAcceptanceAction::StopPermanentFailure);

    return 0;
}
