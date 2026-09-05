#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationTransmissionCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of terminalizing one frozen application recipient and retiring its external delivery lifecycle.</summary>
enum class ApplicationRecipientTerminalizationResult : std::uint8_t {
    Terminalized,
    AlreadyTerminal,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

/// <summary>
/// Narrow composition that keeps aggregate recipient outcome, external OutboundDeliveryLifecycle retirement and payload
/// retention ordered without introducing cancellation semantics.
/// </summary>
/// <remarks>
/// The aggregate remains retained after terminalization. This coordinator never releases the aggregate or its immutable
/// payload reference; callers may release the aggregate only after every recipient is terminal. Resetting the external
/// delivery lifecycle occurs only after the aggregate accepted the terminal outcome, so retryable/forwardable recipients
/// cannot silently lose route-attempt or destination-acknowledgement state.
/// </remarks>
template<
    std::size_t AcknowledgementCapacity,
    std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
    std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission
>
class ApplicationRecipientLifecycleCoordinator final {
    ApplicationTransmissionCoordinator<TransmissionCapacity, RecipientCapacity>& _transmissions;

public:
    explicit ApplicationRecipientLifecycleCoordinator(
        ApplicationTransmissionCoordinator<TransmissionCapacity, RecipientCapacity>& transmissions
    ) noexcept : _transmissions(transmissions) {}

    /// <summary>
    /// Commits a terminal recipient outcome, then retires the exact external per-recipient delivery lifecycle.
    /// </summary>
    ApplicationRecipientTerminalizationResult Terminalize(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        if (outcome == ApplicationRecipientOutcome::Pending || messageId == 0U) {
            return ApplicationRecipientTerminalizationResult::Invalid;
        }
        if (!delivery.IsActive() || delivery.MessageId() != messageId) {
            return ApplicationRecipientTerminalizationResult::Invalid;
        }

        const auto update = _transmissions.SetRecipientOutcome(transmission, messageId, outcome);
        switch (update) {
            case ApplicationTransmissionUpdateResult::Updated:
                delivery.Reset();
                return ApplicationRecipientTerminalizationResult::Terminalized;
            case ApplicationTransmissionUpdateResult::AlreadyTerminal:
                return ApplicationRecipientTerminalizationResult::AlreadyTerminal;
            case ApplicationTransmissionUpdateResult::UnknownRecipient:
                return ApplicationRecipientTerminalizationResult::UnknownRecipient;
            case ApplicationTransmissionUpdateResult::UnknownTransmission:
                return ApplicationRecipientTerminalizationResult::UnknownTransmission;
            case ApplicationTransmissionUpdateResult::Invalid:
                return ApplicationRecipientTerminalizationResult::Invalid;
        }
        return ApplicationRecipientTerminalizationResult::Invalid;
    }
};

} // namespace ESPressio::Mesh
