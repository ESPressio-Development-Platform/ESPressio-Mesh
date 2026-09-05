#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationRecipientLifecycleCoordinator.hpp"
#include "ESPressio_OutboundRadioDeliveryCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Application-level interpretation of one deferred outbound Radio-terminal processing step.</summary>
enum class ApplicationRadioTerminalDisposition : std::uint8_t {
    NoTerminalEvidence,
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

/// <summary>
/// Composes deferred Radio/route terminal processing with authoritative application-recipient aggregate state.
/// </summary>
/// <remarks>
/// Aggregate authority is inspected before Radio terminal evidence is consumed. Unknown aggregate/message state therefore
/// cannot mutate the outbound Radio correlation, route-attempt state or pending next-hop acceptance. If the aggregate is
/// already terminal, the exact composed outbound lifecycle is retired without consuming late Radio evidence.
///
/// For a known Pending recipient, Radio completion/peer acknowledgement still means only AwaitingNextHopAcceptance.
/// RetryCurrentRoute and ReplanDistinctRoute remain non-terminal. Only a definitive deadline stop or exhaustion/permanent
/// failure terminalizes the aggregate recipient. `StopAttemptLimit` is mapped to PermanentFailure because all policy-
/// permitted attempts/routes for that delivery are exhausted; it does not imply any application operation completed.
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
class ApplicationRadioTerminalCoordinator final {
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

    static ApplicationRadioTerminalDisposition MapTerminalization(
        ApplicationRecipientTerminalizationResult result,
        ApplicationRadioTerminalDisposition successful
    ) noexcept {
        switch (result) {
            case ApplicationRecipientTerminalizationResult::Terminalized: return successful;
            case ApplicationRecipientTerminalizationResult::AlreadyTerminal: return ApplicationRadioTerminalDisposition::AlreadyTerminal;
            case ApplicationRecipientTerminalizationResult::UnknownRecipient: return ApplicationRadioTerminalDisposition::UnknownRecipient;
            case ApplicationRecipientTerminalizationResult::UnknownTransmission: return ApplicationRadioTerminalDisposition::UnknownTransmission;
            case ApplicationRecipientTerminalizationResult::Invalid: return ApplicationRadioTerminalDisposition::Invalid;
        }
        return ApplicationRadioTerminalDisposition::Invalid;
    }

public:
    explicit ApplicationRadioTerminalCoordinator(RecipientLifecycle& recipients) noexcept
        : _recipients(recipients) {}

    ApplicationRadioTerminalDisposition TryConsume(
        ApplicationTransmissionHandle transmission,
        RadioDelivery& delivery,
        std::uint64_t nowMilliseconds,
        Radio::LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept {
        if (!transmission || !delivery.IsActive() || delivery.MessageId() == 0U) {
            return ApplicationRadioTerminalDisposition::Invalid;
        }

        const MeshMessageId messageId = delivery.MessageId();
        const auto inspection = _recipients.Inspect(transmission, messageId);
        switch (inspection) {
            case ApplicationRecipientInspectionResult::UnknownTransmission:
                return ApplicationRadioTerminalDisposition::UnknownTransmission;
            case ApplicationRecipientInspectionResult::UnknownRecipient:
                return ApplicationRadioTerminalDisposition::UnknownRecipient;
            case ApplicationRecipientInspectionResult::Invalid:
                return ApplicationRadioTerminalDisposition::Invalid;
            case ApplicationRecipientInspectionResult::Terminal: {
                const auto retirement = _recipients.RetireTerminalComposed(transmission, messageId, delivery);
                return retirement == ApplicationRecipientRetirementResult::Retired
                    ? ApplicationRadioTerminalDisposition::AlreadyTerminal
                    : ApplicationRadioTerminalDisposition::Invalid;
            }
            case ApplicationRecipientInspectionResult::Pending:
                break;
        }

        OutboundForwardingAction action{};
        if (!delivery.TryConsumeRadioTerminal(nowMilliseconds, action, terminal)) {
            return ApplicationRadioTerminalDisposition::NoTerminalEvidence;
        }

        switch (action) {
            case OutboundForwardingAction::AwaitingNextHopAcceptance:
                return ApplicationRadioTerminalDisposition::AwaitingNextHopAcceptance;
            case OutboundForwardingAction::RetryCurrentRoute:
                return ApplicationRadioTerminalDisposition::RetryCurrentRoute;
            case OutboundForwardingAction::ReplanDistinctRoute:
                return ApplicationRadioTerminalDisposition::ReplanDistinctRoute;
            case OutboundForwardingAction::StopDeadlineExpired:
                return MapTerminalization(
                    _recipients.TerminalizeComposed(
                        transmission,
                        messageId,
                        ApplicationRecipientOutcome::DeadlineExpired,
                        delivery
                    ),
                    ApplicationRadioTerminalDisposition::DeadlineExpired
                );
            case OutboundForwardingAction::StopPermanentFailure:
            case OutboundForwardingAction::StopAttemptLimit:
                return MapTerminalization(
                    _recipients.TerminalizeComposed(
                        transmission,
                        messageId,
                        ApplicationRecipientOutcome::PermanentFailure,
                        delivery
                    ),
                    ApplicationRadioTerminalDisposition::PermanentFailure
                );
            case OutboundForwardingAction::Invalid:
                return ApplicationRadioTerminalDisposition::Invalid;
        }
        return ApplicationRadioTerminalDisposition::Invalid;
    }
};

} // namespace ESPressio::Mesh
