#pragma once

#include <cstdint>

#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_RouteAttemptPolicy.hpp"

namespace ESPressio::Mesh {

/// <summary>Whether an immediate forwarding submission can already resolve the enclosing route attempt.</summary>
enum class ForwardingAttemptResolutionState : std::uint8_t {
    /// <summary>The Radio submission was accepted; Mesh delivery evidence is still required.</summary>
    AwaitingDeliveryEvidence,

    /// <summary>The submission itself produced a route-attempt outcome that can be evaluated immediately.</summary>
    Resolved
};

/// <summary>
/// Result of translating only the immediate forwarding-submission disposition into route-attempt semantics.
/// </summary>
/// <remarks>
/// An accepted Radio logical transfer is deliberately never translated to RouteAttemptOutcome::Delivered here, even
/// when every fragment reports transmission completion or peer acknowledgement. Those are direct-link facts only.
/// Mesh delivery confirmation remains a distinct later input and is the only path that may resolve the route attempt as
/// Delivered and permit a successful forwarding transition to consume RemainingHopLimit.
/// </remarks>
struct ForwardingAttemptResolution final {
    ForwardingAttemptResolutionState State{ForwardingAttemptResolutionState::Resolved};
    RouteAttemptOutcome Outcome{RouteAttemptOutcome::PermanentFailure};

    constexpr bool IsResolved() const noexcept {
        return State == ForwardingAttemptResolutionState::Resolved;
    }
};

/// <summary>
/// Maps immediate forwarding-submission failures to the existing route-attempt vocabulary while preserving accepted
/// submissions as pending delivery evidence.
/// </summary>
constexpr ForwardingAttemptResolution ResolveForwardingSubmission(
    const ForwardingSubmissionResult& submission
) noexcept {
    using D = ForwardingSubmissionDisposition;
    switch (submission.Disposition) {
        case D::Accepted:
            return {ForwardingAttemptResolutionState::AwaitingDeliveryEvidence, RouteAttemptOutcome::RouteUnavailable};
        case D::DeadlineExpired:
            return {ForwardingAttemptResolutionState::Resolved, RouteAttemptOutcome::DeadlineExpired};
        case D::ResourceUnavailable:
            return {ForwardingAttemptResolutionState::Resolved, RouteAttemptOutcome::ResourceUnavailable};
        case D::RetryableFailure:
            return {ForwardingAttemptResolutionState::Resolved, RouteAttemptOutcome::RetryableFailure};
        case D::HopLimitExhausted:
        case D::MembershipUnavailable:
        case D::PeerUnavailable:
            return {ForwardingAttemptResolutionState::Resolved, RouteAttemptOutcome::RouteUnavailable};
        case D::PermanentFailure:
        case D::Invalid:
            return {ForwardingAttemptResolutionState::Resolved, RouteAttemptOutcome::PermanentFailure};
    }
    return {ForwardingAttemptResolutionState::Resolved, RouteAttemptOutcome::PermanentFailure};
}

/// <summary>
/// Resolves a later authenticated Mesh delivery confirmation into the only successful route-attempt outcome.
/// </summary>
constexpr RouteAttemptOutcome ResolveMeshDeliveryConfirmation(bool deliveryConfirmed) noexcept {
    return deliveryConfirmed ? RouteAttemptOutcome::Delivered : RouteAttemptOutcome::RetryableFailure;
}

} // namespace ESPressio::Mesh
