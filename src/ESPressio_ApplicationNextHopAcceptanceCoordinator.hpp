#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationRecipientLifecycleCoordinator.hpp"
#include "ESPressio_OutboundRadioDeliveryCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Application-level disposition after authenticated next-hop Mesh acceptance evidence is evaluated.</summary>
enum class ApplicationNextHopAcceptanceDisposition : std::uint8_t {
    ForwardingTransitionCommitted,
    UnrelatedEvidence,
    DeadlineExpired,
    PermanentFailure,
    AlreadyTerminal,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

/// <summary>
/// Preflights authoritative application-recipient state before applying authenticated next-hop acceptance and reconciles
/// definitive acceptance failures with the aggregate without conflating hop acceptance with final destination delivery.
/// </summary>
/// <remarks>
/// Unknown aggregate/message state cannot consume the pending forwarding transition, release Radio correlation or
/// decrement RemainingHopLimit. Already-terminal aggregate authority retires exact composed delivery state without
/// applying late acceptance. Wrong node/incarnation/message evidence remains unrelated and leaves aggregate state Pending.
///
/// A committed next-hop transition decrements RemainingHopLimit exactly once and transfers forwarding responsibility to
/// that hop, but the aggregate recipient remains Pending: next-hop acceptance is not final destination acknowledgement.
/// Deadline expiry commits DeadlineExpired. Hop-limit/internal definitive acceptance failure commits PermanentFailure.
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
class ApplicationNextHopAcceptanceCoordinator final {
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

    static ApplicationNextHopAcceptanceDisposition MapTerminalization(
        ApplicationRecipientTerminalizationResult result,
        ApplicationNextHopAcceptanceDisposition successful
    ) noexcept {
        switch (result) {
            case ApplicationRecipientTerminalizationResult::Terminalized: return successful;
            case ApplicationRecipientTerminalizationResult::AlreadyTerminal: return ApplicationNextHopAcceptanceDisposition::AlreadyTerminal;
            case ApplicationRecipientTerminalizationResult::UnknownRecipient: return ApplicationNextHopAcceptanceDisposition::UnknownRecipient;
            case ApplicationRecipientTerminalizationResult::UnknownTransmission: return ApplicationNextHopAcceptanceDisposition::UnknownTransmission;
            case ApplicationRecipientTerminalizationResult::Invalid: return ApplicationNextHopAcceptanceDisposition::Invalid;
        }
        return ApplicationNextHopAcceptanceDisposition::Invalid;
    }

public:
    explicit ApplicationNextHopAcceptanceCoordinator(RecipientLifecycle& recipients) noexcept
        : _recipients(recipients) {}

    ApplicationNextHopAcceptanceDisposition ApplyAuthenticated(
        ApplicationTransmissionHandle transmission,
        RadioDelivery& delivery,
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acceptedMessageId,
        std::uint64_t nowMilliseconds,
        RemainingHopLimit& remainingHopLimit
    ) noexcept {
        if (!transmission || !delivery.IsActive() || delivery.MessageId() == 0U || acceptedMessageId == 0U) {
            return ApplicationNextHopAcceptanceDisposition::Invalid;
        }

        const MeshMessageId messageId = delivery.MessageId();
        const auto inspection = _recipients.Inspect(transmission, messageId);
        switch (inspection) {
            case ApplicationRecipientInspectionResult::UnknownTransmission:
                return ApplicationNextHopAcceptanceDisposition::UnknownTransmission;
            case ApplicationRecipientInspectionResult::UnknownRecipient:
                return ApplicationNextHopAcceptanceDisposition::UnknownRecipient;
            case ApplicationRecipientInspectionResult::Invalid:
                return ApplicationNextHopAcceptanceDisposition::Invalid;
            case ApplicationRecipientInspectionResult::Terminal: {
                const auto retirement = _recipients.RetireTerminalComposed(transmission, messageId, delivery);
                return retirement == ApplicationRecipientRetirementResult::Retired
                    ? ApplicationNextHopAcceptanceDisposition::AlreadyTerminal
                    : ApplicationNextHopAcceptanceDisposition::Invalid;
            }
            case ApplicationRecipientInspectionResult::Pending:
                break;
        }

        const auto action = delivery.AcceptNextHopAuthenticated(
            authenticatedSource,
            authenticatedSourceIncarnation,
            acceptedMessageId,
            nowMilliseconds,
            remainingHopLimit
        );
        switch (action) {
            case ForwardingAcceptanceAction::ForwardingComplete:
                return ApplicationNextHopAcceptanceDisposition::ForwardingTransitionCommitted;
            case ForwardingAcceptanceAction::IgnoreUnrelatedEvidence:
                return ApplicationNextHopAcceptanceDisposition::UnrelatedEvidence;
            case ForwardingAcceptanceAction::StopDeadlineExpired:
                return MapTerminalization(
                    _recipients.TerminalizeComposed(
                        transmission,
                        messageId,
                        ApplicationRecipientOutcome::DeadlineExpired,
                        delivery
                    ),
                    ApplicationNextHopAcceptanceDisposition::DeadlineExpired
                );
            case ForwardingAcceptanceAction::StopPermanentFailure:
                return MapTerminalization(
                    _recipients.TerminalizeComposed(
                        transmission,
                        messageId,
                        ApplicationRecipientOutcome::PermanentFailure,
                        delivery
                    ),
                    ApplicationNextHopAcceptanceDisposition::PermanentFailure
                );
        }
        return ApplicationNextHopAcceptanceDisposition::Invalid;
    }
};

} // namespace ESPressio::Mesh
