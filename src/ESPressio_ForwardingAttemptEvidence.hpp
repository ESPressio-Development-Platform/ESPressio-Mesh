#pragma once

#include <cstdint>

#include <ESPressio_DeferredLogicalTransferTracker.hpp>

#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_RouteAttemptPolicy.hpp"

namespace ESPressio::Mesh {

/// <summary>Actionable state established for one Mesh next-hop attempt from Radio-layer evidence.</summary>
enum class ForwardingAttemptEvidenceDisposition : std::uint8_t {
    AwaitingNextHopAcceptance,
    RetryableRouteFailure,
    ResourceUnavailable,
    DeadlineExpired,
    PermanentFailure,
    Invalid
};

/// <summary>Classifies immediate Radio submission without promoting Radio admission/completion into Mesh acceptance.</summary>
inline ForwardingAttemptEvidenceDisposition ClassifyForwardingSubmission(
    const ForwardingSubmissionResult& submission
) noexcept {
    switch (submission.Disposition) {
        case ForwardingSubmissionDisposition::Accepted:
            if (submission.RadioResult.LinkResult.Evidence.TransmissionFailed()) {
                return ForwardingAttemptEvidenceDisposition::RetryableRouteFailure;
            }
            return ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance;
        case ForwardingSubmissionDisposition::DeadlineExpired:
            return ForwardingAttemptEvidenceDisposition::DeadlineExpired;
        case ForwardingSubmissionDisposition::ResourceUnavailable:
            return ForwardingAttemptEvidenceDisposition::ResourceUnavailable;
        case ForwardingSubmissionDisposition::MembershipUnavailable:
        case ForwardingSubmissionDisposition::PeerUnavailable:
        case ForwardingSubmissionDisposition::RetryableFailure:
            return ForwardingAttemptEvidenceDisposition::RetryableRouteFailure;
        case ForwardingSubmissionDisposition::HopLimitExhausted:
        case ForwardingSubmissionDisposition::PermanentFailure:
            return ForwardingAttemptEvidenceDisposition::PermanentFailure;
        case ForwardingSubmissionDisposition::Invalid:
            return ForwardingAttemptEvidenceDisposition::Invalid;
    }
    return ForwardingAttemptEvidenceDisposition::Invalid;
}

/// <summary>Classifies terminal Radio logical-transfer evidence for the Mesh forwarding attempt.</summary>
/// <remarks>
/// Completed transmission, including peer/link acknowledgement, proves only a direct-link fact and leaves the attempt
/// waiting for authenticated next-hop acceptance. A terminal Radio failure may drive retry/replanning policy but never
/// consumes RemainingHopLimit. The immutable Mesh delivery deadline remains the outer bound.
/// </remarks>
inline ForwardingAttemptEvidenceDisposition ClassifyLogicalTransferTerminalEvidence(
    const Radio::LogicalTransferTerminalEvidence& terminal,
    std::uint64_t nowMilliseconds,
    std::uint64_t absoluteDeadlineMilliseconds
) noexcept {
    if (!terminal.Transfer || !terminal.Descriptor.IsValid() || !terminal.Evidence.IsTerminal()) {
        return ForwardingAttemptEvidenceDisposition::Invalid;
    }
    if (absoluteDeadlineMilliseconds == 0U || nowMilliseconds >= absoluteDeadlineMilliseconds) {
        return ForwardingAttemptEvidenceDisposition::DeadlineExpired;
    }
    if (terminal.Evidence.TransmissionFailed()) {
        return ForwardingAttemptEvidenceDisposition::RetryableRouteFailure;
    }
    return ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance;
}

/// <summary>
/// Attempts to map a terminal forwarding failure into the bounded route-attempt policy vocabulary.
/// </summary>
/// <returns>False while authenticated next-hop acceptance is still pending; no route outcome is written then.</returns>
inline bool TryMapRouteAttemptOutcome(
    ForwardingAttemptEvidenceDisposition disposition,
    RouteAttemptOutcome& outcome
) noexcept {
    switch (disposition) {
        case ForwardingAttemptEvidenceDisposition::RetryableRouteFailure:
            outcome = RouteAttemptOutcome::RetryableFailure;
            return true;
        case ForwardingAttemptEvidenceDisposition::ResourceUnavailable:
            outcome = RouteAttemptOutcome::ResourceUnavailable;
            return true;
        case ForwardingAttemptEvidenceDisposition::DeadlineExpired:
            outcome = RouteAttemptOutcome::DeadlineExpired;
            return true;
        case ForwardingAttemptEvidenceDisposition::PermanentFailure:
        case ForwardingAttemptEvidenceDisposition::Invalid:
            outcome = RouteAttemptOutcome::PermanentFailure;
            return true;
        case ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance:
            return false;
    }
    return false;
}

} // namespace ESPressio::Mesh
