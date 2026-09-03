#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshTypes.hpp"
#include "ESPressio_TopologySnapshot.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Read-only evidence supplied to an injected topology freshness policy for one authority snapshot.
/// </summary>
/// <remarks>
/// Age is measured only from the local monotonic receipt clock; remote wall-clock timestamps are never compared.
/// ExpectedCadenceMilliseconds is an operational hint supplied by composition and may be zero when unknown.
/// The complete topology snapshot is available so a policy may inspect technology-independent link characteristics
/// without this layer inventing a universal confidence, latency or scalar routing-cost representation.
/// </remarks>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks>
struct TopologyFreshnessEvidence final {
    const TopologySnapshot<TCharacteristics, LinkCapacity>& Snapshot;
    std::uint64_t LocalAgeMilliseconds{0};
    std::uint64_t ExpectedCadenceMilliseconds{0};
    ReachabilityState AuthorityReachability{ReachabilityState::Unknown};
};

/// <summary>Injectable policy deriving local freshness independently from authoritative TopologyGeneration.</summary>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks>
class ITopologyFreshnessPolicy {
public:
    virtual ~ITopologyFreshnessPolicy() = default;

    /// <summary>Classifies the current local usability/freshness of one authenticated topology snapshot.</summary>
    virtual TopologyFreshnessState Classify(
        const TopologyFreshnessEvidence<TCharacteristics, LinkCapacity>& evidence
    ) const noexcept = 0;
};

/// <summary>
/// Tracks local receipt freshness for one authority-scoped complete topology snapshot.
/// </summary>
/// <remarks>
/// This tracker never changes TopologyGeneration or link semantics. Authenticated retransmission of the same complete
/// generation may refresh LastReceiptMilliseconds after the caller verifies it is semantically identical. A new
/// generation may likewise mark receipt after successful application. An old incarnation or mismatched generation can
/// never refresh the current snapshot. Freshness classification is entirely delegated to ITopologyFreshnessPolicy.
/// </remarks>
template<typename TCharacteristics, std::size_t LinkCapacity = Limits::MaxTopologyLinks>
class TopologyFreshnessTracker final {
    TopologySnapshot<TCharacteristics, LinkCapacity>& _snapshot;
    const ITopologyFreshnessPolicy<TCharacteristics, LinkCapacity>& _policy;
    std::uint64_t _lastReceiptMilliseconds{0};
    TopologyFreshnessState _state{TopologyFreshnessState::Expired};

public:
    TopologyFreshnessTracker(
        TopologySnapshot<TCharacteristics, LinkCapacity>& snapshot,
        const ITopologyFreshnessPolicy<TCharacteristics, LinkCapacity>& policy
    ) noexcept : _snapshot(snapshot), _policy(policy) {}

    constexpr std::uint64_t LastReceiptMilliseconds() const noexcept { return _lastReceiptMilliseconds; }
    constexpr TopologyFreshnessState State() const noexcept { return _state; }
    constexpr bool HasReceipt() const noexcept { return _lastReceiptMilliseconds != 0U; }

    /// <summary>
    /// Records local receipt time only for the exact currently retained authenticated authority/incarnation/generation.
    /// </summary>
    bool ObserveAuthenticatedReceipt(
        const System::DeviceIdentifier& authority,
        const MembershipIncarnation& incarnation,
        TopologyGeneration generation,
        std::uint64_t nowMilliseconds
    ) noexcept {
        if (nowMilliseconds == 0U ||
            _snapshot.Generation() == 0U ||
            _snapshot.Authority() != authority ||
            _snapshot.Incarnation() != incarnation ||
            _snapshot.Generation() != generation) {
            return false;
        }
        if (_lastReceiptMilliseconds != 0U && nowMilliseconds < _lastReceiptMilliseconds) return false;
        _lastReceiptMilliseconds = nowMilliseconds;
        return true;
    }

    /// <summary>
    /// Re-evaluates freshness from local age, operational cadence, authority reachability and snapshot characteristics.
    /// </summary>
    TopologyFreshnessState Evaluate(
        std::uint64_t nowMilliseconds,
        ReachabilityState authorityReachability,
        std::uint64_t expectedCadenceMilliseconds = 0U
    ) noexcept {
        if (_snapshot.Generation() == 0U || _lastReceiptMilliseconds == 0U || nowMilliseconds < _lastReceiptMilliseconds) {
            _state = TopologyFreshnessState::Expired;
            return _state;
        }

        const TopologyFreshnessEvidence<TCharacteristics, LinkCapacity> evidence{
            _snapshot,
            nowMilliseconds - _lastReceiptMilliseconds,
            expectedCadenceMilliseconds,
            authorityReachability
        };
        _state = _policy.Classify(evidence);
        return _state;
    }

    /// <summary>Clears local freshness metadata without mutating the authoritative topology snapshot.</summary>
    void Reset() noexcept {
        _lastReceiptMilliseconds = 0U;
        _state = TopologyFreshnessState::Expired;
    }
};

} // namespace ESPressio::Mesh
