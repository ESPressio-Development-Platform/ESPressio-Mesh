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

/// <summary>Summary of one aggregate-deadline sweep and its synchronous external-lifecycle cleanup.</summary>
struct ApplicationDeadlineSweepResult final {
    std::size_t ExpiredTransmissions{0};
    std::size_t ExpiredRecipients{0};
    std::size_t RetiredExternalLifecycles{0};
    std::size_t ExternalLifecycleMismatches{0};
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

        const auto update = _transmissions.SetRecipientOutcomeAuthoritative(transmission, messageId, outcome);
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

    /// <summary>
    /// Expires every due aggregate recipient and synchronously retires any matching externally owned delivery lifecycle.
    /// </summary>
    /// <remarks>
    /// The resolver is invoked only after the aggregate has committed DeadlineExpired for the exact MessageId. It may
    /// return nullptr when that recipient has no active external lifecycle (for example, forwarding never began). When it
    /// returns a lifecycle, that object must expose IsActive(), MessageId() and Reset(). Exact MessageId matching is checked
    /// before Reset, so a resolver bug cannot cancel another recipient's bounded ACK/Radio/forwarding state. The resolver
    /// is composition-supplied rather than a Mesh-owned registry, preserving external lifecycle ownership and avoiding a
    /// second capacity/identity table. All cleanup happens synchronously inside the sweep before the caller regains control.
    /// </remarks>
    template<typename TExternalLifecycleResolver>
    ApplicationDeadlineSweepResult ExpireDueAndRetire(
        std::uint64_t nowMilliseconds,
        TExternalLifecycleResolver&& resolveExternalLifecycle
    ) noexcept {
        ApplicationDeadlineSweepResult result{};
        result.ExpiredTransmissions = _transmissions.ExpireDueWithRecipients(
            nowMilliseconds,
            [&](ApplicationTransmissionHandle transmission, MeshMessageId messageId) noexcept {
                ++result.ExpiredRecipients;
                auto* external = resolveExternalLifecycle(transmission, messageId);
                if (external == nullptr) return;
                if (!external->IsActive() || external->MessageId() != messageId) {
                    ++result.ExternalLifecycleMismatches;
                    return;
                }
                const auto retired = RetireTerminalExternal(transmission, messageId, *external);
                if (retired == ApplicationRecipientRetirementResult::Retired) {
                    ++result.RetiredExternalLifecycles;
                } else {
                    ++result.ExternalLifecycleMismatches;
                }
            }
        );
        return result;
    }
};

} // namespace ESPressio::Mesh
