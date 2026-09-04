#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>
#include <ESPressio_RadioTypes.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"
#include "ESPressio_TopologySnapshot.hpp"

namespace ESPressio::Mesh {

struct AuthenticatedDirectPeerBinding final {
    System::DeviceIdentifier Neighbour{};
    MembershipIncarnation Incarnation{};
    RadioIdentifier LocalRadio{0};
    Radio::RadioPeerHandle Peer{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Neighbour) && static_cast<bool>(Incarnation) &&
               LocalRadio != 0U && LocalRadio != 0xFFU && static_cast<bool>(Peer);
    }
};

enum class DirectPeerBindingResult : std::uint8_t { Bound, Replaced, ResourceUnavailable, Invalid };

template<std::size_t Capacity = Limits::MaxTopologyLinks>
class AuthenticatedDirectPeerBindingTable final {
    static_assert(Capacity > 0, "Direct peer binding capacity must be non-zero.");
    struct Slot final { AuthenticatedDirectPeerBinding Binding{}; bool Occupied{false}; };
    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }

    DirectPeerBindingResult Bind(const AuthenticatedDirectPeerBinding& binding) noexcept {
        if (!binding.IsValid()) return DirectPeerBindingResult::Invalid;
        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Binding.Neighbour == binding.Neighbour && slot.Binding.LocalRadio == binding.LocalRadio) {
                slot.Binding = binding;
                return DirectPeerBindingResult::Replaced;
            }
        }
        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Binding = binding; slot.Occupied = true; ++_size; return DirectPeerBindingResult::Bound;
        }
        return DirectPeerBindingResult::ResourceUnavailable;
    }

    const AuthenticatedDirectPeerBinding* Resolve(
        RadioIdentifier localRadio,
        const System::DeviceIdentifier& neighbour,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        if (localRadio == 0U || localRadio == 0xFFU || !neighbour || !incarnation) return nullptr;
        for (const auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Binding.LocalRadio == localRadio && slot.Binding.Neighbour == neighbour &&
                slot.Binding.Incarnation == incarnation) return &slot.Binding;
        }
        return nullptr;
    }

    /// <summary>Returns true when at least one executable Radio binding exists for the exact authenticated neighbour incarnation.</summary>
    bool HasNeighbour(
        const System::DeviceIdentifier& neighbour,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        if (!neighbour || !incarnation) return false;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Binding.Neighbour == neighbour && slot.Binding.Incarnation == incarnation &&
                slot.Binding.IsValid()) return true;
        }
        return false;
    }

    const AuthenticatedDirectPeerBinding* ResolveNextHop(
        const TopologyLinkIdentity& nextHop,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& neighbourIncarnation
    ) const noexcept {
        if (!nextHop || !localDevice || nextHop.Advertiser != localDevice) return nullptr;
        return Resolve(nextHop.LocalRadio, nextHop.Neighbour, neighbourIncarnation);
    }

    std::size_t RemoveNeighbour(const System::DeviceIdentifier& neighbour) noexcept {
        if (!neighbour) return 0U;
        std::size_t removed = 0U;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Binding.Neighbour != neighbour) continue;
            slot = {}; --_size; ++removed;
        }
        return removed;
    }

    std::size_t RemoveRadio(RadioIdentifier localRadio) noexcept {
        if (localRadio == 0U || localRadio == 0xFFU) return 0U;
        std::size_t removed = 0U;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Binding.LocalRadio != localRadio) continue;
            slot = {}; --_size; ++removed;
        }
        return removed;
    }

    bool RemovePeer(Radio::RadioPeerHandle peer) noexcept {
        if (!peer) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Binding.Peer != peer) continue;
            slot = {}; --_size; return true;
        }
        return false;
    }

    void Clear() noexcept { for (auto& slot : _slots) slot = {}; _size = 0U; }
};

} // namespace ESPressio::Mesh
