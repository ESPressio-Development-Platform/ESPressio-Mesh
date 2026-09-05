#pragma once

#include <cstddef>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_DeliveryAcknowledgementTracker.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_ForwardingRadioTerminalCorrelation.hpp"
#include "ESPressio_InboundDeliveryReservations.hpp"
#include "ESPressio_LivenessProbeReservations.hpp"
#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MembershipTombstoneTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_RouteCache.hpp"
#include "ESPressio_TopologyGraphStore.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Orders controlled local teardown of the principal non-application Mesh runtime stores.
/// </summary>
/// <remarks>
/// The caller must first stop every RadioTransport so no provider callback can repopulate forwarding correlation, then
/// reset exact externally owned application-delivery lifecycles through ApplicationRecipientLifecycleCoordinator. This
/// coordinator subsequently releases all retained non-application execution and remote-Mesh state before finally
/// resetting the traffic governor. It emits no wire message and creates no delivery, cancellation, reachability,
/// membership, Radio-terminal or clock evidence.
///
/// Composition configuration deliberately survives: registered primitive receivers, injected policies, registered
/// local Radio interfaces, the local MembershipIncarnation and its MeshMessageId high-water value are not runtime work.
/// Starting a genuinely new local membership incarnation remains an explicit identity-lifecycle operation and must reset
/// MeshRadioRegistry and construct/restore MeshMessageIdGenerator consistently outside this coordinator.
/// </remarks>
template<
    typename TTopologyCharacteristics,
    typename TClockQuality,
    std::size_t AcknowledgementCapacity,
    std::size_t CorrelationCapacity,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t TombstoneCapacity = Limits::MaxMembershipTombstones,
    std::size_t InboundDeliveryCapacity = Limits::MaxActiveInboundDeliveries,
    std::size_t CandidateCapacity = Limits::MaxPendingNeighbourCandidates,
    std::size_t AuthenticationCapacity = Limits::MaxActiveInboundAuthentications,
    std::size_t ProbeCapacity = Limits::MaxActiveLivenessProbes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks,
    std::size_t TopologyLinkCapacity = Limits::MaxTopologyLinks,
    std::size_t RouteCapacity = Limits::MaxRouteCacheEntries,
    std::size_t RouteHopCapacity = Limits::MaxRouteHops
>
class MeshRuntimeResetCoordinator final {
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MembershipLivenessTracker<MembershipCapacity>& _liveness;
    MembershipTombstoneTable<TombstoneCapacity>& _tombstones;
    InboundDeliveryReservationTable<InboundDeliveryCapacity>& _inboundDeliveries;
    PendingNeighbourCandidateTable<CandidateCapacity>& _candidates;
    InboundAuthenticationReservationTable<AuthenticationCapacity>& _authentications;
    LivenessProbeReservationTable<ProbeCapacity>& _probes;
    AuthenticatedDirectPeerBindingTable<BindingCapacity>& _directPeers;
    TopologyGraphStore<TTopologyCharacteristics, TopologyLinkCapacity, MembershipCapacity>& _topology;
    RouteCache<RouteCapacity, RouteHopCapacity>& _routes;
    DeliveryAcknowledgementTracker<AcknowledgementCapacity>& _acknowledgements;
    ForwardingRadioTerminalCorrelation<CorrelationCapacity>& _radioCorrelations;
    ClockCoordinationTable<TClockQuality, MembershipCapacity>& _clock;
    IMeshTrafficGovernor& _traffic;

public:
    MeshRuntimeResetCoordinator(
        AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        MembershipLivenessTracker<MembershipCapacity>& liveness,
        MembershipTombstoneTable<TombstoneCapacity>& tombstones,
        InboundDeliveryReservationTable<InboundDeliveryCapacity>& inboundDeliveries,
        PendingNeighbourCandidateTable<CandidateCapacity>& candidates,
        InboundAuthenticationReservationTable<AuthenticationCapacity>& authentications,
        LivenessProbeReservationTable<ProbeCapacity>& probes,
        AuthenticatedDirectPeerBindingTable<BindingCapacity>& directPeers,
        TopologyGraphStore<TTopologyCharacteristics, TopologyLinkCapacity, MembershipCapacity>& topology,
        RouteCache<RouteCapacity, RouteHopCapacity>& routes,
        DeliveryAcknowledgementTracker<AcknowledgementCapacity>& acknowledgements,
        ForwardingRadioTerminalCorrelation<CorrelationCapacity>& radioCorrelations,
        ClockCoordinationTable<TClockQuality, MembershipCapacity>& clock,
        IMeshTrafficGovernor& traffic
    ) noexcept :
        _memberships(memberships),
        _liveness(liveness),
        _tombstones(tombstones),
        _inboundDeliveries(inboundDeliveries),
        _candidates(candidates),
        _authentications(authentications),
        _probes(probes),
        _directPeers(directPeers),
        _topology(topology),
        _routes(routes),
        _acknowledgements(acknowledgements),
        _radioCorrelations(radioCorrelations),
        _clock(clock),
        _traffic(traffic) {}

    /// <summary>Releases every retained principal non-application runtime record in bounded deterministic order.</summary>
    void ResetForControlledShutdown() noexcept {
        _radioCorrelations.Clear();
        _acknowledgements.Clear();
        _inboundDeliveries.Clear();
        _authentications.Clear();
        _candidates.Clear();
        _probes.Clear();
        _routes.Clear();
        _topology.Clear();
        _directPeers.Clear();
        _clock.Clear();
        _liveness.Clear();
        _tombstones.Clear();
        _memberships.Clear();
        _traffic.ResetForControlledShutdown();
    }
};

} // namespace ESPressio::Mesh
