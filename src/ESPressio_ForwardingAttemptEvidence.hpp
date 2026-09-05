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

/// <summary>
/// Classifies the immediate Radio submission result without promoting Radio admission/completion into Mesh acceptance.
/// </summary>
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

/// <summary>
/// Classifies terminal Radio logical-transfer evidence for route-attempt policy.
/// </summary>
/// <remarks>
/// Completed transmission, including peer/link acknowledgement, only proves a direct-link fact. It leaves the Mesh
/// attempt waiting for authenticated next-hop acceptance. A terminal Radio failure may drive retry/replanning policy,
/// but never consumes RemainingHopLimit. The immutable Mesh delivery deadline remains the outer bound.
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

/// <summary>Maps only terminal failure classes into the existing bounded route-attempt policy vocabulary.</summary>
inline RouteAttemptOutcome ToRouteAttemptOutcome(ForwardingAttemptEvidenceDisposition disposition) noexcept {
    switch (disposition) {
        case ForwardingAttemptEvidenceDisposition::RetryableRouteFailure:
            return RouteAttemptOutcome::RetryableFailure;
        case ForwardingAttemptEvidenceDisposition::ResourceUnavailable:
            return RouteAttemptOutcome::ResourceUnavailable;
        case ForwardingAttemptEvidenceDisposition::DeadlineExpired:
            return RouteAttemptOutcome::DeadlineExpired;
        case ForwardingAttemptEvidenceDisposition::PermanentFailure:
        case ForwardingAttemptEvidenceDisposition::Invalid:
            return RouteAttemptOutcome::PermanentFailure;
        case ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance:
            // This state is deliberately not a completed route-attempt outcome. Callers must not feed this mapping into
            // RouteAttemptCoordinator until authenticated next-hop acceptance or another terminal attempt fact exists.
            return RouteAttemptOutcome::RouteUnavailable;
    }
    return RouteAttemptOutcome::PermanentFailure;
}

} // namespace ESPressio::Mesh
