#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_IRadio.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of registering one Radio interface into the current Mesh membership incarnation.</summary>
enum class MeshRadioRegistrationResult : std::uint8_t {
    Registered,
    AlreadyRegistered,
    ResourceUnavailable,
    IdentifierExhausted,
    Invalid
};

/// <summary>
/// Fixed-capacity mapping from process-local Radio interfaces to incarnation-scoped Mesh RadioIdentifier values.
/// </summary>
/// <remarks>
/// RadioIdentifier is a Mesh-local handle only; it is not a hardware address and does not identify a device.
/// Identifiers are allocated monotonically from 1 and are never recycled during one membership incarnation,
/// even when an interface is removed. ResetForNewIncarnation is the only operation that restarts allocation.
///
/// This registry does not own or start Radio interfaces and does not own RadioPeerHandle state; those remain
/// responsibilities of ESPressio-Radio/RadioTransport. Mutation is intended for the serialized Mesh domain.
/// </remarks>
template<std::size_t Capacity = Limits::MaxRadiosPerNode>
class MeshRadioRegistry final {
    static_assert(Capacity > 0, "Mesh radio capacity must be non-zero.");
    static_assert(Capacity <= 254, "A membership incarnation has at most 254 usable RadioIdentifier values.");

    struct Slot final {
        Radio::IRadio* Interface{nullptr};
        RadioIdentifier Identifier{0};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};
    std::uint16_t _nextIdentifier{1};

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }

    /// <summary>Registers one Radio interface and returns its current-incarnation RadioIdentifier.</summary>
    MeshRadioRegistrationResult Register(
        Radio::IRadio& radio,
        RadioIdentifier& identifier
    ) noexcept {
        identifier = 0;
        if (!radio.LocalAddress().IsValid()) return MeshRadioRegistrationResult::Invalid;

        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Interface == &radio) {
                identifier = slot.Identifier;
                return MeshRadioRegistrationResult::AlreadyRegistered;
            }
        }

        if (_nextIdentifier > 254U) return MeshRadioRegistrationResult::IdentifierExhausted;

        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            const auto allocated = static_cast<RadioIdentifier>(_nextIdentifier++);
            slot.Interface = &radio;
            slot.Identifier = allocated;
            slot.Occupied = true;
            ++_size;
            identifier = allocated;
            return MeshRadioRegistrationResult::Registered;
        }
        return MeshRadioRegistrationResult::ResourceUnavailable;
    }

    /// <summary>Finds the current RadioIdentifier for a registered interface.</summary>
    RadioIdentifier IdentifierOf(const Radio::IRadio& radio) const noexcept {
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Interface == &radio) return slot.Identifier;
        }
        return 0;
    }

    /// <summary>Resolves a current-incarnation RadioIdentifier to its process-local Radio interface.</summary>
    Radio::IRadio* Resolve(RadioIdentifier identifier) noexcept {
        if (identifier == 0U || identifier == 0xFFU) return nullptr;
        for (auto& slot : _slots) {
            if (slot.Occupied && slot.Identifier == identifier) return slot.Interface;
        }
        return nullptr;
    }

    const Radio::IRadio* Resolve(RadioIdentifier identifier) const noexcept {
        if (identifier == 0U || identifier == 0xFFU) return nullptr;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Identifier == identifier) return slot.Interface;
        }
        return nullptr;
    }

    /// <summary>
    /// Removes one interface without making its RadioIdentifier available for reuse in this incarnation.
    /// </summary>
    bool Remove(Radio::IRadio& radio) noexcept {
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Interface != &radio) continue;
            slot.Interface = nullptr;
            slot.Occupied = false;
            --_size;
            return true;
        }
        return false;
    }

    /// <summary>
    /// Clears all interface bindings and restarts RadioIdentifier allocation for a genuinely new membership incarnation.
    /// </summary>
    void ResetForNewIncarnation() noexcept {
        for (auto& slot : _slots) slot = {};
        _size = 0U;
        _nextIdentifier = 1U;
    }
};

} // namespace ESPressio::Mesh
