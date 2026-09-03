#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_MeshRadioRegistry.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of accepting one pre-authentication membership claim from a direct Radio peer.</summary>
enum class NeighbourDiscoveryResult : std::uint8_t {
    Inserted,
    Refreshed,
    CandidateResourceUnavailable,
    RadioNotRegistered,
    InvalidPeer,
    InvalidClaim
};

/// <summary>
/// Narrow bridge from Radio-owned direct-peer evidence into bounded Mesh pre-authentication candidate storage.
/// </summary>
/// <remarks>
/// This component performs no parsing, cryptography or admission decision. The caller supplies an untrusted claim
/// extracted from Mesh discovery/control content, while the direct source peer comes from RadioTransport. The peer
/// handle and local RadioIdentifier establish only the link on which the claim was observed; the claimed
/// DeviceIdentifier/MembershipIncarnation remain untrusted until external authentication succeeds.
/// </remarks>
template<
    std::size_t RadioCapacity = Limits::MaxRadiosPerNode,
    std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates
>
class NeighbourDiscoveryCoordinator final {
    MeshRadioRegistry<RadioCapacity>& _radios;
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;

public:
    NeighbourDiscoveryCoordinator(
        MeshRadioRegistry<RadioCapacity>& radios,
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates
    ) noexcept : _radios(radios), _candidates(candidates) {}

    /// <summary>
    /// Records one untrusted membership claim against the exact Radio-owned source peer of a complete transfer.
    /// </summary>
    NeighbourDiscoveryResult ObserveClaim(
        Radio::IRadio& ingressRadio,
        const Radio::RadioTransportMessageView& transfer,
        const UntrustedMembershipClaim& claim,
        std::uint64_t nowMilliseconds,
        NeighbourCandidateHandle& candidate
    ) noexcept {
        candidate = {};
        const auto radioIdentifier = _radios.IdentifierOf(ingressRadio);
        if (radioIdentifier == 0U) return NeighbourDiscoveryResult::RadioNotRegistered;
        if (!transfer.SourcePeer) return NeighbourDiscoveryResult::InvalidPeer;
        if (!claim.Device || !claim.Incarnation || nowMilliseconds == 0U) {
            return NeighbourDiscoveryResult::InvalidClaim;
        }

        switch (_candidates.Observe(
            radioIdentifier,
            transfer.SourcePeer,
            claim,
            nowMilliseconds,
            candidate
        )) {
            case PendingCandidateInsertResult::Inserted:
                return NeighbourDiscoveryResult::Inserted;
            case PendingCandidateInsertResult::Refreshed:
                return NeighbourDiscoveryResult::Refreshed;
            case PendingCandidateInsertResult::ResourceUnavailable:
                return NeighbourDiscoveryResult::CandidateResourceUnavailable;
            case PendingCandidateInsertResult::Invalid:
                return NeighbourDiscoveryResult::InvalidClaim;
        }
        return NeighbourDiscoveryResult::InvalidClaim;
    }
};

} // namespace ESPressio::Mesh
