#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_OutboundDeliveryLifecycle.hpp"

namespace ESPressio::Mesh {

enum class ApplicationTransmissionAdmissionResult : std::uint8_t { Begun, ResourceUnavailable, DeadlineExpired, DuplicateRecipient, DuplicateMessageId, Invalid };
enum class ApplicationRecipientBeginResult : std::uint8_t { Begun, AlreadyTerminal, ResourceUnavailable, DeadlineExpired, UnknownRecipient, UnknownTransmission, Invalid };

/// <summary>Composes one Application traffic reservation, one immutable shared payload and one bounded recipient aggregate.</summary>
/// <remarks>
/// One Application reservation represents the accepted aggregate. Recipient deliveries keep independent MeshMessageIds,
/// route-attempt state and destination acknowledgements while all reference the aggregate's one immutable logical payload.
/// The coordinator owns no payload bytes and introduces no hidden 8*32 active-delivery pool.
/// </remarks>
template<std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
         std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission>
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
        const ApplicationPayload& payload,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds,
        ApplicationTransmissionHandle& handle
    ) noexcept {
        handle = {};
        if (!payload) return ApplicationTransmissionAdmissionResult::Invalid;

        MeshTrafficReservation reservation{};
        const auto admission = _traffic.TryAcquire(MeshTrafficClass::Application, reservation);
        if (admission == MeshTrafficAdmissionResult::ResourceUnavailable) {
            return ApplicationTransmissionAdmissionResult::ResourceUnavailable;
        }
        if (admission != MeshTrafficAdmissionResult::Admitted || !reservation) {
            return ApplicationTransmissionAdmissionResult::Invalid;
        }

        const auto begun = _transmissions.Begin(
            recipients, recipientCount, payload, nowMilliseconds, absoluteDeadlineMilliseconds, handle
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

    const ApplicationPayload* Payload(ApplicationTransmissionHandle handle) const noexcept {
        return _transmissions.Payload(handle);
    }

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
        if (result == ApplicationTransmissionUpdateResult::Updated) {
            ReleaseTrafficIfTerminal(handle);
        }
        return result;
    }

    bool Expire(ApplicationTransmissionHandle handle, std::uint64_t nowMilliseconds) noexcept {
        const bool expired = _transmissions.Expire(handle, nowMilliseconds);
        if (expired) ReleaseTrafficIfTerminal(handle);
        return expired;
    }

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
