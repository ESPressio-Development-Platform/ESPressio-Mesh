#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ForwardingAttemptLifecycle.hpp"
#include "ESPressio_ForwardingRadioTerminalCorrelation.hpp"

namespace ESPressio::Mesh {

/// <summary>State of optional deferred Radio-terminal correlation for one forwarding submission.</summary>
enum class ForwardingRadioCorrelationDisposition : std::uint8_t {
    NotRequired,
    Reserved,
    Bound,
    ResourceUnavailable,
    BindingUnavailable
};

/// <summary>Result of one forwarding submission coordinated with bounded Radio-terminal correlation.</summary>
struct ForwardingRadioAttemptResult final {
    ForwardingSubmissionResult Submission{};
    ForwardingAttemptAction Action{ForwardingAttemptAction::StopPermanentFailure};
    ForwardingRadioCorrelationHandle Correlation{};
    ForwardingRadioCorrelationDisposition CorrelationDisposition{ForwardingRadioCorrelationDisposition::NotRequired};
};

/// <summary>
/// Narrow composition helper which reserves Mesh-side terminal correlation before Radio submission and binds it to the
/// deferred RadioTransport handle immediately after an accepted Send returns.
/// </summary>
/// <remarks>
/// Reservation happens before any Radio fragment can be accepted, so Mesh correlation pressure is explicit bounded
/// backpressure rather than an after-the-fact loss of evidence. Synchronously terminal Radio evidence requires no retained
/// correlation: it is already present in ForwardingSubmissionResult and still does not constitute Mesh next-hop
/// acceptance. If a provider/transport violates the expected deferred-handle uniqueness contract after acceptance,
/// BindingUnavailable is reported but the accepted forwarding attempt remains pending; Mesh must not retry merely because
/// local observation correlation failed while already-accepted Radio work may still succeed.
///
/// This coordinator owns no payload, route, timer, retry counter, HopLimit or acceptance state. The owning serialized Mesh
/// execution domain is responsible for invoking Submit/TryConsumeTerminal and for releasing abandoned correlations.
/// </remarks>
template<
    std::size_t CorrelationCapacity,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class ForwardingRadioAttemptCoordinator final {
    ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& _submission;
    ForwardingRadioTerminalCorrelation<CorrelationCapacity>& _correlation;
    const RouteAttemptCoordinator& _attempts;

public:
    ForwardingRadioAttemptCoordinator(
        ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& submission,
        ForwardingRadioTerminalCorrelation<CorrelationCapacity>& correlation,
        const RouteAttemptCoordinator& attempts
    ) noexcept : _submission(submission), _correlation(correlation), _attempts(attempts) {}

    ForwardingRadioAttemptResult Submit(
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) {
        ForwardingRadioAttemptResult result;
        result.Correlation = _correlation.Reserve();
        if (!result.Correlation) {
            result.Submission.Disposition = ForwardingSubmissionDisposition::ResourceUnavailable;
            result.Action = ForwardingAttemptLifecycle::AfterSubmission(
                result.Submission, _attempts, nowMilliseconds, absoluteDeadlineMilliseconds
            );
            result.CorrelationDisposition = ForwardingRadioCorrelationDisposition::ResourceUnavailable;
            return result;
        }
        result.CorrelationDisposition = ForwardingRadioCorrelationDisposition::Reserved;

        result.Submission = _submission.Submit(
            localDevice,
            route,
            remainingHopLimit,
            payload,
            payloadSize,
            nowMilliseconds,
            absoluteDeadlineMilliseconds
        );
        result.Action = ForwardingAttemptLifecycle::AfterSubmission(
            result.Submission, _attempts, nowMilliseconds, absoluteDeadlineMilliseconds
        );

        if (!result.Submission) {
            _correlation.Release(result.Correlation);
            result.Correlation = {};
            result.CorrelationDisposition = ForwardingRadioCorrelationDisposition::NotRequired;
            return result;
        }

        if (!result.Submission.RadioResult.DeferredTransfer) {
            _correlation.Release(result.Correlation);
            result.Correlation = {};
            result.CorrelationDisposition = ForwardingRadioCorrelationDisposition::NotRequired;
            return result;
        }

        if (_correlation.Bind(result.Correlation, result.Submission.RadioResult.DeferredTransfer)) {
            result.CorrelationDisposition = ForwardingRadioCorrelationDisposition::Bound;
            return result;
        }

        _correlation.Release(result.Correlation);
        result.Correlation = {};
        result.CorrelationDisposition = ForwardingRadioCorrelationDisposition::BindingUnavailable;
        return result;
    }

    /// <summary>Consumes terminal Radio evidence for one bound forwarding attempt and evaluates its bounded lifecycle.</summary>
    bool TryConsumeTerminal(
        ForwardingRadioCorrelationHandle correlation,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds,
        ForwardingAttemptAction& action,
        Radio::LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept {
        ForwardingRadioTerminalObservation observation;
        if (!_correlation.TryTake(correlation, observation)) return false;
        if (terminal != nullptr) *terminal = observation.Terminal;
        action = ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(
            observation.Terminal,
            _attempts,
            nowMilliseconds,
            absoluteDeadlineMilliseconds
        );
        return true;
    }

    /// <summary>Releases retained correlation when the owning forwarding attempt is abandoned for another reason.</summary>
    bool Release(ForwardingRadioCorrelationHandle correlation) noexcept {
        return _correlation.Release(correlation);
    }
};

} // namespace ESPressio::Mesh
