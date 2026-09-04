#include <cassert>

#include <ESPressio_ForwardingAttemptResolution.hpp>

using namespace ESPressio::Mesh;

int main() {
    ForwardingSubmissionResult accepted{};
    accepted.Disposition = ForwardingSubmissionDisposition::Accepted;
    accepted.RadioResult.Status = ESPressio::Radio::RadioTransportSendStatus::Accepted;
    accepted.RadioResult.LinkResult = ESPressio::Radio::RadioSendResult::Accepted(
        ESPressio::Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged()
    );

    const auto acceptedResolution = ResolveForwardingSubmission(accepted);
    assert(!acceptedResolution.IsResolved());
    assert(accepted.DirectLinkEvidence() == ForwardingDirectLinkEvidence::PeerAcknowledged);

    ForwardingSubmissionResult retryable{};
    retryable.Disposition = ForwardingSubmissionDisposition::RetryableFailure;
    const auto retryableResolution = ResolveForwardingSubmission(retryable);
    assert(retryableResolution.IsResolved());
    assert(retryableResolution.Outcome == RouteAttemptOutcome::RetryableFailure);

    ForwardingSubmissionResult unavailable{};
    unavailable.Disposition = ForwardingSubmissionDisposition::PeerUnavailable;
    const auto unavailableResolution = ResolveForwardingSubmission(unavailable);
    assert(unavailableResolution.IsResolved());
    assert(unavailableResolution.Outcome == RouteAttemptOutcome::RouteUnavailable);

    ForwardingSubmissionResult deadline{};
    deadline.Disposition = ForwardingSubmissionDisposition::DeadlineExpired;
    const auto deadlineResolution = ResolveForwardingSubmission(deadline);
    assert(deadlineResolution.IsResolved());
    assert(deadlineResolution.Outcome == RouteAttemptOutcome::DeadlineExpired);

    assert(ResolveMeshDeliveryConfirmation(true) == RouteAttemptOutcome::Delivered);
    assert(ResolveMeshDeliveryConfirmation(false) == RouteAttemptOutcome::RetryableFailure);
    return 0;
}
