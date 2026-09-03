#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Generation-safe reservation for one active liveness probe.</summary>
struct LivenessProbeReservation final {
    std::uint8_t Slot{std::numeric_limits<std::uint8_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint8_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Result of attempting to start one bounded active liveness probe.</summary>
enum class LivenessProbeReservationResult : std::uint8_t {
    Reserved,
    AlreadyInProgress,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed-capacity execution reservations for active liveness probes.
/// </summary>
/// <remarks>
/// Probe scheduling/eligibility remains policy-owned. This table only prevents duplicate concurrent probes to the
/// same authenticated DeviceIdentifier + MembershipIncarnation and enforces the locked finite active-probe bound.
/// It stores no reachability result and cannot change MembershipState or authenticated authority.
/// </remarks>
template<std::size_t Capacity = Limits::MaxActiveLivenessProbes>
class LivenessProbeReservationTable final {
    static_assert(Capacity > 0, "Active liveness probe capacity must be non-zero.");
    static_assert(Capacity < std::numeric_limits<std::uint8_t>::max(),
                  "Liveness probe slots must fit the compact reservation handle.");

    struct Slot final {
        System::DeviceIdentifier Device{};
        MembershipIncarnation Incarnation{};
        std::uint16_t Generation{0};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

    static std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        ++current;
        if (current == 0U) ++current;
        return current;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }

    LivenessProbeReservationResult TryReserve(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        LivenessProbeReservation& reservation
    ) noexcept {
        reservation = {};
        if (!device || !incarnation) return LivenessProbeReservationResult::Invalid;

        for (std::size_t index = 0; index < Capacity; ++index) {
            const auto& slot = _slots[index];
            if (slot.Occupied && slot.Device == device && slot.Incarnation == incarnation) {
                reservation = LivenessProbeReservation{static_cast<std::uint8_t>(index), slot.Generation};
                return LivenessProbeReservationResult::AlreadyInProgress;
            }
        }

        for (std::size_t index = 0; index < Capacity; ++index) {
            auto& slot = _slots[index];
            if (slot.Occupied) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Device = device;
            slot.Incarnation = incarnation;
            slot.Occupied = true;
            ++_size;
            reservation = LivenessProbeReservation{static_cast<std::uint8_t>(index), slot.Generation};
            return LivenessProbeReservationResult::Reserved;
        }

        return LivenessProbeReservationResult::ResourceUnavailable;
    }

    /// <summary>Releases one exact current probe reservation.</summary>
    bool Release(LivenessProbeReservation reservation) noexcept {
        if (!reservation || reservation.Slot >= Capacity) return false;
        auto& slot = _slots[reservation.Slot];
        if (!slot.Occupied || slot.Generation != reservation.Generation) return false;
        slot.Device = {};
        slot.Incarnation = {};
        slot.Occupied = false;
        --_size;
        return true;
    }

    /// <summary>Returns whether an exact authenticated incarnation already has active probe work.</summary>
    bool Contains(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        if (!device || !incarnation) return false;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Device == device && slot.Incarnation == incarnation) return true;
        }
        return false;
    }
};

} // namespace ESPressio::Mesh
