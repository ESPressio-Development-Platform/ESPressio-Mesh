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

    template<typename TExternalLifecycle>
    ApplicationRecipientTerminalizationResult TerminalizeExternal(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome,
        TExternalLifecycle& external
    ) noexcept {
        if (outcome == ApplicationRecipientOutcome::Pending || messageId == 0U ||
            !external.IsActive() || external.MessageId() != messageId) {
            return ApplicationRecipientTerminalizationResult::Invalid;
        }

        const auto update = _transmissions.SetRecipientOutcome(transmission, messageId, outcome);
        switch (update) {
            case ApplicationTransmissionUpdateResult::Updated:
                external.Reset();
                return ApplicationRecipientTerminalizationResult::Terminalized;
            case ApplicationTransmissionUpdateResult::AlreadyTerminal:
                external.Reset();
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

    template<typename TExternalLifecycle>
    ApplicationRecipientRetirementResult RetireTerminalExternal(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        TExternalLifecycle& external
    ) noexcept {
        if (!transmission || messageId == 0U || !external.IsActive() || external.MessageId() != messageId) {
            return ApplicationRecipientRetirementResult::Invalid;
        }
        if (!_transmissions.Contains(transmission)) return ApplicationRecipientRetirementResult::UnknownTransmission;

        ApplicationRecipientOutcome outcome{};
        if (!_transmissions.TryGetRecipientOutcome(transmission, messageId, outcome)) {
            return ApplicationRecipientRetirementResult::UnknownRecipient;
        }
        if (outcome == ApplicationRecipientOutcome::Pending) return ApplicationRecipientRetirementResult::NotTerminal;

        external.Reset();
        return ApplicationRecipientRetirementResult::Retired;
    }

public:
    explicit ApplicationRecipientLifecycleCoordinator(
        ApplicationTransmissionCoordinator<TransmissionCapacity, RecipientCapacity>& transmissions
    ) noexcept : _transmissions(transmissions) {}

    /// <summary>Inspects aggregate recipient authority without mutating aggregate or external delivery state.</summary>
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

    /// <summary>Commits a terminal recipient outcome and retires the exact matching outbound delivery lifecycle.</summary>
    ApplicationRecipientTerminalizationResult Terminalize(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        return TerminalizeExternal(transmission, messageId, outcome, delivery);
    }

    /// <summary>
    /// Commits a terminal recipient outcome and retires a composed external lifecycle exposing IsActive/MessageId/Reset.
    /// </summary>
    /// <remarks>
    /// This overload allows higher composition layers to retire wrappers which own additional bounded state (for example
    /// a retained Radio-terminal correlation) without duplicating aggregate-authority sequencing. Aggregate state is
    /// committed first; only Updated/AlreadyTerminal may retire the exact external lifecycle.
    /// </remarks>
    template<typename TExternalLifecycle>
    ApplicationRecipientTerminalizationResult TerminalizeComposed(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome,
        TExternalLifecycle& external
    ) noexcept {
        return TerminalizeExternal(transmission, messageId, outcome, external);
    }

    /// <summary>Retires external delivery state after the aggregate recipient became terminal elsewhere.</summary>
    ApplicationRecipientRetirementResult RetireTerminal(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        return RetireTerminalExternal(transmission, messageId, delivery);
    }

    /// <summary>Retires a composed exact external lifecycle after aggregate terminality was established elsewhere.</summary>
    template<typename TExternalLifecycle>
    ApplicationRecipientRetirementResult RetireTerminalComposed(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        TExternalLifecycle& external
    ) noexcept {
        return RetireTerminalExternal(transmission, messageId, external);
    }
};

} // namespace ESPressio::Mesh
