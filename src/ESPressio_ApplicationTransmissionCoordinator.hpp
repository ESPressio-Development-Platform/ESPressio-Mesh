#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_OutboundDeliveryLifecycle.hpp"

namespace ESPressio::Mesh {

enum class ApplicationTransmissionAdmissionResult : std::uint8_t {
    Begun,
    ResourceUnavailable,
    DeadlineExpired,
    DuplicateRecipient,
    DuplicateMessageId,
    Invalid
};

enum class ApplicationRecipientBeginResult : std::uint8_t {
    Begun,
    AlreadyTerminal,
    ResourceUnavailable,
    DeadlineExpired,
    UnknownRecipient,
    UnknownTransmission,
    Invalid
};

/// <summary>
/// Composes one application-traffic reservation with a bounded sender-local transmission aggregate and independently
/// supplied per-recipient outbound delivery lifecycles.
/// </summary>
/// <remarks>
/// One Application reservation represents one accepted aggregate, not every frozen recipient. Recipient deliveries keep
/// independent MeshMessageIds, route-attempt state and destination-acknowledgement state. This coordinator deliberately
/// does not own payload bytes, route state, acknowledgement storage, Radio work or a second recipient-capacity pool.
/// The caller supplies the OutboundDeliveryLifecycle used for a recipient, allowing delivery execution to remain bounded
/// by the application's existing scheduling/composition policy rather than silently introducing a 8*32 active-delivery
/// allocation. Calls are intended to be serialized by the owning Mesh execution domain.
/// </remarks>
template<
    std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
    std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission
>
class ApplicationTransmissionCoordinator final {
    struct ReservationBinding final {
        bool Used{false};
        std::uint16_t TransmissionGeneration{0};
        MeshTrafficReservation Reservation{};
    };

    ApplicationTransmissionTable<TransmissionCapacity, RecipientCapacity>& _transmissions;
    IMeshTrafficGovernor& _traffic;
    std::array<ReservationBinding, TransmissionCapacity> _reservations{};

    ReservationBinding* ResolveBinding(ApplicationTransmissionHandle handle) noexcept {
        if (!handle || handle.Slot >= TransmissionCapacity) return nullptr;
        auto& binding = _reservations[handle.Slot];
        return binding.Used && binding.TransmissionGeneration == handle.Generation ? &binding : nullptr;
    }

    void ReleaseTrafficIfTerminal(ApplicationTransmissionHandle handle) noexcept {
        if (!_transmissions.IsTerminal(handle)) return;
        auto* binding = ResolveBinding(handle);
        if (binding == nullptr) return;
        (void)_traffic.Release(binding->Reservation);
        *binding = {};
    }

    static ApplicationTransmissionAdmissionResult Map(ApplicationTransmissionBeginResult result) noexcept {
        switch (result) {
            case ApplicationTransmissionBeginResult::Begun: return ApplicationTransmissionAdmissionResult::Begun;
            case ApplicationTransmissionBeginResult::ResourceUnavailable: return ApplicationTransmissionAdmissionResult::ResourceUnavailable;
            case ApplicationTransmissionBeginResult::DeadlineExpired: return ApplicationTransmissionAdmissionResult::DeadlineExpired;
            case ApplicationTransmissionBeginResult::DuplicateRecipient: return ApplicationTransmissionAdmissionResult::DuplicateRecipient;
            case ApplicationTransmissionBeginResult::DuplicateMessageId: return ApplicationTransmissionAdmissionResult::DuplicateMessageId;
            case ApplicationTransmissionBeginResult::Invalid: return ApplicationTransmissionAdmissionResult::Invalid;
        }
        return ApplicationTransmissionAdmissionResult::Invalid;
    }

public:
    ApplicationTransmissionCoordinator(
        ApplicationTransmissionTable<TransmissionCapacity, RecipientCapacity>& transmissions,
        IMeshTrafficGovernor& traffic
    ) noexcept : _transmissions(transmissions), _traffic(traffic) {}

    ApplicationTransmissionAdmissionResult Begin(
        const ApplicationTransmissionRecipient* recipients,
        std::size_t recipientCount,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds,
        ApplicationTransmissionHandle& handle
    ) noexcept {
        handle = {};
        MeshTrafficReservation reservation{};
        const auto admission = _traffic.TryAcquire(MeshTrafficClass::Application, reservation);
        if (admission == MeshTrafficAdmissionResult::ResourceUnavailable) {
            return ApplicationTransmissionAdmissionResult::ResourceUnavailable;
        }
        if (admission != MeshTrafficAdmissionResult::Admitted || !reservation) {
            return ApplicationTransmissionAdmissionResult::Invalid;
        }

        const auto begun = _transmissions.Begin(
            recipients, recipientCount, nowMilliseconds, absoluteDeadlineMilliseconds, handle
        );
        if (begun != ApplicationTransmissionBeginResult::Begun) {
            (void)_traffic.Release(reservation);
            handle = {};
            return Map(begun);
        }

        if (handle.Slot >= TransmissionCapacity) {
            (void)_transmissions.Release(handle);
            (void)_traffic.Release(reservation);
            handle = {};
            return ApplicationTransmissionAdmissionResult::Invalid;
        }
        _reservations[handle.Slot] = {true, handle.Generation, reservation};
        return ApplicationTransmissionAdmissionResult::Begun;
    }

    /// <summary>Begins one recipient's independent delivery using the aggregate's frozen identity, MessageId and deadline.</summary>
    template<std::size_t AcknowledgementCapacity>
    ApplicationRecipientBeginResult BeginRecipient(
        ApplicationTransmissionHandle handle,
        std::size_t recipientIndex,
        std::uint64_t nowMilliseconds,
        bool requireDestinationAcknowledgement,
        OutboundDeliveryLifecycle<AcknowledgementCapacity>& delivery
    ) noexcept {
        if (!_transmissions.Contains(handle)) return ApplicationRecipientBeginResult::UnknownTransmission;
        ApplicationTransmissionRecipient recipient{};
        ApplicationRecipientOutcome outcome{};
        if (!_transmissions.TryGetRecipient(handle, recipientIndex, recipient, outcome)) {
            return ApplicationRecipientBeginResult::UnknownRecipient;
        }
        if (outcome != ApplicationRecipientOutcome::Pending) return ApplicationRecipientBeginResult::AlreadyTerminal;

        const auto result = delivery.Begin(
            recipient.Device,
            recipient.Incarnation,
            recipient.MessageId,
            nowMilliseconds,
            _transmissions.AbsoluteDeadlineMilliseconds(handle),
            requireDestinationAcknowledgement
        );
        switch (result) {
            case OutboundDeliveryBeginResult::Begun: return ApplicationRecipientBeginResult::Begun;
            case OutboundDeliveryBeginResult::AlreadyActive: return ApplicationRecipientBeginResult::Invalid;
            case OutboundDeliveryBeginResult::ResourceUnavailable: return ApplicationRecipientBeginResult::ResourceUnavailable;
            case OutboundDeliveryBeginResult::DeadlineExpired: return ApplicationRecipientBeginResult::DeadlineExpired;
            case OutboundDeliveryBeginResult::Invalid: return ApplicationRecipientBeginResult::Invalid;
        }
        return ApplicationRecipientBeginResult::Invalid;
    }

    ApplicationTransmissionUpdateResult SetRecipientOutcome(
        ApplicationTransmissionHandle handle,
        MeshMessageId messageId,
        ApplicationRecipientOutcome outcome
    ) noexcept {
        const auto result = _transmissions.SetOutcome(handle, messageId, outcome);
        if (result == ApplicationTransmissionUpdateResult::Updated) ReleaseTrafficIfTerminal(handle);
        return result;
    }

    /// <summary>Expires only pending recipients and releases Application traffic capacity when the aggregate becomes terminal.</summary>
    bool Expire(ApplicationTransmissionHandle handle, std::uint64_t nowMilliseconds) noexcept {
        const bool expired = _transmissions.Expire(handle, nowMilliseconds);
        if (expired) ReleaseTrafficIfTerminal(handle);
        return expired;
    }

    /// <summary>Releases aggregate result storage and any still-held Application reservation.</summary>
    bool Release(ApplicationTransmissionHandle handle) noexcept {
        if (!_transmissions.Contains(handle)) return false;
        if (auto* binding = ResolveBinding(handle); binding != nullptr) {
            (void)_traffic.Release(binding->Reservation);
            *binding = {};
        }
        return _transmissions.Release(handle);
    }
};

} // namespace ESPressio::Mesh
