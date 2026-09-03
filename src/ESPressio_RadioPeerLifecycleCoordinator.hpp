#pragma once

#include <array>
#include <cstddef>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshRadioRegistry.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Removes pre-authentication neighbour work when its Radio-owned peer binding ceases to exist.
/// </summary>
/// <remarks>
/// This coordinator observes only Radio peer lifecycle. It never mutates authenticated membership, liveness,
/// deduplication or tombstones: link-local peer loss is not authoritative membership loss. Matching pending
/// candidates are removed because their process-local RadioPeerHandle is no longer resolvable, and any expensive
/// inbound-authentication reservation held for those candidates is released first.
///
/// The coordinator is intended for the serialized Mesh execution domain. Cleanup is bounded by CandidateCapacity
/// and uses a fixed local handle array rather than allocating a snapshot.
/// </remarks>
template<
    std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
    std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
    std::size_t RadioCapacity = Limits::MaxRadiosPerNode
>
class RadioPeerLifecycleCoordinator final : public Radio::IRadioTransportPeerObserver {
    MeshRadioRegistry<RadioCapacity>& _radios;
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;

public:
    RadioPeerLifecycleCoordinator(
        MeshRadioRegistry<RadioCapacity>& radios,
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications
    ) noexcept :
        _radios(radios),
        _candidates(candidates),
        _authentications(authentications) {}

    /// <summary>Peer observation itself does not create Mesh admission state; discovery payload processing does that.</summary>
    void OnRadioPeerObserved(
        Radio::RadioTransport&,
        Radio::IRadio&,
        Radio::RadioPeerHandle,
        const Radio::RadioAddress&
    ) override {}

    /// <summary>
    /// Releases every pre-auth candidate bound to the invalidated RadioIdentifier + RadioPeerHandle pair.
    /// </summary>
    void OnRadioPeerInvalidated(
        Radio::RadioTransport&,
        Radio::IRadio& radio,
        Radio::RadioPeerHandle peer,
        const Radio::RadioAddress&,
        Radio::RadioPeerInvalidationReason
    ) override {
        const RadioIdentifier radioIdentifier = _radios.IdentifierOf(radio);
        if (radioIdentifier == 0U || !peer) return;

        std::array<NeighbourCandidateHandle, CandidateCapacity> removals{};
        std::size_t removalCount = 0;
        _candidates.ForEach([&](const PendingNeighbourCandidate& candidate) {
            if (candidate.Radio == radioIdentifier && candidate.Peer == peer && removalCount < removals.size()) {
                removals[removalCount++] = candidate.Handle;
            }
        });

        for (std::size_t index = 0; index < removalCount; ++index) {
            const auto handle = removals[index];
            _authentications.Release(handle);
            _candidates.Remove(handle);
        }
    }
};

} // namespace ESPressio::Mesh
