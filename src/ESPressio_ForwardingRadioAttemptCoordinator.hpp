#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ForwardingAttemptLifecycle.hpp"
#include "ESPressio_ForwardingRadioTerminalCorrelation.hpp"

namespace ESPressio::Mesh {

enum class ForwardingRadioCorrelationDisposition : std::uint8_t {
    NotRequired,
    Reserved,
    Bound,
    ResourceUnavailable,
    BindingUnavailable
};

struct ForwardingRadioAttemptResult final {
    ForwardingSubmissionResult Submission{};
    ForwardingAttemptAction Action{ForwardingAttemptAction::StopPermanentFailure};
    ForwardingRadioCorrelationHandle Correlation{};
    ForwardingRadioCorrelationDisposition CorrelationDisposition{ForwardingRadioCorrelationDisposition::NotRequired};
};

/// <summary>Coordinates one final-Radio submission with bounded domain-qualified terminal correlation.</summary>
template<std::size_t CorrelationCapacity,
         std::size_t MembershipCapacity=Limits::MaxMeshNodes,
         std::size_t BindingCapacity=Limits::MaxTopologyLinks,
         std::size_t HopCapacity=Limits::MaxRouteHops>
class ForwardingRadioAttemptCoordinator final {
    ForwardingSubmissionCoordinator<MembershipCapacity,BindingCapacity,HopCapacity>& _submission;
    ForwardingRadioTerminalCorrelation<CorrelationCapacity>& _correlation;
    const RouteAttemptCoordinator& _attempts;
public:
    ForwardingRadioAttemptCoordinator(
        ForwardingSubmissionCoordinator<MembershipCapacity,BindingCapacity,HopCapacity>& submission,
        ForwardingRadioTerminalCorrelation<CorrelationCapacity>& correlation,
        const RouteAttemptCoordinator& attempts) noexcept
        :_submission(submission),_correlation(correlation),_attempts(attempts) {}

    ForwardingRadioAttemptResult Submit(
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        MeshRelayServiceClass service,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds) noexcept {
        ForwardingRadioAttemptResult result{};
        result.Correlation=_correlation.Reserve();
        if(!result.Correlation){
            result.Submission.Disposition=ForwardingSubmissionDisposition::ResourceUnavailable;
            result.Action=ForwardingAttemptLifecycle::AfterSubmission(result.Submission,_attempts,nowMilliseconds,absoluteDeadlineMilliseconds);
            result.CorrelationDisposition=ForwardingRadioCorrelationDisposition::ResourceUnavailable;
            return result;
        }
        result.CorrelationDisposition=ForwardingRadioCorrelationDisposition::Reserved;
        result.Submission=_submission.Submit(localDevice,route,remainingHopLimit,service,payload,payloadSize,
                                             nowMilliseconds,absoluteDeadlineMilliseconds);
        result.Action=ForwardingAttemptLifecycle::AfterSubmission(result.Submission,_attempts,nowMilliseconds,absoluteDeadlineMilliseconds);
        if(!result.Submission){
            _correlation.Release(result.Correlation);result.Correlation={};
            result.CorrelationDisposition=ForwardingRadioCorrelationDisposition::NotRequired;
            return result;
        }
        if(_correlation.Bind(result.Correlation,result.Submission.RadioDomain,result.Submission.RadioResult.TransferId)){
            result.CorrelationDisposition=ForwardingRadioCorrelationDisposition::Bound;
            return result;
        }
        _correlation.Release(result.Correlation);result.Correlation={};
        result.CorrelationDisposition=ForwardingRadioCorrelationDisposition::BindingUnavailable;
        return result;
    }

    bool TryConsumeTerminal(
        ForwardingRadioCorrelationHandle correlation,std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds,ForwardingAttemptAction& action,
        Radio::RadioRuntimeTransferResult* terminal=nullptr) noexcept {
        ForwardingRadioTerminalObservation observation{};
        if(!_correlation.TryTake(correlation,observation)) return false;
        if(terminal!=nullptr)*terminal=observation.Terminal;
        action=ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(
            observation.Terminal,_attempts,nowMilliseconds,absoluteDeadlineMilliseconds);
        return true;
    }

    bool Release(ForwardingRadioCorrelationHandle correlation) noexcept {return _correlation.Release(correlation);}
};

} // namespace ESPressio::Mesh
