#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Identity of one outbound Node delivery awaiting destination delivery acknowledgement.</summary>
/// <remarks>
/// MeshMessageId is scoped to the authenticated local source incarnation. The destination identity/incarnation is
/// retained as well so an acknowledgement from a later participation incarnation cannot complete older delivery work.
/// This is sender-local bookkeeping only and is not a wire envelope.
/// </remarks>
struct PendingDeliveryAcknowledgementIdentity final {
    System::DeviceIdentifier Destination{};
    MembershipIncarnation DestinationIncarnation{};
    MeshMessageId MessageId{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Destination) && static_cast<bool>(DestinationIncarnation) && MessageId != 0U;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }

    constexpr bool operator==(const PendingDeliveryAcknowledgementIdentity& other) const noexcept {
        return Destination == other.Destination &&
               DestinationIncarnation == other.DestinationIncarnation &&
               MessageId == other.MessageId;
    }
};

/// <summary>One sender-local pending destination acknowledgement record.</summary>
struct PendingDeliveryAcknowledgement final {
    PendingDeliveryAcknowledgementIdentity Identity{};
    std::uint64_t AbsoluteDeadlineMilliseconds{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Identity) && AbsoluteDeadlineMilliseconds != 0U;
    }
};

/// <summary>Result of reserving finite sender-local acknowledgement tracking capacity.</summary>
enum class DeliveryAcknowledgementReserveResult : std::uint8_t {
    Reserved,
    AlreadyPending,
    ResourceUnavailable,
    DeadlineExpired,
    Invalid
};

/// <summary>Result of applying one already-authenticated destination delivery acknowledgement.</summary>
enum class DeliveryAcknowledgementApplyResult : std::uint8_t {
    Acknowledged,
    NotPending,
    DeadlineExpired,
    Invalid
};

/// <summary>
/// Fixed-capacity sender-local tracker for Node deliveries awaiting destination delivery acknowledgement.
/// </summary>
/// <remarks>
/// This type deliberately defines no acknowledgement wire schema and allocates no Mesh PrimitiveFamilyId. The caller
/// must first authenticate the incoming acknowledgement and establish the exact source DeviceIdentifier +
/// MembershipIncarnation before calling AcknowledgeAuthenticated(). A matching acknowledgement confirms only delivery
/// of the Mesh message to the destination framework; it never represents requested-operation completion.
///
/// Capacity is intentionally an explicit template argument: architecture freezes application aggregate and recipient
/// bounds but does not yet freeze one global pending-ACK cardinality across application/control/selective-multicast work.
/// Forcing the composition root to choose a finite capacity avoids silently inventing that memory-policy decision.
///
/// Mutation is intended for the serialized Mesh execution domain. The tracker owns no payload, route, retry state,
/// scheduler, Radio work or authentication context.
/// </remarks>
template<std::size_t Capacity>
class DeliveryAcknowledgementTracker final {
    static_assert(Capacity > 0U, "Delivery acknowledgement capacity must be non-zero.");

    struct Slot final {
        PendingDeliveryAcknowledgement Record{};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

    static bool IsExpired(const PendingDeliveryAcknowledgement& record, std::uint64_t nowMilliseconds) noexcept {
        return record.AbsoluteDeadlineMilliseconds == 0U || nowMilliseconds >= record.AbsoluteDeadlineMilliseconds;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>Finds one exact pending destination acknowledgement without changing its lifetime.</summary>
    const PendingDeliveryAcknowledgement* Find(
        const PendingDeliveryAcknowledgementIdentity& identity
    ) const noexcept {
        if (!identity) return nullptr;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Record.Identity == identity) return &slot.Record;
        }
        return nullptr;
    }

    /// <summary>
    /// Reserves one finite acknowledgement record using the delivery's already-established immutable absolute deadline.
    /// </summary>
    DeliveryAcknowledgementReserveResult Reserve(
        const PendingDeliveryAcknowledgementIdentity& identity,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) noexcept {
        if (!identity || absoluteDeadlineMilliseconds == 0U) {
            return DeliveryAcknowledgementReserveResult::Invalid;
        }
        if (nowMilliseconds >= absoluteDeadlineMilliseconds) {
            return DeliveryAcknowledgementReserveResult::DeadlineExpired;
        }

        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Record.Identity == identity) {
                return DeliveryAcknowledgementReserveResult::AlreadyPending;
            }
        }

        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Record = PendingDeliveryAcknowledgement{identity, absoluteDeadlineMilliseconds};
            slot.Occupied = true;
            ++_size;
            return DeliveryAcknowledgementReserveResult::Reserved;
        }
        return DeliveryAcknowledgementReserveResult::ResourceUnavailable;
    }

    /// <summary>
    /// Applies an acknowledgement whose source identity/incarnation has already been authenticated by the caller.
    /// </summary>
    /// <remarks>
    /// Acknowledgement after the immutable delivery deadline is not accepted as completion. The expired record is
    /// released immediately so stale acknowledgements cannot retain sender-local capacity.
    /// </remarks>
    DeliveryAcknowledgementApplyResult AcknowledgeAuthenticated(
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acknowledgedMessageId,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (!authenticatedSource || !authenticatedSourceIncarnation || acknowledgedMessageId == 0U) {
            return DeliveryAcknowledgementApplyResult::Invalid;
        }

        const PendingDeliveryAcknowledgementIdentity identity{
            authenticatedSource,
            authenticatedSourceIncarnation,
            acknowledgedMessageId
        };

        for (auto& slot : _slots) {
            if (!slot.Occupied || !(slot.Record.Identity == identity)) continue;
            if (IsExpired(slot.Record, nowMilliseconds)) {
                slot = {};
                --_size;
                return DeliveryAcknowledgementApplyResult::DeadlineExpired;
            }
            slot = {};
            --_size;
            return DeliveryAcknowledgementApplyResult::Acknowledged;
        }
        return DeliveryAcknowledgementApplyResult::NotPending;
    }

    /// <summary>Releases an exact pending record after cancellation or definitive delivery failure.</summary>
    bool Release(const PendingDeliveryAcknowledgementIdentity& identity) noexcept {
        if (!identity) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied || !(slot.Record.Identity == identity)) continue;
            slot = {};
            --_size;
            return true;
        }
        return false;
    }

    /// <summary>Purges records whose immutable delivery deadlines have elapsed.</summary>
    std::size_t PurgeExpired(std::uint64_t nowMilliseconds) noexcept {
        std::size_t removed = 0U;
        for (auto& slot : _slots) {
            if (!slot.Occupied || !IsExpired(slot.Record, nowMilliseconds)) continue;
            slot = {};
            --_size;
            ++removed;
        }
        return removed;
    }

    /// <summary>Clears all sender-local acknowledgement state during controlled Mesh reset.</summary>
    void Clear() noexcept {
        _slots = {};
        _size = 0U;
    }
};

} // namespace ESPressio::Mesh
