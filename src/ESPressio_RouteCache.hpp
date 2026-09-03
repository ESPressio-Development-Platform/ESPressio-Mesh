#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_Route.hpp"

namespace ESPressio::Mesh {

/// <summary>Generation-safe local handle identifying one current route-cache entry.</summary>
struct RouteCacheHandle final {
    std::uint16_t Slot{0xFFFFU};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept { return Slot != 0xFFFFU && Generation != 0U; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const RouteCacheHandle& other) const noexcept {
        return Slot == other.Slot && Generation == other.Generation;
    }
    constexpr bool operator!=(const RouteCacheHandle& other) const noexcept { return !(*this == other); }
};

/// <summary>Result of inserting or replacing one local cached route.</summary>
enum class RouteCacheStoreResult : std::uint8_t {
    Stored,
    Replaced,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed-capacity disposable cache of locally planned Mesh routes.
/// </summary>
/// <remarks>
/// Cache contents are never authoritative. A hit is only a planning hint and the caller must revalidate it against
/// current membership, topology freshness, link usability and routing policy before forwarding. This deliberately
/// avoids embedding technology-specific freshness/cost interpretation in the cache itself.
///
/// Replacement is deterministic: an existing source+destination entry is replaced in place; otherwise a free slot is
/// used. The cache does not evict another destination implicitly when full because hidden eviction policy is independently
/// variable behavior. Callers may explicitly invalidate entries or Clear() in response to topology/freshness changes.
/// </remarks>
template<
    std::size_t EntryCapacity = Limits::MaxRouteCacheEntries,
    std::size_t HopCapacity = Limits::MaxRouteHops
>
class RouteCache final {
    static_assert(EntryCapacity > 0, "Route-cache capacity must be non-zero.");
    static_assert(EntryCapacity < 0xFFFFU, "Route-cache slots must fit RouteCacheHandle.");

public:
    using Route = ResolvedRoute<HopCapacity>;

private:
    struct Slot final {
        Route Value{};
        std::uint16_t Generation{0};
        bool Occupied{false};
    };

    std::array<Slot, EntryCapacity> _slots{};
    std::size_t _size{0};

    static std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        ++current;
        if (current == 0U) ++current;
        return current;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return EntryCapacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>Finds a cached route by exact local source and final destination.</summary>
    const Route* Find(
        const System::DeviceIdentifier& source,
        const System::DeviceIdentifier& destination
    ) const noexcept {
        if (!source || !destination) return nullptr;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Value.Source() == source && slot.Value.Destination() == destination) {
                return &slot.Value;
            }
        }
        return nullptr;
    }

    /// <summary>Resolves a generation-safe cache handle to its current route.</summary>
    const Route* Resolve(RouteCacheHandle handle) const noexcept {
        if (!handle || handle.Slot >= EntryCapacity) return nullptr;
        const auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return nullptr;
        return &slot.Value;
    }

    /// <summary>Stores a valid route without silently evicting an unrelated cached destination.</summary>
    RouteCacheStoreResult Store(const Route& route, RouteCacheHandle& handle) noexcept {
        handle = {};
        if (!route.Source() || !route.Destination()) return RouteCacheStoreResult::Invalid;

        for (std::size_t i = 0; i < EntryCapacity; ++i) {
            auto& slot = _slots[i];
            if (!slot.Occupied || slot.Value.Source() != route.Source() ||
                slot.Value.Destination() != route.Destination()) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Value = route;
            handle = RouteCacheHandle{static_cast<std::uint16_t>(i), slot.Generation};
            return RouteCacheStoreResult::Replaced;
        }

        for (std::size_t i = 0; i < EntryCapacity; ++i) {
            auto& slot = _slots[i];
            if (slot.Occupied) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Value = route;
            slot.Occupied = true;
            ++_size;
            handle = RouteCacheHandle{static_cast<std::uint16_t>(i), slot.Generation};
            return RouteCacheStoreResult::Stored;
        }
        return RouteCacheStoreResult::ResourceUnavailable;
    }

    /// <summary>Invalidates one exact cache entry; stale handles cannot affect later slot occupants.</summary>
    bool Invalidate(RouteCacheHandle handle) noexcept {
        if (!handle || handle.Slot >= EntryCapacity) return false;
        auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return false;
        slot.Value.Clear();
        slot.Occupied = false;
        --_size;
        return true;
    }

    /// <summary>Invalidates all cached routes whose source or final destination is the supplied device.</summary>
    std::size_t InvalidateEndpoint(const System::DeviceIdentifier& device) noexcept {
        if (!device) return 0U;
        std::size_t removed = 0U;
        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Value.Source() != device && slot.Value.Destination() != device) continue;
            slot.Value.Clear();
            slot.Occupied = false;
            --_size;
            ++removed;
        }
        return removed;
    }

    /// <summary>
    /// Invalidates every cached route traversing a directed edge advertised by the supplied topology authority.
    /// </summary>
    std::size_t InvalidateAuthority(const System::DeviceIdentifier& authority) noexcept {
        if (!authority) return 0U;
        std::size_t removed = 0U;
        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            bool affected = false;
            for (const auto& hop : slot.Value) {
                if (hop.Advertiser == authority) {
                    affected = true;
                    break;
                }
            }
            if (!affected) continue;
            slot.Value.Clear();
            slot.Occupied = false;
            --_size;
            ++removed;
        }
        return removed;
    }

    /// <summary>Clears every disposable route hint.</summary>
    void Clear() noexcept {
        for (auto& slot : _slots) {
            slot.Value.Clear();
            slot.Occupied = false;
        }
        _size = 0U;
    }
};

} // namespace ESPressio::Mesh
