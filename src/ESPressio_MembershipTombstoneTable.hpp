#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Compact historical evidence for one previously represented Mesh membership incarnation.</summary>
struct MembershipTombstone final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    std::uint64_t RetentionDeadlineMilliseconds{0};
    MembershipTombstoneDisposition Disposition{
        MembershipTombstoneDisposition::LocallyForgotten
    };

    /// <summary>Indicates whether this entry contains a valid identity pair and finite retention deadline.</summary>
    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Device) &&
               static_cast<bool>(Incarnation) &&
               RetentionDeadlineMilliseconds != 0U;
    }
};

/// <summary>
/// Fixed-capacity local membership tombstone table used to retain compact incarnation history.
/// </summary>
/// <remarks>
/// Tombstones are independent of active-member capacity and never retain profiles, topology, routes,
/// Radio addresses, payloads or deduplication windows. Saturation uses deterministic eviction and can
/// only discard continuity history; it never weakens authentication or admission requirements.
/// Time values are supplied by the caller from a monotonic clock.
/// </remarks>
template<std::size_t Capacity = Limits::MaxMembershipTombstones>
class MembershipTombstoneTable final {
    static_assert(Capacity > 0, "Membership tombstone capacity must be non-zero.");

    struct Slot final {
        MembershipTombstone Value{};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

    static constexpr std::uint8_t EvictionRank(
        MembershipTombstoneDisposition disposition
    ) noexcept {
        switch (disposition) {
            case MembershipTombstoneDisposition::LocallyForgotten: return 0;
            case MembershipTombstoneDisposition::SupersededIncarnation: return 1;
            case MembershipTombstoneDisposition::AuthoritativeLeave: return 2;
        }
        return 3;
    }

    static bool StableLess(
        const MembershipTombstone& lhs,
        const MembershipTombstone& rhs
    ) noexcept {
        const auto lhsRank = EvictionRank(lhs.Disposition);
        const auto rhsRank = EvictionRank(rhs.Disposition);
        if (lhsRank != rhsRank) return lhsRank < rhsRank;
        if (lhs.RetentionDeadlineMilliseconds != rhs.RetentionDeadlineMilliseconds) {
            return lhs.RetentionDeadlineMilliseconds < rhs.RetentionDeadlineMilliseconds;
        }
        if (lhs.Device != rhs.Device) return lhs.Device < rhs.Device;
        return lhs.Incarnation < rhs.Incarnation;
    }

    static std::uint64_t SaturatingDeadline(
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds
    ) noexcept {
        if (retentionMilliseconds == 0U) return 0U;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        if (nowMilliseconds > maximum - retentionMilliseconds) return maximum;
        return nowMilliseconds + retentionMilliseconds;
    }

public:
    /// <summary>Returns the fixed compile-time capacity of the table.</summary>
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }

    /// <summary>Returns the number of currently retained tombstones.</summary>
    constexpr std::size_t Size() const noexcept { return _size; }

    /// <summary>Returns whether no tombstones are currently retained.</summary>
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>Removes every tombstone whose local monotonic retention deadline has elapsed.</summary>
    std::size_t PurgeExpired(std::uint64_t nowMilliseconds) noexcept {
        std::size_t removed = 0;
        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Value.RetentionDeadlineMilliseconds > nowMilliseconds) continue;
            slot = {};
            --_size;
            ++removed;
        }
        return removed;
    }

    /// <summary>Finds an exact DeviceIdentifier + MembershipIncarnation tombstone.</summary>
    const MembershipTombstone* Find(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        PurgeExpired(nowMilliseconds);
        for (const auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Value.Device == device && slot.Value.Incarnation == incarnation) {
                return &slot.Value;
            }
        }
        return nullptr;
    }

    /// <summary>Finds an exact tombstone from a const table without mutating expired slots.</summary>
    const MembershipTombstone* FindRetained(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        for (const auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Value.Device == device && slot.Value.Incarnation == incarnation) {
                return &slot.Value;
            }
        }
        return nullptr;
    }

    /// <summary>
    /// Records or refreshes compact local history for one membership incarnation.
    /// </summary>
    /// <param name="device">Permanent transport-independent device identity.</param>
    /// <param name="incarnation">Membership incarnation represented by the tombstone.</param>
    /// <param name="disposition">Reason the compact history is being retained.</param>
    /// <param name="nowMilliseconds">Current local monotonic time.</param>
    /// <param name="retentionMilliseconds">Requested finite retention duration.</param>
    /// <returns><c>false</c> only when identity or retention arguments are invalid.</returns>
    bool Record(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        MembershipTombstoneDisposition disposition,
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        if (!device || !incarnation || retentionMilliseconds == 0U) return false;

        const auto deadline = SaturatingDeadline(nowMilliseconds, retentionMilliseconds);
        if (deadline == 0U) return false;

        PurgeExpired(nowMilliseconds);

        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Value.Device == device && slot.Value.Incarnation == incarnation) {
                slot.Value.RetentionDeadlineMilliseconds = deadline;
                slot.Value.Disposition = disposition;
                return true;
            }
        }

        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Occupied = true;
            slot.Value = MembershipTombstone{
                device,
                incarnation,
                deadline,
                disposition
            };
            ++_size;
            return true;
        }

        std::size_t victim = 0;
        for (std::size_t index = 1; index < Capacity; ++index) {
            if (StableLess(_slots[index].Value, _slots[victim].Value)) {
                victim = index;
            }
        }

        _slots[victim].Value = MembershipTombstone{
            device,
            incarnation,
            deadline,
            disposition
        };
        return true;
    }

    /// <summary>Removes one exact retained tombstone if present.</summary>
    bool Remove(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Value.Device == device && slot.Value.Incarnation == incarnation) {
                slot = {};
                --_size;
                return true;
            }
        }
        return false;
    }

    /// <summary>Removes all retained tombstones.</summary>
    void Clear() noexcept {
        _slots = {};
        _size = 0;
    }
};

} // namespace ESPressio::Mesh
