#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ESPressio::Mesh {

/// <summary>Generation-safe handle to one allocation in a fixed owned-byte pool.</summary>
struct OwnedBytePoolHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

struct MutableOwnedByteView final {
    std::uint8_t* Data{nullptr};
    std::size_t Size{0};
    constexpr explicit operator bool() const noexcept { return Data != nullptr && Size != 0U; }
};

struct OwnedByteView final {
    const std::uint8_t* Data{nullptr};
    std::size_t Size{0};
    constexpr explicit operator bool() const noexcept { return Data != nullptr && Size != 0U; }
};

/// <summary>Fixed-cardinality, fixed-byte owned storage with no heap fallback.</summary>
/// <remarks>
/// The pool is suitable for composition-owned inbound packets, control frames or bounded application payload backing.
/// Allocation failure is explicit. Release and controlled reset clear retained bytes and make every old handle stale.
/// </remarks>
template<std::size_t SlotCapacity, std::size_t BytesPerSlot>
class BoundedOwnedBytePool final {
    static_assert(SlotCapacity > 0U, "Owned-byte slot capacity must be non-zero.");
    static_assert(SlotCapacity < std::numeric_limits<std::uint16_t>::max(),
                  "Owned-byte slot capacity must fit the handle slot.");
    static_assert(BytesPerSlot > 0U, "Owned bytes per slot must be non-zero.");

    struct Slot final {
        std::array<std::uint8_t, BytesPerSlot> Bytes{};
        std::size_t Size{0};
        std::uint16_t Generation{0};
        bool Used{false};
    };

    std::array<Slot, SlotCapacity> _slots{};

    static constexpr std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        const auto next = static_cast<std::uint16_t>(current + 1U);
        return next == 0U ? 1U : next;
    }

    Slot* ResolveSlot(OwnedBytePoolHandle handle) noexcept {
        if (!handle || handle.Slot >= SlotCapacity) return nullptr;
        auto& slot = _slots[handle.Slot];
        return slot.Used && slot.Generation == handle.Generation ? &slot : nullptr;
    }

    const Slot* ResolveSlot(OwnedBytePoolHandle handle) const noexcept {
        if (!handle || handle.Slot >= SlotCapacity) return nullptr;
        const auto& slot = _slots[handle.Slot];
        return slot.Used && slot.Generation == handle.Generation ? &slot : nullptr;
    }

    static void Clear(Slot& slot) noexcept {
        volatile std::uint8_t* bytes = slot.Bytes.data();
        for (std::size_t index = 0; index < slot.Bytes.size(); ++index) bytes[index] = 0U;
        slot.Size = 0U;
        slot.Used = false;
    }

public:
    static constexpr std::size_t MaximumSlots = SlotCapacity;
    static constexpr std::size_t MaximumBytesPerSlot = BytesPerSlot;
    static constexpr std::size_t PayloadCapacityBytes = SlotCapacity * BytesPerSlot;

    bool Acquire(std::size_t size, OwnedBytePoolHandle& handle, MutableOwnedByteView& view) noexcept {
        handle = {};
        view = {};
        if (size == 0U || size > BytesPerSlot) return false;
        for (std::size_t index = 0; index < SlotCapacity; ++index) {
            auto& slot = _slots[index];
            if (slot.Used) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Size = size;
            slot.Used = true;
            handle = {static_cast<std::uint16_t>(index), slot.Generation};
            view = {slot.Bytes.data(), size};
            return true;
        }
        return false;
    }

    bool Store(const std::uint8_t* bytes, std::size_t size, OwnedBytePoolHandle& handle) noexcept {
        MutableOwnedByteView destination{};
        if ((bytes == nullptr && size != 0U) || !Acquire(size, handle, destination)) return false;
        std::memcpy(destination.Data, bytes, size);
        return true;
    }

    OwnedByteView Resolve(OwnedBytePoolHandle handle) const noexcept {
        const auto* slot = ResolveSlot(handle);
        return slot == nullptr ? OwnedByteView{} : OwnedByteView{slot->Bytes.data(), slot->Size};
    }

    bool Release(OwnedBytePoolHandle handle) noexcept {
        auto* slot = ResolveSlot(handle);
        if (slot == nullptr) return false;
        Clear(*slot);
        return true;
    }

    void ResetForControlledShutdown() noexcept {
        for (auto& slot : _slots) if (slot.Used) Clear(slot);
    }
};

} // namespace ESPressio::Mesh
