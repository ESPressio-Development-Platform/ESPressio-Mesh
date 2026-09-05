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

enum class ApplicationRecipientInspectionResult : std::uint8_t {
    Pending,
    Terminal,
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

    /// <summary>Inspects aggregate recipient authority without mutating aggregate or external delivery state.</summary>
    /// <remarks>
    /// Composition layers use this before consuming independently-owned terminal evidence such as destination ACK state.
    /// It prevents an external lifecycle from being mutated when the aggregate/message pair is unknown and lets an
    /// already-terminal aggregate retire stale external state without first consuming a late ACK.
    /// </remarks>
    ApplicationRecipientInspectionResult Inspect(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome* outcome = nullptr
    ) const noexcept {
        if (!transmission || messageId == 0U) return ApplicationRecipientInspectionResult::Invalid;
        if (!_transmissions.Contains(transmission)) return ApplicationRecipientInspectionResult::UnknownTransmission;

        ApplicationRecipientOutcome current{};
        if (!_transmissions.TryGetRecipientOutcome(transmission, messageId, current)) {
            return ApplicationRecipientInspectionResult::UnknownRecipient;
        }
        if (outcome != nullptr) *outcome = current;
        return current == ApplicationRecipientOutcome::Pending
            ? ApplicationRecipientInspectionResult::Pending
            : ApplicationRecipientInspectionResult::Terminal;
    }

    /// <summary>Commits a terminal recipient outcome and retires the exact matching external delivery lifecycle.</summary>
    /// <remarks>
    /// Aggregate state is authoritative for recipient terminality. If another owner (for example deadline sweeping)
    /// already terminalized the recipient, an exact matching active delivery is still retired here while the previously
    /// committed aggregate outcome is preserved. This closes the race between independently owned terminalization paths
    /// without allowing a late result to overwrite DeadlineExpired or another terminal outcome.
    /// </remarks>
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
                // The aggregate outcome won the race and must not be overwritten, but the exact external lifecycle is
                // now stale and must release any pending destination-ACK reservation/forwarding transition.
                delivery.Reset();
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
