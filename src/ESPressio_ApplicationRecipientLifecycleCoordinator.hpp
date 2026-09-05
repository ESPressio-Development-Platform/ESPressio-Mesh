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

struct ApplicationDeadlineSweepResult final {
    std::size_t ExpiredTransmissions{0};
    std::size_t ExpiredRecipients{0};
    std::size_t RetiredExternalLifecycles{0};
    std::size_t ExternalLifecycleMismatches{0};
};

/// <summary>Summary of one controlled local teardown of application aggregate and external recipient state.</summary>
struct ApplicationControlledResetResult final {
    std::size_t ReleasedTransmissions{0};
    std::size_t RecipientRecordsVisited{0};
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

    ApplicationRecipientTerminalizationResult Terminalize(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        return TerminalizeExternal(transmission, messageId, outcome, delivery);
    }

    template<typename TExternalLifecycle>
    ApplicationRecipientTerminalizationResult TerminalizeComposed(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome,
        TExternalLifecycle& external
    ) noexcept {
        return TerminalizeExternal(transmission, messageId, outcome, external);
    }

    ApplicationRecipientRetirementResult RetireTerminal(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        return RetireTerminalExternal(transmission, messageId, delivery);
    }

    template<typename TExternalLifecycle>
    ApplicationRecipientRetirementResult RetireTerminalComposed(
        ApplicationTransmissionHandle transmission,
        MeshMessageId messageId,
        TExternalLifecycle& external
    ) noexcept {
        return RetireTerminalExternal(transmission, messageId, external);
    }

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

    /// <summary>
    /// Performs controlled local teardown of every retained application aggregate and its exact external recipient state.
    /// </summary>
    /// <remarks>
    /// This is shutdown/reset cleanup, not a delivery outcome and not a distributed cancellation protocol. No recipient is
    /// marked Delivered, PermanentFailure or DeadlineExpired merely because the local Mesh service is being reset. The
    /// resolver is called for every retained recipient before aggregate records and Application traffic reservations are
    /// discarded. A nullptr means no external lifecycle is retained. An inactive lifecycle is already clean. An active
    /// lifecycle whose MessageId differs is reported as a mismatch and is never reset through the wrong recipient identity.
    /// After traversal, local aggregate/payload references and traffic reservations are released deterministically even if
    /// a composition bug produced a mismatch; the mismatch count lets the composition owner detect its own orphaned state.
    /// </remarks>
    template<typename TExternalLifecycleResolver>
    ApplicationControlledResetResult ResetForControlledShutdown(
        TExternalLifecycleResolver&& resolveExternalLifecycle
    ) noexcept {
        ApplicationControlledResetResult result{};
        for (std::size_t slot = 0; slot < TransmissionCapacity; ++slot) {
            for (std::uint16_t generation = 1U; generation != 0U; ++generation) {
                const ApplicationTransmissionHandle handle{static_cast<std::uint16_t>(slot), generation};
                if (!_transmissions.Contains(handle)) continue;
                ++result.ReleasedTransmissions;
                const std::size_t count = _transmissions._transmissions.RecipientCount(handle);
                for (std::size_t index = 0; index < count; ++index) {
                    ApplicationTransmissionRecipient recipient{};
                    ApplicationRecipientOutcome outcome{};
                    if (!_transmissions._transmissions.TryGetRecipient(handle, index, recipient, outcome)) continue;
                    ++result.RecipientRecordsVisited;
                    auto* external = resolveExternalLifecycle(handle, recipient.MessageId);
                    if (external == nullptr || !external->IsActive()) continue;
                    if (external->MessageId() != recipient.MessageId) {
                        ++result.ExternalLifecycleMismatches;
                        continue;
                    }
                    external->Reset();
                    ++result.RetiredExternalLifecycles;
                }
                break;
            }
        }
        _transmissions.ResetForControlledShutdown();
        return result;
    }
};

} // namespace ESPressio::Mesh
