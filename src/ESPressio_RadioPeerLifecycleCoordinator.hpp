#pragma once

#include <array>
#include <cstddef>

#include <ESPressio_IRadio.hpp>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Removes pre-authentication neighbour work when an explicitly identified final-Radio peer ceases to exist.</summary>
/// <remarks>
/// Final Radio deliberately exposes no semantic peer-observer graph. The composition root supplies the local
/// RadioIdentifier together with the invalidated RadioPeerHandle. Cleanup is bounded by CandidateCapacity and affects only
/// pre-authentication candidate/reservation state; authenticated membership and distributed liveness remain independent.
/// </remarks>
template<
    std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
    std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications
>
class RadioPeerLifecycleCoordinator final {
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;

public:
    RadioPeerLifecycleCoordinator(
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications
    ) noexcept : _candidates(candidates), _authentications(authentications) {}

    void RadioPeerObserved(RadioIdentifier, Radio::RadioPeerHandle) noexcept {}

    void RadioPeerInvalidated(RadioIdentifier radioIdentifier, Radio::RadioPeerHandle peer) noexcept {
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
