#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationRecipientLifecycleCoordinator.hpp"
#include "ESPressio_OutboundRadioDeliveryCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Application-level disposition after one outbound Radio-backed submission step.</summary>
enum class ApplicationRadioSubmissionDisposition : std::uint8_t {
    AwaitingNextHopAcceptance,
    RetryCurrentRoute,
    ReplanDistinctRoute,
    DeadlineExpired,
    PermanentFailure,
    AlreadyTerminal,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

/// <summary>Combined Radio submission detail plus aggregate-aware application disposition.</summary>
struct ApplicationRadioSubmissionResult final {
    ApplicationRadioSubmissionDisposition Disposition{ApplicationRadioSubmissionDisposition::Invalid};
    OutboundRadioForwardingResult Radio{};
};

/// <summary>
/// Preflights authoritative application-recipient state before one outbound Radio-backed submission and atomically
/// reconciles any synchronous definitive stop action with the aggregate.
/// </summary>
/// <remarks>
/// This coordinator owns no route, payload, Radio or aggregate storage. It prevents callers from submitting work for an
/// unknown/already-terminal aggregate recipient and then forgetting to reconcile immediate forwarding stop outcomes.
/// AwaitingNextHopAcceptance, RetryCurrentRoute and ReplanDistinctRoute remain non-terminal. StopDeadlineExpired commits
/// DeadlineExpired; StopPermanentFailure and StopAttemptLimit commit PermanentFailure. Radio admission/completion and
/// destination delivery acknowledgement retain their independent meanings.
/// </remarks>
template<
    std::size_t AcknowledgementCapacity,
    std::size_t CorrelationCapacity,
    std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
    std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class ApplicationRadioSubmissionCoordinator final {
    using RecipientLifecycle = ApplicationRecipientLifecycleCoordinator<
        AcknowledgementCapacity,
        TransmissionCapacity,
        RecipientCapacity
    >;
    using RadioDelivery = OutboundRadioDeliveryCoordinator<
        AcknowledgementCapacity,
        CorrelationCapacity,
        MembershipCapacity,
        BindingCapacity,
        HopCapacity
    >;

    RecipientLifecycle& _recipients;

    static ApplicationRadioSubmissionDisposition MapTerminalization(
        ApplicationRecipientTerminalizationResult result,
        ApplicationRadioSubmissionDisposition successful
    ) noexcept {
        switch (result) {
            case ApplicationRecipientTerminalizationResult::Terminalized: return successful;
            case ApplicationRecipientTerminalizationResult::AlreadyTerminal: return ApplicationRadioSubmissionDisposition::AlreadyTerminal;
            case ApplicationRecipientTerminalizationResult::UnknownRecipient: return ApplicationRadioSubmissionDisposition::UnknownRecipient;
            case ApplicationRecipientTerminalizationResult::UnknownTransmission: return ApplicationRadioSubmissionDisposition::UnknownTransmission;
            case ApplicationRecipientTerminalizationResult::Invalid: return ApplicationRadioSubmissionDisposition::Invalid;
        }
        return ApplicationRadioSubmissionDisposition::Invalid;
    }

public:
    explicit ApplicationRadioSubmissionCoordinator(RecipientLifecycle& recipients) noexcept
        : _recipients(recipients) {}

    ApplicationRadioSubmissionResult Submit(
        ApplicationTransmissionHandle transmission,
        RadioDelivery& delivery,
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint64_t nowMilliseconds
    ) {
        ApplicationRadioSubmissionResult result{};
        if (!transmission || !delivery.IsActive() || delivery.MessageId() == 0U) return result;

        const MeshMessageId messageId = delivery.MessageId();
        const auto inspection = _recipients.Inspect(transmission, messageId);
        switch (inspection) {
            case ApplicationRecipientInspectionResult::UnknownTransmission:
                result.Disposition = ApplicationRadioSubmissionDisposition::UnknownTransmission;
                return result;
            case ApplicationRecipientInspectionResult::UnknownRecipient:
                result.Disposition = ApplicationRadioSubmissionDisposition::UnknownRecipient;
                return result;
            case ApplicationRecipientInspectionResult::Invalid:
                return result;
            case ApplicationRecipientInspectionResult::Terminal: {
                const auto retirement = _recipients.RetireTerminalComposed(transmission, messageId, delivery);
                result.Disposition = retirement == ApplicationRecipientRetirementResult::Retired
                    ? ApplicationRadioSubmissionDisposition::AlreadyTerminal
                    : ApplicationRadioSubmissionDisposition::Invalid;
                return result;
            }
            case ApplicationRecipientInspectionResult::Pending:
                break;
        }

        result.Radio = delivery.Submit(
            localDevice,
            route,
            remainingHopLimit,
            payload,
            payloadSize,
            nowMilliseconds
        );

        switch (result.Radio.Action) {
            case OutboundForwardingAction::AwaitingNextHopAcceptance:
                result.Disposition = ApplicationRadioSubmissionDisposition::AwaitingNextHopAcceptance;
                break;
            case OutboundForwardingAction::RetryCurrentRoute:
                result.Disposition = ApplicationRadioSubmissionDisposition::RetryCurrentRoute;
                break;
            case OutboundForwardingAction::ReplanDistinctRoute:
                result.Disposition = ApplicationRadioSubmissionDisposition::ReplanDistinctRoute;
                break;
            case OutboundForwardingAction::StopDeadlineExpired:
                result.Disposition = MapTerminalization(
                    _recipients.TerminalizeComposed(
                        transmission,
                        messageId,
                        ApplicationRecipientOutcome::DeadlineExpired,
                        delivery
                    ),
                    ApplicationRadioSubmissionDisposition::DeadlineExpired
                );
                break;
            case OutboundForwardingAction::StopPermanentFailure:
            case OutboundForwardingAction::StopAttemptLimit:
                result.Disposition = MapTerminalization(
                    _recipients.TerminalizeComposed(
                        transmission,
                        messageId,
                        ApplicationRecipientOutcome::PermanentFailure,
                        delivery
                    ),
                    ApplicationRadioSubmissionDisposition::PermanentFailure
                );
                break;
            case OutboundForwardingAction::Invalid:
                result.Disposition = ApplicationRadioSubmissionDisposition::Invalid;
                break;
        }
        return result;
    }
};

} // namespace ESPressio::Mesh
