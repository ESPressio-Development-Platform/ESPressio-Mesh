#pragma once

#include <array>
#include <cstddef>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshRadioRegistry.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Removes pre-authentication neighbour state when its Radio-owned direct-peer handle ceases to be current.
/// </summary>
/// <remarks>
/// This observer deliberately affects only pending candidates and their expensive-authentication reservations.
/// A Radio peer invalidation is link-local evidence and MUST NOT remove, supersede, or otherwise mutate authenticated
/// membership authority. Authenticated member reachability converges separately through Mesh liveness/topology policy.
///
/// The callback is bounded by MaxPendingNeighbourCandidates and allocates no dynamic storage. It does not mutate
/// RadioTransport while the Radio Observable notification is active.
/// </remarks>
template<
    std::size_t RadioCapacity = Limits::MaxRadiosPerNode,
    std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
    std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications
>
class PreAuthenticationPeerLifecycleObserver final : public Radio::IRadioTransportPeerObserver {
    MeshRadioRegistry<RadioCapacity>& _radios;
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;

public:
    PreAuthenticationPeerLifecycleObserver(
        MeshRadioRegistry<RadioCapacity>& radios,
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications
    ) noexcept :
        _radios(radios),
        _candidates(candidates),
        _authentications(authentications) {}

    /// <summary>
    /// Peer observation alone does not create a Mesh neighbour candidate because no identity claim has yet been parsed.
    /// </summary>
    void OnRadioPeerObserved(
        Radio::RadioTransport&,
        Radio::IRadio&,
        Radio::RadioPeerHandle,
        const Radio::RadioAddress&
    ) override {}

    /// <summary>
    /// Releases all pre-authentication state bound to the exact invalidated RadioIdentifier + RadioPeerHandle.
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
        std::size_t removalCount = 0U;
        _candidates.ForEach([&](const PendingNeighbourCandidate& candidate) {
            if (candidate.Radio == radioIdentifier && candidate.Peer == peer && removalCount < removals.size()) {
                removals[removalCount++] = candidate.Handle;
            }
        });

        for (std::size_t index = 0; index < removalCount; ++index) {
            _authentications.Release(removals[index]);
            _candidates.Remove(removals[index]);
        }
    }
};

} // namespace ESPressio::Mesh
