#include <cassert>

#include <ESPressio_ForwardingAttemptLifecycle.hpp>

using namespace ESPressio::Mesh;

namespace {
ESPressio::Radio::LogicalTransferTerminalEvidence Terminal(
    ESPressio::Radio::RadioDirectLinkEvidence evidence
) {
    ESPressio::Radio::LogicalTransferTerminalEvidence terminal;
    terminal.Transfer = {0, 1};
    terminal.Descriptor.Radio = reinterpret_cast<ESPressio::Radio::IRadio*>(1);
    terminal.Descriptor.TransferId = 7;
    const std::uint8_t address = 0x22;
    terminal.Descriptor.Destination = ESPressio::Radio::RadioAddress::FromBytes(&address, 1);
    terminal.Evidence = evidence;
    return terminal;
}
}

int main() {
    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    assert(attempts.BeginDistinctRouteAttempt(100, 500));

    // Radio acceptance, even with strongest synchronous link evidence, remains pending Mesh acceptance.
    ForwardingSubmissionResult accepted;
    accepted.Disposition = ForwardingSubmissionDisposition::Accepted;
    accepted.RadioResult = {
        ESPressio::Radio::RadioTransportSendStatus::Accepted,
        ESPressio::Radio::RadioSendResult::Accepted(
            ESPressio::Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged()
        )
    };
    assert(accepted.DirectLinkEvidence() == ForwardingDirectLinkEvidence::PeerAcknowledged);
    assert(ForwardingAttemptLifecycle::AfterSubmission(accepted, attempts, 110, 500) ==
           ForwardingAttemptAction::AwaitingNextHopAcceptance);

    // Deferred Radio completion/peer ACK is still only direct-link evidence, never Mesh next-hop acceptance.
    const auto completed = Terminal(ESPressio::Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged());
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(completed, attempts, 115, 500) ==
           ForwardingAttemptAction::AwaitingNextHopAcceptance);

    // A terminal Radio failure can drive the bounded same-route retry policy without consuming HopLimit.
    const auto failed = Terminal(ESPressio::Radio::RadioDirectLinkEvidence::Failed());
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(failed, attempts, 116, 500) ==
           ForwardingAttemptAction::RetryCurrentRoute);

    // The immutable delivery deadline dominates even otherwise useful terminal Radio evidence.
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(completed, attempts, 500, 500) ==
           ForwardingAttemptAction::StopDeadlineExpired);

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

    ForwardingSubmissionResult retryable;
    retryable.Disposition = ForwardingSubmissionDisposition::RetryableFailure;
    assert(ForwardingAttemptLifecycle::AfterSubmission(retryable, attempts, 130, 500) ==
           ForwardingAttemptAction::RetryCurrentRoute);

    ForwardingSubmissionResult resource;
    resource.Disposition = ForwardingSubmissionDisposition::ResourceUnavailable;
    assert(ForwardingAttemptLifecycle::AfterSubmission(resource, attempts, 130, 500) ==
           ForwardingAttemptAction::RetryCurrentRoute);

    ForwardingSubmissionResult unavailable;
    unavailable.Disposition = ForwardingSubmissionDisposition::PeerUnavailable;
    assert(ForwardingAttemptLifecycle::AfterSubmission(unavailable, attempts, 130, 500) ==
           ForwardingAttemptAction::ReplanDistinctRoute);

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
