#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_TopologySnapshot.hpp"

namespace ESPressio::Mesh {

/// <summary>Read-only local evidence supplied when deciding whether an observed complete link set is material enough to publish.</summary>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks>
struct TopologyPublicationEvidence final {
    using Link = DirectedTopologyLink<TCharacteristics>;

    /// <summary>Last complete topology generation actually published by this local authority.</summary>
    const TopologySnapshot<TCharacteristics, LinkCapacity>& Published;
    /// <summary>Candidate complete local outbound-link set produced by observation/normalization.</summary>
    const Link* CandidateLinks{nullptr};
    /// <summary>Number of links in CandidateLinks.</summary>
    std::size_t CandidateCount{0};
    /// <summary>Whether this candidate begins a genuinely new MembershipIncarnation generation namespace.</summary>
    bool NewIncarnation{false};
};

/// <summary>Injectable materiality policy separating local link observation from authoritative topology publication.</summary>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks>
class ITopologyPublicationPolicy {
public:
    virtual ~ITopologyPublicationPolicy() = default;

    /// <summary>
    /// Returns true when the candidate complete link set represents a material authoritative change that should publish.
    /// </summary>
    /// <remarks>
    /// Implementations own technology/application thresholds such as latency, reliability or observation hysteresis.
    /// Mesh deliberately provides no universal percentage, timing, confidence or scalar RouteCost threshold.
    /// </remarks>
    virtual bool ShouldPublish(
        const TopologyPublicationEvidence<TCharacteristics, LinkCapacity>& evidence
    ) const noexcept = 0;
};

/// <summary>Outcome of considering one complete normalized local outbound-link observation for publication.</summary>
enum class TopologyPublicationResult : std::uint8_t {
    Published,
    Unchanged,
    SuppressedByPolicy,
    GenerationExhausted,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Converts policy-approved complete local topology observations into authoritative monotonically generated snapshots.
/// </summary>
/// <remarks>
/// Semantic equality is set-based and ignores link ordering. An unchanged candidate never invokes the materiality policy
/// and never advances TopologyGeneration. A materially published change advances generation exactly once. A genuinely
/// new MembershipIncarnation starts again at generation 1. Generation never wraps.
///
/// This coordinator does not estimate link characteristics and does not choose materiality thresholds. Observation and
/// normalization belong below this boundary; dissemination/retransmission scheduling belongs above it.
/// </remarks>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks>
class TopologyPublicationCoordinator final {
public:
    using Link = DirectedTopologyLink<TCharacteristics>;

private:
    TopologySnapshot<TCharacteristics, LinkCapacity>& _published;
    const ITopologyPublicationPolicy<TCharacteristics, LinkCapacity>& _policy;

    bool EquivalentCandidate(const Link* links, std::size_t count) const noexcept {
        if (count != _published.Size()) return false;
        for (std::size_t i = 0; i < count; ++i) {
            bool matched = false;
            for (const auto& current : _published) {
                if (links[i] == current) {
                    matched = true;
                    break;
                }
            }
            if (!matched) return false;
        }
        return true;
    }

public:
    TopologyPublicationCoordinator(
        TopologySnapshot<TCharacteristics, LinkCapacity>& published,
        const ITopologyPublicationPolicy<TCharacteristics, LinkCapacity>& policy
    ) noexcept : _published(published), _policy(policy) {}

    /// <summary>Considers one complete normalized local outbound-link set for authoritative publication.</summary>
    TopologyPublicationResult ConsiderComplete(
        const System::DeviceIdentifier& authority,
        const MembershipIncarnation& incarnation,
        const Link* links,
        std::size_t count
    ) noexcept {
        if (!authority || !incarnation || (links == nullptr && count != 0U)) {
            return TopologyPublicationResult::Invalid;
        }
        if (count > LinkCapacity) return TopologyPublicationResult::ResourceUnavailable;
        for (std::size_t i = 0; i < count; ++i) {
            if (!links[i].Identity || links[i].Identity.Advertiser != authority) {
                return TopologyPublicationResult::Invalid;
            }
            for (std::size_t j = i + 1; j < count; ++j) {
                if (links[i].Identity == links[j].Identity) return TopologyPublicationResult::Invalid;
            }
        }

        const bool hasPublished = _published.Generation() != 0U;
        if (hasPublished && _published.Authority() != authority) return TopologyPublicationResult::Invalid;
        const bool newIncarnation = !hasPublished || _published.Incarnation() != incarnation;

        if (!newIncarnation && EquivalentCandidate(links, count)) {
            return TopologyPublicationResult::Unchanged;
        }

        const TopologyPublicationEvidence<TCharacteristics, LinkCapacity> evidence{
            _published,
            links,
            count,
            newIncarnation
        };
        if (!_policy.ShouldPublish(evidence)) return TopologyPublicationResult::SuppressedByPolicy;

        TopologyGeneration nextGeneration = 1U;
        if (!newIncarnation) {
            if (_published.Generation() == std::numeric_limits<TopologyGeneration>::max()) {
                return TopologyPublicationResult::GenerationExhausted;
            }
            nextGeneration = _published.Generation() + 1U;
        }

        const auto applied = _published.ApplyComplete(
            authority,
            incarnation,
            nextGeneration,
            links,
            count
        );
        switch (applied) {
            case TopologySnapshotApplyResult::Applied:
                return TopologyPublicationResult::Published;
            case TopologySnapshotApplyResult::ResourceUnavailable:
                return TopologyPublicationResult::ResourceUnavailable;
            case TopologySnapshotApplyResult::Invalid:
            case TopologySnapshotApplyResult::StaleGeneration:
            case TopologySnapshotApplyResult::ConflictingSameGeneration:
            case TopologySnapshotApplyResult::RefreshedSameGeneration:
                return TopologyPublicationResult::Invalid;
        }
        return TopologyPublicationResult::Invalid;
    }
};

} // namespace ESPressio::Mesh
