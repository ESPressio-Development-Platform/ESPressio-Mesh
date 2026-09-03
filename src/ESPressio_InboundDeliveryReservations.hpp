#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Identity of one authenticated ordinary Mesh delivery while semantic handoff is in progress.</summary>
struct InboundDeliveryIdentity final {
    System::DeviceIdentifier Source{};
    MembershipIncarnation Incarnation{};
    MeshMessageId MessageId{0};

    /// <summary>Indicates whether every semantic identity component is valid and non-zero.</summary>
    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Source) &&
               static_cast<bool>(Incarnation) &&
               MessageId != 0U;
    }

    /// <summary>Compares complete source/incarnation/transmission identity.</summary>
    constexpr bool operator==(const InboundDeliveryIdentity& other) const noexcept {
        return Source == other.Source &&
               Incarnation == other.Incarnation &&
               MessageId == other.MessageId;
    }

    /// <summary>Compares complete source/incarnation/transmission identity for inequality.</summary>
    constexpr bool operator!=(const InboundDeliveryIdentity& other) const noexcept {
        return !(*this == other);
    }
};

/// <summary>Immediate result of attempting to reserve one authenticated inbound delivery for semantic handoff.</summary>
enum class InboundDeliveryReservationResult : std::uint8_t {
    /// <summary>The delivery now exclusively owns one bounded InProgress reservation.</summary>
    Reserved,
    /// <summary>An equivalent authenticated delivery copy is already being processed.</summary>
    AlreadyInProgress,
    /// <summary>No bounded inbound-delivery slot is currently available.</summary>
    ResourceUnavailable,
    /// <summary>The supplied source/incarnation/message identity is invalid.</summary>
    Invalid
};

/// <summary>
/// Fixed-capacity InProgress reservation table preventing concurrent duplicate semantic delivery.
/// </summary>
/// <remarks>
/// This table is entered only after source authentication and MembershipIncarnation validation and after
/// committed deduplication classifies the MeshMessageId as unseen. Reserving before family dispatch prevents
/// copies arriving over different Radios/routes from both reaching the upper primitive receiver. A definitive
/// receive disposition is followed by committed deduplication and then Release; TemporarilyUnavailable or
/// ResourceUnavailable releases the reservation without committing deduplication so a later retry may proceed.
/// The default Mesh execution model serializes mutation of this table; the table deliberately owns no mutex or
/// task because execution-domain synchronization is a separate responsibility.
/// </remarks>
template<std::size_t Capacity = Limits::MaxActiveInboundDeliveries>
class InboundDeliveryReservationTable final {
    static_assert(Capacity > 0, "Inbound delivery reservation capacity must be non-zero.");

    struct Slot final {
        InboundDeliveryIdentity Identity{};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

public:
    /// <summary>Returns the fixed compile-time reservation capacity.</summary>
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }

    /// <summary>Returns the number of deliveries currently reserved as InProgress.</summary>
    constexpr std::size_t Size() const noexcept { return _size; }

    /// <summary>Returns whether no delivery is currently reserved.</summary>
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>Returns whether an exact authenticated delivery identity is already reserved.</summary>
    bool Contains(const InboundDeliveryIdentity& identity) const noexcept {
        if (!identity.IsValid()) return false;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Identity == identity) return true;
        }
        return false;
    }

    /// <summary>
    /// Attempts to atomically claim one logical reservation within the serialized Mesh execution domain.
    /// </summary>
    InboundDeliveryReservationResult TryReserve(
        const InboundDeliveryIdentity& identity
    ) noexcept {
        if (!identity.IsValid()) return InboundDeliveryReservationResult::Invalid;

        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Identity == identity) {
                return InboundDeliveryReservationResult::AlreadyInProgress;
            }
        }

        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Identity = identity;
            slot.Occupied = true;
            ++_size;
            return InboundDeliveryReservationResult::Reserved;
        }

        return InboundDeliveryReservationResult::ResourceUnavailable;
    }

    /// <summary>
    /// Releases one exact InProgress reservation after the caller has either committed or abandoned delivery.
    /// </summary>
    bool Release(const InboundDeliveryIdentity& identity) noexcept {
        if (!identity.IsValid()) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Identity != identity) continue;
            slot = {};
            --_size;
            return true;
        }
        return false;
    }

    /// <summary>Clears all InProgress reservations during controlled Mesh shutdown/reset.</summary>
    void Clear() noexcept {
        _slots = {};
        _size = 0U;
    }
};

} // namespace ESPressio::Mesh
