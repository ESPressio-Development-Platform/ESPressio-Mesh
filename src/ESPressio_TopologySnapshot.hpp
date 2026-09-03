#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Local freshness classification for one authenticated remote topology authority.</summary>
enum class TopologyFreshnessState : std::uint8_t {
    Fresh,
    Degraded,
    Stale,
    Expired
};

/// <summary>
/// Identity of one directed Mesh topology edge advertised by its owning member.
/// </summary>
/// <remarks>
/// Direction is explicit: A→B and B→A are different identities and symmetry is never inferred. LocalRadio is
/// mandatory because parallel Radios remain distinct edges. NeighbourRadio may be zero when the neighbour-side
/// RadioIdentifier/link endpoint is not knowable; 0xFF remains reserved and is invalid. No RadioAddress or
/// RadioPeerHandle is retained as distributed topology identity.
/// </remarks>
struct TopologyLinkIdentity final {
    System::DeviceIdentifier Advertiser{};
    RadioIdentifier LocalRadio{0};
    System::DeviceIdentifier Neighbour{};
    RadioIdentifier NeighbourRadio{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Advertiser) &&
               LocalRadio != 0U && LocalRadio != 0xFFU &&
               static_cast<bool>(Neighbour) &&
               Neighbour != Advertiser &&
               NeighbourRadio != 0xFFU;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }

    constexpr bool operator==(const TopologyLinkIdentity& other) const noexcept {
        return Advertiser == other.Advertiser &&
               LocalRadio == other.LocalRadio &&
               Neighbour == other.Neighbour &&
               NeighbourRadio == other.NeighbourRadio;
    }
    constexpr bool operator!=(const TopologyLinkIdentity& other) const noexcept { return !(*this == other); }
};

/// <summary>
/// One directed topology edge plus technology-independent semantic observations chosen by the topology implementation.
/// </summary>
/// <remarks>
/// TCharacteristics deliberately remains a separate bounded value type. The architecture does not define a universal
/// scalar RouteCost: Radio/Mesh observations are normalized independently and interpreted by IRoutingStrategy.
/// </remarks>
template<typename TCharacteristics>
struct DirectedTopologyLink final {
    TopologyLinkIdentity Identity{};
    TCharacteristics Characteristics{};

    constexpr bool operator==(const DirectedTopologyLink& other) const noexcept {
        return Identity == other.Identity && Characteristics == other.Characteristics;
    }
    constexpr bool operator!=(const DirectedTopologyLink& other) const noexcept { return !(*this == other); }
};

/// <summary>Result of applying one complete authenticated outbound-topology generation.</summary>
enum class TopologySnapshotApplyResult : std::uint8_t {
    Applied,
    RefreshedSameGeneration,
    StaleGeneration,
    ConflictingSameGeneration,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed-capacity complete published outbound-topology set owned by one DeviceIdentifier + MembershipIncarnation.
/// </summary>
/// <remarks>
/// TopologyGeneration is scoped to the authority incarnation. A newer complete generation replaces the prior set;
/// removals are represented by absence. Retransmission of an identical generation is accepted as refresh but does not
/// advance semantic truth. Reusing a generation with different semantic links is rejected as conflicting authority.
///
/// Link ordering is not semantic: same-generation equivalence is set-based. The structure owns no freshness policy;
/// local receipt age/freshness is retained separately so authenticated retransmission can refresh freshness without
/// mutating TopologyGeneration.
/// </remarks>
template<typename TCharacteristics, std::size_t Capacity = Limits::MaxTopologyLinks>
class TopologySnapshot final {
    static_assert(Capacity > 0, "Topology link capacity must be non-zero.");

public:
    using Link = DirectedTopologyLink<TCharacteristics>;

private:
    System::DeviceIdentifier _authority{};
    MembershipIncarnation _incarnation{};
    TopologyGeneration _generation{0};
    std::array<Link, Capacity> _links{};
    std::size_t _size{0};

    static bool ContainsDuplicate(const Link* links, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                if (links[i].Identity == links[j].Identity) return true;
            }
        }
        return false;
    }

    bool EquivalentSet(const Link* links, std::size_t count) const noexcept {
        if (count != _size) return false;
        for (std::size_t i = 0; i < count; ++i) {
            bool matched = false;
            for (std::size_t j = 0; j < _size; ++j) {
                if (links[i] == _links[j]) {
                    matched = true;
                    break;
                }
            }
            if (!matched) return false;
        }
        return true;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }
    constexpr const System::DeviceIdentifier& Authority() const noexcept { return _authority; }
    constexpr const MembershipIncarnation& Incarnation() const noexcept { return _incarnation; }
    constexpr TopologyGeneration Generation() const noexcept { return _generation; }

    constexpr const Link* begin() const noexcept { return _links.data(); }
    constexpr const Link* end() const noexcept { return _links.data() + _size; }

    /// <summary>Finds one exact directed edge identity within the current complete generation.</summary>
    const Link* Find(const TopologyLinkIdentity& identity) const noexcept {
        for (std::size_t i = 0; i < _size; ++i) {
            if (_links[i].Identity == identity) return &_links[i];
        }
        return nullptr;
    }

    /// <summary>
    /// Applies a complete authenticated topology generation for one authority/incarnation.
    /// </summary>
    TopologySnapshotApplyResult ApplyComplete(
        const System::DeviceIdentifier& authority,
        const MembershipIncarnation& incarnation,
        TopologyGeneration generation,
        const Link* links,
        std::size_t count
    ) noexcept {
        if (!authority || !incarnation || generation == 0U || (links == nullptr && count != 0U)) {
            return TopologySnapshotApplyResult::Invalid;
        }
        if (count > Capacity) return TopologySnapshotApplyResult::ResourceUnavailable;

        for (std::size_t i = 0; i < count; ++i) {
            if (!links[i].Identity || links[i].Identity.Advertiser != authority) {
                return TopologySnapshotApplyResult::Invalid;
            }
        }
        if (ContainsDuplicate(links, count)) return TopologySnapshotApplyResult::Invalid;

        if (_generation != 0U && _authority == authority && _incarnation == incarnation) {
            if (generation < _generation) return TopologySnapshotApplyResult::StaleGeneration;
            if (generation == _generation) {
                return EquivalentSet(links, count)
                    ? TopologySnapshotApplyResult::RefreshedSameGeneration
                    : TopologySnapshotApplyResult::ConflictingSameGeneration;
            }
        }

        // A different authenticated incarnation begins an independent generation namespace.
        _authority = authority;
        _incarnation = incarnation;
        _generation = generation;
        _size = count;
        for (std::size_t i = 0; i < count; ++i) _links[i] = links[i];
        for (std::size_t i = count; i < Capacity; ++i) _links[i] = {};
        return TopologySnapshotApplyResult::Applied;
    }

    /// <summary>Clears the complete local snapshot; this changes no remote authoritative truth.</summary>
    void Clear() noexcept {
        _authority = {};
        _incarnation = {};
        _generation = 0U;
        _size = 0U;
        for (auto& link : _links) link = {};
    }
};

} // namespace ESPressio::Mesh
