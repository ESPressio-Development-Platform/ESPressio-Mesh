#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Bounded authenticated profile data used by Mesh-local destination resolution.</summary>
/// <remarks>
/// GroupIdentifier values are opaque, stable and scoped by the MeshIdentifier of the containing composition. A display
/// name is deliberately not a Group identity input. Group declarations are sorted canonically so authenticated profile
/// equivalence is independent of declaration order. Capability bits remain application/platform-defined semantics.
/// </remarks>
class MeshNodeProfile final {
    CanonicalName _name{};
    MeshNodeAlias _alias{0U};
    CapabilityMask _capabilities{0U};
    ProfileGeneration _generation{0U};
    std::array<GroupIdentifier, Limits::MaxGroupsPerNode> _groups{};
    std::uint8_t _groupCount{0U};

public:
    static bool TryCreate(
        const CanonicalName& name,
        MeshNodeAlias alias,
        CapabilityMask capabilities,
        ProfileGeneration generation,
        const GroupIdentifier* groups,
        std::size_t groupCount,
        MeshNodeProfile& result
    ) noexcept {
        result = {};
        if (!name || alias == 0U || generation == 0U ||
            groupCount > Limits::MaxGroupsPerNode || (groups == nullptr && groupCount != 0U)) return false;
        MeshNodeProfile candidate;
        candidate._name = name;
        candidate._alias = alias;
        candidate._capabilities = capabilities;
        candidate._generation = generation;
        for (std::size_t source = 0U; source < groupCount; ++source) {
            if (!groups[source]) return false;
            std::size_t insertion = candidate._groupCount;
            while (insertion > 0U && groups[source] < candidate._groups[insertion - 1U]) {
                candidate._groups[insertion] = candidate._groups[insertion - 1U];
                --insertion;
            }
            if ((insertion > 0U && candidate._groups[insertion - 1U] == groups[source]) ||
                (insertion < candidate._groupCount && candidate._groups[insertion] == groups[source])) return false;
            candidate._groups[insertion] = groups[source];
            ++candidate._groupCount;
        }
        result = candidate;
        return true;
    }

    constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(_name) && _alias != 0U && _generation != 0U;
    }
    constexpr const CanonicalName& Name() const noexcept { return _name; }
    constexpr MeshNodeAlias Alias() const noexcept { return _alias; }
    constexpr CapabilityMask Capabilities() const noexcept { return _capabilities; }
    constexpr ProfileGeneration Generation() const noexcept { return _generation; }
    constexpr std::size_t GroupCount() const noexcept { return _groupCount; }
    constexpr const GroupIdentifier* GroupAt(std::size_t index) const noexcept {
        return index < _groupCount ? &_groups[index] : nullptr;
    }
    constexpr bool HasGroup(const GroupIdentifier& group) const noexcept {
        if (!group) return false;
        for (std::size_t index = 0U; index < _groupCount; ++index) {
            if (_groups[index] == group) return true;
            if (group < _groups[index]) return false;
        }
        return false;
    }
    constexpr bool SupportsAll(CapabilityMask required) const noexcept {
        return required != 0U && (_capabilities & required) == required;
    }
    bool operator==(const MeshNodeProfile& other) const noexcept {
        if (_name != other._name || _alias != other._alias || _capabilities != other._capabilities ||
            _generation != other._generation || _groupCount != other._groupCount) return false;
        for (std::size_t index = 0U; index < _groupCount; ++index) {
            if (_groups[index] != other._groups[index]) return false;
        }
        return true;
    }
    bool operator!=(const MeshNodeProfile& other) const noexcept { return !(*this == other); }
};

} // namespace ESPressio::Mesh
