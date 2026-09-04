#include <cassert>

#include <ESPressio_ForwardingEvidencePolicy.hpp>
#include <ESPressio_ForwardingTransition.hpp>

using namespace ESPressio::Mesh;

int main() {
    DefaultForwardingEvidencePolicy policy;

    ForwardingSubmissionResult accepted{};
    accepted.Disposition = ForwardingSubmissionDisposition::Accepted;
    accepted.RadioResult.Status = ESPressio::Radio::RadioTransportSendStatus::Accepted;
    accepted.RadioResult.LinkResult = ESPressio::Radio::RadioSendResult::Accepted(
        ESPressio::Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged()
    );

    // Even the strongest synchronous direct-link evidence is not a Mesh delivery acknowledgement.
    ForwardingTransitionEvidence evidence{accepted, false};
    assert(accepted.DirectLinkEvidence() == ForwardingDirectLinkEvidence::PeerAcknowledged);
    assert(policy.Evaluate(evidence) == ForwardingTransitionDisposition::AwaitingEvidence);

    RemainingHopLimit hops = 3;
    assert(hops == 3);

    // Only separately established Mesh-level delivery evidence completes the forwarding transition.
    evidence.MeshDeliveryAcknowledged = true;
    assert(policy.Evaluate(evidence) == ForwardingTransitionDisposition::Successful);
    assert(CommitSuccessfulForwardingTransition(hops));
    assert(hops == 2);

    // Admission without any completion proof must likewise remain pending.
    accepted.RadioResult.LinkResult = ESPressio::Radio::RadioSendResult::Accepted();
    evidence = {accepted, false};
    assert(accepted.DirectLinkEvidence() == ForwardingDirectLinkEvidence::SubmissionAccepted);
    assert(policy.Evaluate(evidence) == ForwardingTransitionDisposition::AwaitingEvidence);

    ForwardingSubmissionResult resource{};
    resource.Disposition = ForwardingSubmissionDisposition::ResourceUnavailable;
    assert(policy.Evaluate({resource, false}) == ForwardingTransitionDisposition::ResourceUnavailable);

    ForwardingSubmissionResult peerLost{};
    peerLost.Disposition = ForwardingSubmissionDisposition::PeerUnavailable;
    assert(policy.Evaluate({peerLost, false}) == ForwardingTransitionDisposition::RetryableFailure);

    ForwardingSubmissionResult expired{};
    expired.Disposition = ForwardingSubmissionDisposition::DeadlineExpired;
    assert(policy.Evaluate({expired, false}) == ForwardingTransitionDisposition::DeadlineExpired);

    ForwardingSubmissionResult exhausted{};
    exhausted.Disposition = ForwardingSubmissionDisposition::HopLimitExhausted;
    assert(policy.Evaluate({exhausted, false}) == ForwardingTransitionDisposition::PermanentFailure);

    return 0;
}
