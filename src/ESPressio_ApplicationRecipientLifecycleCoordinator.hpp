#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationTransmissionCoordinator.hpp"

namespace ESPressio::Mesh {

enum class ApplicationRecipientTerminalizationResult : std::uint8_t {
    Terminalized,
    AlreadyTerminal,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

enum class ApplicationRecipientRetirementResult : std::uint8_t {
    Retired,
    NotTerminal,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

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

    /// <summary>Retires external delivery state after the aggregate recipient became terminal elsewhere.</summary>
    /// <remarks>
    /// This is intended for deadline sweeping and other independently owned terminalization paths. It never mutates the
    /// aggregate outcome and therefore cannot act as cancellation. A Pending recipient retains its active delivery state.
    /// </remarks>
    ApplicationRecipientRetirementResult RetireTerminal(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        if (!transmission || messageId == 0U || !delivery.IsActive() || delivery.MessageId() != messageId) {
            return ApplicationRecipientRetirementResult::Invalid;
        }
        if (!_transmissions.Contains(transmission)) return ApplicationRecipientRetirementResult::UnknownTransmission;

        ApplicationRecipientOutcome outcome{};
        if (!_transmissions.TryGetRecipientOutcome(transmission, messageId, outcome)) {
            return ApplicationRecipientRetirementResult::UnknownRecipient;
        }
        if (outcome == ApplicationRecipientOutcome::Pending) return ApplicationRecipientRetirementResult::NotTerminal;

        delivery.Reset();
        return ApplicationRecipientRetirementResult::Retired;
    }
};

} // namespace ESPressio::Mesh
