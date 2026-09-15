#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioRuntime.hpp>

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

/// <summary>Narrow bridge from final Radio direct-peer evidence into bounded Mesh pre-authentication candidate storage.</summary>
/// <remarks>
/// The complete inbound handle is a direct-link fact only. Provider and DirectPeer identify where the untrusted claim was
/// observed; claimed DeviceIdentifier/MembershipIncarnation remain untrusted until the separate authentication lifecycle
/// succeeds. This coordinator owns no Radio receive bytes, parsing, cryptography or semantic admission authority.
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

    /// <summary>Records one untrusted membership claim against the exact final-Radio source peer.</summary>
    NeighbourDiscoveryResult ObserveClaim(
        const Radio::RadioInboundTransferHandle& transfer,
        const UntrustedMembershipClaim& claim,
        std::uint64_t nowMilliseconds,
        NeighbourCandidateHandle& candidate
    ) noexcept {
        candidate = {};
        if (transfer.Provider == nullptr) return NeighbourDiscoveryResult::RadioNotRegistered;
        const auto radioIdentifier = _radios.IdentifierOf(*transfer.Provider);
        if (radioIdentifier == 0U) return NeighbourDiscoveryResult::RadioNotRegistered;
        if (!transfer.DirectPeer) return NeighbourDiscoveryResult::InvalidPeer;
        if (!claim.Device || !claim.Incarnation || nowMilliseconds == 0U) {
            return NeighbourDiscoveryResult::InvalidClaim;
        }

        switch (_candidates.Observe(
            radioIdentifier,
            transfer.DirectPeer,
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
