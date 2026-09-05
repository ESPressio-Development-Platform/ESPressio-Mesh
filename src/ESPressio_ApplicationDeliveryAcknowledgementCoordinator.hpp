#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationRecipientLifecycleCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of applying one authenticated destination acknowledgement to an application recipient lifecycle.</summary>
enum class ApplicationDeliveryAcknowledgementResult : std::uint8_t {
    Delivered,
    DeadlineExpired,
    Unrelated,
    AlreadyTerminal,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

/// <summary>
/// Atomically composes sender-local destination-ACK consumption with aggregate application-recipient terminalization.
/// </summary>
/// <remarks>
/// Callers must use this boundary instead of applying an ACK directly to OutboundDeliveryLifecycle when that delivery
/// belongs to an ApplicationTransmission aggregate. A successfully authenticated destination ACK first updates the exact
/// per-delivery ACK lifecycle and then commits the corresponding aggregate recipient outcome before retiring the external
/// delivery state. Thus the ACK tracker and aggregate cannot accidentally diverge.
///
/// An ACK still means only definitive destination-framework acceptance of this Mesh delivery. It is not application work
/// completion. Radio/link evidence and next-hop acceptance remain unrelated. Existing aggregate terminal outcomes win
/// races: a late ACK cannot overwrite DeadlineExpired or another terminal result, but the exact stale delivery lifecycle
/// is still retired by ApplicationRecipientLifecycleCoordinator.
/// </remarks>
template<
    std::size_t AcknowledgementCapacity,
    std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
    std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission
>
class ApplicationDeliveryAcknowledgementCoordinator final {
    ApplicationRecipientLifecycleCoordinator<
        AcknowledgementCapacity,
        TransmissionCapacity,
        RecipientCapacity
    >& _recipients;

    static ApplicationDeliveryAcknowledgementResult MapTerminalization(
        ApplicationRecipientTerminalizationResult result,
        ApplicationDeliveryAcknowledgementResult successful
    ) noexcept {
        switch (result) {
            case ApplicationRecipientTerminalizationResult::Terminalized: return successful;
            case ApplicationRecipientTerminalizationResult::AlreadyTerminal: return ApplicationDeliveryAcknowledgementResult::AlreadyTerminal;
            case ApplicationRecipientTerminalizationResult::UnknownRecipient: return ApplicationDeliveryAcknowledgementResult::UnknownRecipient;
            case ApplicationRecipientTerminalizationResult::UnknownTransmission: return ApplicationDeliveryAcknowledgementResult::UnknownTransmission;
            case ApplicationRecipientTerminalizationResult::Invalid: return ApplicationDeliveryAcknowledgementResult::Invalid;
        }
        return ApplicationDeliveryAcknowledgementResult::Invalid;
    }

public:
    explicit ApplicationDeliveryAcknowledgementCoordinator(
        ApplicationRecipientLifecycleCoordinator<
            AcknowledgementCapacity,
            TransmissionCapacity,
            RecipientCapacity
        >& recipients
    ) noexcept : _recipients(recipients) {}

    ApplicationDeliveryAcknowledgementResult ApplyAuthenticated(
        ApplicationTransmissionHandle transmission,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery,
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acknowledgedMessageId,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!transmission || !delivery.IsActive() || acknowledgedMessageId == 0U ||
            delivery.MessageId() != acknowledgedMessageId) {
            return ApplicationDeliveryAcknowledgementResult::Invalid;
        }

        const auto action = delivery.ApplyDestinationAcknowledgementAuthenticated(
            authenticatedSource,
            authenticatedSourceIncarnation,
            acknowledgedMessageId,
            nowMilliseconds
        );

        switch (action) {
            case OutboundDeliveryAcknowledgementAction::DeliveryConfirmed:
                return MapTerminalization(
                    _recipients.Terminalize(
                        transmission,
                        acknowledgedMessageId,
                        ApplicationRecipientOutcome::Delivered,
                        delivery
                    ),
                    ApplicationDeliveryAcknowledgementResult::Delivered
                );
            case OutboundDeliveryAcknowledgementAction::StopDeadlineExpired:
                return MapTerminalization(
                    _recipients.Terminalize(
                        transmission,
                        acknowledgedMessageId,
                        ApplicationRecipientOutcome::DeadlineExpired,
                        delivery
                    ),
                    ApplicationDeliveryAcknowledgementResult::DeadlineExpired
                );
            case OutboundDeliveryAcknowledgementAction::IgnoreUnrelatedAcknowledgement:
                return ApplicationDeliveryAcknowledgementResult::Unrelated;
            case OutboundDeliveryAcknowledgementAction::Invalid:
                return ApplicationDeliveryAcknowledgementResult::Invalid;
        }
        return ApplicationDeliveryAcknowledgementResult::Invalid;
    }
};

} // namespace ESPressio::Mesh
