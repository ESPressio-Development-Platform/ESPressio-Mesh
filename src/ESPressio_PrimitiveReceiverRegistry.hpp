#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>
#include <ESPressio_PrimitiveFamilyRegistry.hpp>
#include <ESPressio_PrimitiveTypes.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Borrowed immutable primitive-family payload delivered synchronously at the Mesh receiver boundary.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct PrimitivePayloadView final {
    const std::uint8_t* Data{nullptr};
    std::size_t Size{0};

    constexpr bool IsValid() const noexcept { return Data != nullptr || Size == 0U; }
};

/// <summary>Authenticated Mesh-specific provenance accompanying one primitive-family delivery.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshReceiveContext final {
    System::DeviceIdentifier Source{};
    MembershipIncarnation SourceIncarnation{};
    MeshMessageId DeliveryMessageId{0};
    RemainingHopLimit RemainingHops{0};
    bool Broadcast{false};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Source) && static_cast<bool>(SourceIncarnation) && DeliveryMessageId != 0U;
    }
};

/// <summary>Local semantic disposition returned by one primitive-family receiver.</summary>
/// <remarks>
/// These values never redefine Mesh delivery success and never implicitly generate a NACK or reciprocal primitive.
/// Only TemporarilyUnavailable and ResourceUnavailable are retryable by the inbound-delivery coordinator; every
/// other disposition is definitive for duplicate-suppression purposes.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class PrimitiveReceiveDisposition : std::uint8_t {
    Accepted,
    UnsupportedVersion,
    Malformed,
    RejectedByLocalPolicy,
    TemporarilyUnavailable,
    ResourceUnavailable
};

/// <summary>Whether a registered family implementation is advertised in the authenticated NodeProfile.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class PrimitiveReceiverExposure : std::uint8_t {
    Hidden,
    Advertised
};

/// <summary>Bounded semantic support descriptor owned by one registered external primitive receiver.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct PrimitiveReceiverDescriptor final {
    Primitive::PrimitiveFamilyId Family{Primitive::FamilyIds::Invalid};
    Primitive::PrimitiveProtocolVersionRange Versions{};
    Primitive::ContractFingerprint Fingerprint{};
    PrimitiveReceiverExposure Exposure{PrimitiveReceiverExposure::Advertised};

    constexpr bool IsValid() const noexcept {
        return Primitive::FamilyIds::IsUsable(Family) && Versions.IsValid();
    }
};

/// <summary>Receives one short bounded external primitive-family handoff from Mesh.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic interface/object includes vptr storage where not supplied by a base.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class IPrimitiveReceiver {
public:
    virtual ~IPrimitiveReceiver() = default;

    /// <summary>
    /// Validates/accepts one family payload. Heavy work must be transferred into the owning subsystem's bounded execution path.
    /// </summary>
    virtual PrimitiveReceiveDisposition Receive(
        const MeshReceiveContext& context,
        Primitive::PrimitiveProtocolVersion version,
        PrimitivePayloadView payload
    ) noexcept = 0;
};

/// <summary>Generation-safe registration handle for one external primitive-family receiver.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct PrimitiveReceiverHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Result of bounded external primitive receiver registration.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class PrimitiveReceiverRegistrationResult : std::uint8_t {
    Registered,
    FamilyAlreadyRegistered,
    ResourceUnavailable,
    Invalid
};

/// <summary>Result of dispatching one external primitive family at the destination Mesh endpoint.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class PrimitiveDispatchResult : std::uint8_t {
    Dispatched,
    UnsupportedFamily,
    UnsupportedVersion,
    Invalid
};

/// <summary>
/// Fixed-capacity heap-free PrimitiveFamilyId-to-receiver dispatch registry.
/// </summary>
/// <remarks>
/// Exactly one active external receiver may own a family. Registration is deterministic and lifetime-safe via a
/// generation handle. MeshControl is owned internally by Mesh and cannot be registered through this external receiver
/// boundary. Command, Event, State and application/private families may be registered by their owning integration.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
template<std::size_t Capacity = Limits::MaxPrimitiveReceivers>
class PrimitiveReceiverRegistry final {
    static_assert(Capacity > 0, "Primitive receiver capacity must be non-zero.");
    static_assert(Capacity < std::numeric_limits<std::uint16_t>::max(),
                  "Primitive receiver slots must fit the generation-safe handle.");

/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct Slot final {
        PrimitiveReceiverDescriptor Descriptor{};
        IPrimitiveReceiver* Receiver{nullptr};
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

    PrimitiveReceiverRegistrationResult Register(
        const PrimitiveReceiverDescriptor& descriptor,
        IPrimitiveReceiver& receiver,
        PrimitiveReceiverHandle& handle
    ) noexcept {
        handle = {};
        if (!descriptor.IsValid() || descriptor.Family == Primitive::FamilyIds::MeshControl) {
            return PrimitiveReceiverRegistrationResult::Invalid;
        }

        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Descriptor.Family == descriptor.Family) {
                return PrimitiveReceiverRegistrationResult::FamilyAlreadyRegistered;
            }
        }

        for (std::size_t index = 0; index < Capacity; ++index) {
            auto& slot = _slots[index];
            if (slot.Occupied) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Descriptor = descriptor;
            slot.Receiver = &receiver;
            slot.Occupied = true;
            ++_size;
            handle = PrimitiveReceiverHandle{static_cast<std::uint16_t>(index), slot.Generation};
            return PrimitiveReceiverRegistrationResult::Registered;
        }
        return PrimitiveReceiverRegistrationResult::ResourceUnavailable;
    }

    bool Unregister(PrimitiveReceiverHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return false;
        auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return false;
        slot.Descriptor = {};
        slot.Receiver = nullptr;
        slot.Occupied = false;
        --_size;
        return true;
    }

    const PrimitiveReceiverDescriptor* FindDescriptor(
        Primitive::PrimitiveFamilyId family
    ) const noexcept {
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Descriptor.Family == family) return &slot.Descriptor;
        }
        return nullptr;
    }

    /// <summary>Enumerates current receiver support descriptors synchronously without allocating a snapshot.</summary>
    /// <remarks>
    /// Enumeration is bounded by Capacity. The visitor must not structurally mutate the registry while enumeration is
    /// active. This surface allows profile-support composition without exposing receiver implementation pointers.
    /// </remarks>
    template<typename TVisitor>
    void ForEachDescriptor(TVisitor&& visitor) const {
        for (const auto& slot : _slots) {
            if (slot.Occupied) visitor(slot.Descriptor);
        }
    }

    PrimitiveDispatchResult Dispatch(
        Primitive::PrimitiveFamilyId family,
        Primitive::PrimitiveProtocolVersion version,
        const MeshReceiveContext& context,
        PrimitivePayloadView payload,
        PrimitiveReceiveDisposition& disposition
    ) noexcept {
        if (!Primitive::FamilyIds::IsUsable(family) || !context.IsValid() || !payload.IsValid()) {
            return PrimitiveDispatchResult::Invalid;
        }

        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Descriptor.Family != family) continue;
            if (!slot.Descriptor.Versions.Contains(version)) {
                disposition = PrimitiveReceiveDisposition::UnsupportedVersion;
                return PrimitiveDispatchResult::UnsupportedVersion;
            }
            disposition = slot.Receiver->Receive(context, version, payload);
            return PrimitiveDispatchResult::Dispatched;
        }
        return PrimitiveDispatchResult::UnsupportedFamily;
    }
};

} // namespace ESPressio::Mesh
