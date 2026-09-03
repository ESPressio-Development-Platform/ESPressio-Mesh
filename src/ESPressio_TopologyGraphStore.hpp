#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_TopologySnapshot.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Globally bounded authoritative directed-link store spanning all known Mesh topology authorities.
/// </summary>
/// <remarks>
/// MaxTopologyLinks is a graph-wide bound, not a per-member multiplier. Authority metadata is bounded independently
/// by AuthorityCapacity (normally MaxMeshNodes). Each authority owns one current MembershipIncarnation and one complete
/// TopologyGeneration. Applying a newer complete generation replaces only that authority's links; links belonging to
/// other authorities are unaffected. A new authenticated incarnation for the same DeviceIdentifier starts a new
/// generation namespace and replaces the previous incarnation's complete set.
///
/// Freshness/receipt age is intentionally not stored here. It remains independent local metadata so retransmission can
/// refresh freshness without mutating authoritative generation or link semantics.
/// </remarks>
template<
    typename TCharacteristics,
    std::size_t LinkCapacity = Limits::MaxTopologyLinks,
    std::size_t AuthorityCapacity = Limits::MaxMeshNodes
>
class TopologyGraphStore final {
    static_assert(LinkCapacity > 0, "Global topology link capacity must be non-zero.");
    static_assert(AuthorityCapacity > 0, "Topology authority capacity must be non-zero.");

public:
    using Link = DirectedTopologyLink<TCharacteristics>;

    struct AuthorityRecord final {
        System::DeviceIdentifier Device{};
        MembershipIncarnation Incarnation{};
        TopologyGeneration Generation{0};
        std::size_t LinkCount{0};

        constexpr bool IsValid() const noexcept {
            return static_cast<bool>(Device) && static_cast<bool>(Incarnation) && Generation != 0U;
        }
    };

private:
    std::array<Link, LinkCapacity> _links{};
    std::size_t _linkCount{0};
    std::array<AuthorityRecord, AuthorityCapacity> _authorities{};
    std::size_t _authorityCount{0};

    AuthorityRecord* FindAuthorityMutable(const System::DeviceIdentifier& device) noexcept {
        for (auto& record : _authorities) {
            if (record.IsValid() && record.Device == device) return &record;
        }
        return nullptr;
    }

    static bool CandidateHasDuplicate(const Link* links, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                if (links[i].Identity == links[j].Identity) return true;
            }
        }
        return false;
    }

    bool EquivalentAuthoritySet(
        const System::DeviceIdentifier& authority,
        const Link* links,
        std::size_t count
    ) const noexcept {
        std::size_t retained = 0;
        for (std::size_t i = 0; i < _linkCount; ++i) {
            if (_links[i].Identity.Advertiser == authority) ++retained;
        }
        if (retained != count) return false;

        for (std::size_t i = 0; i < count; ++i) {
            bool matched = false;
            for (std::size_t j = 0; j < _linkCount; ++j) {
                if (_links[j].Identity.Advertiser == authority && _links[j] == links[i]) {
                    matched = true;
                    break;
                }
            }
            if (!matched) return false;
        }
        return true;
    }

    void RemoveAuthorityLinks(const System::DeviceIdentifier& authority) noexcept {
        std::size_t write = 0;
        for (std::size_t read = 0; read < _linkCount; ++read) {
            if (_links[read].Identity.Advertiser == authority) continue;
            if (write != read) _links[write] = _links[read];
            ++write;
        }
        for (std::size_t i = write; i < _linkCount; ++i) _links[i] = {};
        _linkCount = write;
    }

public:
    static constexpr std::size_t MaximumLinks() noexcept { return LinkCapacity; }
    static constexpr std::size_t MaximumAuthorities() noexcept { return AuthorityCapacity; }
    constexpr std::size_t LinkCount() const noexcept { return _linkCount; }
    constexpr std::size_t AuthorityCount() const noexcept { return _authorityCount; }

    constexpr const Link* begin() const noexcept { return _links.data(); }
    constexpr const Link* end() const noexcept { return _links.data() + _linkCount; }

    /// <summary>Returns metadata for one currently retained topology authority.</summary>
    const AuthorityRecord* FindAuthority(const System::DeviceIdentifier& device) const noexcept {
        for (const auto& record : _authorities) {
            if (record.IsValid() && record.Device == device) return &record;
        }
        return nullptr;
    }

    /// <summary>Finds one exact directed topology edge across the globally bounded graph.</summary>
    const Link* Find(const TopologyLinkIdentity& identity) const noexcept {
        for (std::size_t i = 0; i < _linkCount; ++i) {
            if (_links[i].Identity == identity) return &_links[i];
        }
        return nullptr;
    }

    /// <summary>
    /// Applies one complete authenticated authority generation while preserving the graph-wide link bound.
    /// </summary>
    /// <remarks>
    /// The supplied link array must not alias this store's internal link range. This avoids a second LinkCapacity-sized
    /// scratch array solely for defensive self-aliasing on constrained targets.
    /// </remarks>
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
        if (count > LinkCapacity) return TopologySnapshotApplyResult::ResourceUnavailable;
        for (std::size_t i = 0; i < count; ++i) {
            if (!links[i].Identity || links[i].Identity.Advertiser != authority) {
                return TopologySnapshotApplyResult::Invalid;
            }
        }
        if (CandidateHasDuplicate(links, count)) return TopologySnapshotApplyResult::Invalid;

        AuthorityRecord* record = FindAuthorityMutable(authority);
        if (record != nullptr && record->Incarnation == incarnation) {
            if (generation < record->Generation) return TopologySnapshotApplyResult::StaleGeneration;
            if (generation == record->Generation) {
                return EquivalentAuthoritySet(authority, links, count)
                    ? TopologySnapshotApplyResult::RefreshedSameGeneration
                    : TopologySnapshotApplyResult::ConflictingSameGeneration;
            }
        }

        const std::size_t previousCount = record == nullptr ? 0U : record->LinkCount;
        if (_linkCount - previousCount + count > LinkCapacity) {
            return TopologySnapshotApplyResult::ResourceUnavailable;
        }

        if (record == nullptr) {
            for (auto& candidate : _authorities) {
                if (candidate.IsValid()) continue;
                record = &candidate;
                ++_authorityCount;
                break;
            }
            if (record == nullptr) return TopologySnapshotApplyResult::ResourceUnavailable;
        }

        RemoveAuthorityLinks(authority);
        for (std::size_t i = 0; i < count; ++i) _links[_linkCount++] = links[i];

        record->Device = authority;
        record->Incarnation = incarnation;
        record->Generation = generation;
        record->LinkCount = count;
        return TopologySnapshotApplyResult::Applied;
    }

    /// <summary>Removes one authority and its directed edges from local graph retention.</summary>
    bool RemoveAuthority(const System::DeviceIdentifier& authority) noexcept {
        auto* record = FindAuthorityMutable(authority);
        if (record == nullptr) return false;
        RemoveAuthorityLinks(authority);
        *record = {};
        --_authorityCount;
        return true;
    }

    /// <summary>Clears all locally retained topology authorities and links.</summary>
    void Clear() noexcept {
        for (auto& link : _links) link = {};
        for (auto& authority : _authorities) authority = {};
        _linkCount = 0U;
        _authorityCount = 0U;
    }
};

} // namespace ESPressio::Mesh
