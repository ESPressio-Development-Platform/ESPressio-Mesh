#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_PrimitiveTypes.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"

namespace ESPressio::Mesh {

/// <summary>Transport-independent semantic support advertised for one primitive family in NodeProfile data.</summary>
struct PrimitiveSupportDescriptor final {
    Primitive::PrimitiveFamilyId Family{Primitive::FamilyIds::Invalid};
    Primitive::PrimitiveProtocolVersionRange Versions{};
    Primitive::ContractFingerprint Fingerprint{};

    constexpr bool IsValid() const noexcept {
        return Primitive::FamilyIds::IsUsable(Family) && Versions.IsValid();
    }
};

/// <summary>
/// Fixed-capacity canonical snapshot of externally advertised primitive-family support.
/// </summary>
/// <remarks>
/// The snapshot copies only semantic profile data from `PrimitiveReceiverRegistry`; receiver pointers, registration
/// handles and Hidden registrations never escape into authenticated profile representation. Entries are sorted by
/// PrimitiveFamilyId so equivalent active receiver sets produce deterministic profile input independent of local
/// registration order. The centrally allocated Mesh Control family remains internal protocol machinery and is not
/// injected into the application-facing support snapshot.
/// </remarks>
template<std::size_t Capacity = Limits::MaxPrimitiveReceivers>
class AdvertisedPrimitiveSupportSnapshot final {
    std::array<PrimitiveSupportDescriptor, Capacity> _entries{};
    std::size_t _size{0};

    void InsertSorted(const PrimitiveSupportDescriptor& descriptor) noexcept {
        if (_size >= Capacity) return;
        std::size_t index = _size;
        while (index > 0 && _entries[index - 1].Family > descriptor.Family) {
            _entries[index] = _entries[index - 1];
            --index;
        }
        _entries[index] = descriptor;
        ++_size;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>Builds a deterministic snapshot from currently Advertised receiver descriptors.</summary>
    static AdvertisedPrimitiveSupportSnapshot Capture(
        const PrimitiveReceiverRegistry<Capacity>& registry
    ) noexcept {
        AdvertisedPrimitiveSupportSnapshot snapshot;
        registry.ForEachDescriptor([&](const PrimitiveReceiverDescriptor& descriptor) {
            if (descriptor.Exposure != PrimitiveReceiverExposure::Advertised) return;
            snapshot.InsertSorted(PrimitiveSupportDescriptor{
                descriptor.Family,
                descriptor.Versions,
                descriptor.Fingerprint
            });
        });
        return snapshot;
    }

    /// <summary>Returns one canonical entry by bounded index, or null when the index is outside the snapshot.</summary>
    const PrimitiveSupportDescriptor* At(std::size_t index) const noexcept {
        return index < _size ? &_entries[index] : nullptr;
    }

    /// <summary>Finds advertised support for one family in the canonical snapshot.</summary>
    const PrimitiveSupportDescriptor* Find(Primitive::PrimitiveFamilyId family) const noexcept {
        for (std::size_t index = 0; index < _size; ++index) {
            if (_entries[index].Family == family) return &_entries[index];
            if (_entries[index].Family > family) break;
        }
        return nullptr;
    }
};

} // namespace ESPressio::Mesh
