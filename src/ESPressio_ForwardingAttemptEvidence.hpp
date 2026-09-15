#pragma once

#include <cstdint>

#include <ESPressio_RadioRuntime.hpp>

#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_RouteAttemptPolicy.hpp"

namespace ESPressio::Mesh {

enum class ForwardingAttemptEvidenceDisposition : std::uint8_t {
    AwaitingNextHopAcceptance,
    RetryableRouteFailure,
    ResourceUnavailable,
    DeadlineExpired,
    PermanentFailure,
    Invalid
};

/// <summary>Classifies immediate managed-Radio scheduler admission without promoting it into delivery evidence.</summary>
inline ForwardingAttemptEvidenceDisposition ClassifyForwardingSubmission(
    const ForwardingSubmissionResult& submission) noexcept {
    switch(submission.Disposition){
        case ForwardingSubmissionDisposition::Accepted:
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

/// <summary>Classifies terminal final-Radio runtime evidence for one Mesh forwarding attempt.</summary>
/// <remarks>Completed physical transmission remains pending for authenticated Mesh next-hop acceptance.</remarks>
inline ForwardingAttemptEvidenceDisposition ClassifyRadioRuntimeTerminal(
    const Radio::RadioRuntimeTransferResult& terminal,
    std::uint64_t nowMilliseconds,
    std::uint64_t absoluteDeadlineMilliseconds) noexcept {
    if(!terminal.Domain||terminal.Result.TransferId==0) return ForwardingAttemptEvidenceDisposition::Invalid;
    if(absoluteDeadlineMilliseconds==0U||nowMilliseconds>=absoluteDeadlineMilliseconds)
        return ForwardingAttemptEvidenceDisposition::DeadlineExpired;
    switch(terminal.Result.Status){
        case Radio::RadioTransferTerminalStatus::Completed:
            return ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance;
        case Radio::RadioTransferTerminalStatus::Expired:
            return ForwardingAttemptEvidenceDisposition::DeadlineExpired;
        case Radio::RadioTransferTerminalStatus::TransmissionFailed:
        case Radio::RadioTransferTerminalStatus::EvidenceInsufficient:
        case Radio::RadioTransferTerminalStatus::ProviderUnavailable:
        case Radio::RadioTransferTerminalStatus::Cancelled:
            return ForwardingAttemptEvidenceDisposition::RetryableRouteFailure;
    }
    return ForwardingAttemptEvidenceDisposition::Invalid;
}

inline bool TryMapRouteAttemptOutcome(
    ForwardingAttemptEvidenceDisposition disposition,RouteAttemptOutcome& outcome) noexcept {
    switch(disposition){
        case ForwardingAttemptEvidenceDisposition::RetryableRouteFailure:
            outcome=RouteAttemptOutcome::RetryableFailure;return true;
        case ForwardingAttemptEvidenceDisposition::ResourceUnavailable:
            outcome=RouteAttemptOutcome::ResourceUnavailable;return true;
        case ForwardingAttemptEvidenceDisposition::DeadlineExpired:
            outcome=RouteAttemptOutcome::DeadlineExpired;return true;
        case ForwardingAttemptEvidenceDisposition::PermanentFailure:
        case ForwardingAttemptEvidenceDisposition::Invalid:
            outcome=RouteAttemptOutcome::PermanentFailure;return true;
        case ForwardingAttemptEvidenceDisposition::AwaitingNextHopAcceptance:
            return false;
    }
    return false;
}

} // namespace ESPressio::Mesh
