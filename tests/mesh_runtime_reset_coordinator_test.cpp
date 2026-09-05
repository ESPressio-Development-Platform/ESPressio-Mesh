#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_MeshRuntimeResetCoordinator.hpp>

using namespace ESPressio;

namespace {
struct Characteristics final {
    std::uint16_t Metric{0};
    constexpr bool operator==(const Characteristics& other) const noexcept { return Metric == other.Metric; }
};

struct ClockQuality final { std::uint32_t UncertaintyNanoseconds{0}; };

System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes[15] = tail;
    return Mesh::MembershipIncarnation{bytes};
}
}

int main() {
    constexpr std::size_t Capacity = 2U;
    const auto local = Device(1);
    const auto remote = Device(2);
    const auto incarnation = Incarnation(1);

    Mesh::AuthenticatedMembershipTable<Capacity> memberships;
    assert(memberships.UpsertAuthenticated(remote, incarnation, Mesh::MembershipState::Active) ==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Mesh::DefaultMeshLivenessPolicy livenessPolicy;
    Mesh::MembershipLivenessTracker<Capacity> liveness{memberships, livenessPolicy};
    assert(liveness.ObserveAuthenticatedEvidence(remote, incarnation, 100U));

    Mesh::MembershipTombstoneTable<Capacity> tombstones;
    assert(tombstones.Record(Device(3), Incarnation(3), Mesh::MembershipTombstoneDisposition::LocallyForgotten, 100U));

    Mesh::InboundDeliveryReservationTable<Capacity> inboundDeliveries;
    const Mesh::InboundDeliveryIdentity inbound{remote, incarnation, 7U};
    assert(inboundDeliveries.TryReserve(inbound) == Mesh::InboundDeliveryReservationResult::Reserved);

    Mesh::PendingNeighbourCandidateTable<Capacity> candidates;
    Mesh::NeighbourCandidateHandle candidate{};
    assert(candidates.Observe(1U, Radio::RadioPeerHandle{0U, 1U}, {remote, incarnation}, 100U, candidate) ==
           Mesh::PendingCandidateInsertResult::Inserted);

    Mesh::InboundAuthenticationReservationTable<Capacity> authentications;
    assert(authentications.TryReserve(candidate) == Mesh::InboundAuthenticationReservationResult::Reserved);

    Mesh::LivenessProbeReservationTable<Capacity> probes;
    Mesh::LivenessProbeReservation probe{};
    assert(probes.TryReserve(remote, incarnation, probe) == Mesh::LivenessProbeReservationResult::Reserved);

    Mesh::AuthenticatedDirectPeerBindingTable<Capacity> directPeers;
    assert(directPeers.Bind({remote, incarnation, 1U, Radio::RadioPeerHandle{0U, 1U}}) ==
           Mesh::DirectPeerBindingResult::Bound);

    Mesh::TopologyGraphStore<Characteristics, Capacity, Capacity> topology;
    const Mesh::DirectedTopologyLink<Characteristics> link{{local, 1U, remote, 1U}, {10U}};
    assert(topology.ApplyComplete(local, incarnation, 1U, &link, 1U) == Mesh::TopologySnapshotApplyResult::Applied);

    Mesh::ResolvedRoute<Capacity> route;
    assert(route.Assign(local, remote, &link.Identity, 1U));
    Mesh::RouteCache<Capacity, Capacity> routes;
    Mesh::RouteCacheHandle routeHandle{};
    assert(routes.Store(route, routeHandle) == Mesh::RouteCacheStoreResult::Stored);

    Mesh::DeliveryAcknowledgementTracker<Capacity> acknowledgements;
    const Mesh::PendingDeliveryAcknowledgementIdentity acknowledgement{remote, incarnation, 7U};
    assert(acknowledgements.Reserve(acknowledgement, 100U, 200U) ==
           Mesh::DeliveryAcknowledgementReserveResult::Reserved);

    Mesh::ForwardingRadioTerminalCorrelation<Capacity> radioCorrelations;
    const auto correlation = radioCorrelations.Reserve();
    assert(correlation && radioCorrelations.Bind(correlation, Radio::DeferredLogicalTransferHandle{0U, 1U}));

    Mesh::ClockCoordinationTable<ClockQuality, Capacity> clock;
    assert(clock.Observe({remote, incarnation, remote, Mesh::ClockRootStratum, {100U}, 100U}));

    Mesh::DefaultMeshTrafficGovernor traffic;
    Mesh::MeshTrafficReservation trafficReservation{};
    assert(traffic.TryAcquire(Mesh::MeshTrafficClass::GeneralControl, trafficReservation) ==
           Mesh::MeshTrafficAdmissionResult::Admitted);

    Mesh::MeshRuntimeResetCoordinator<Characteristics, ClockQuality, Capacity, Capacity,
                                      Capacity, Capacity, Capacity, Capacity, Capacity,
                                      Capacity, Capacity, Capacity, Capacity, Capacity> reset{
        memberships, liveness, tombstones, inboundDeliveries, candidates, authentications, probes,
        directPeers, topology, routes, acknowledgements, radioCorrelations, clock, traffic
    };
    reset.ResetForControlledShutdown();

    assert(memberships.Empty());
    assert(liveness.EvidenceFor(remote, incarnation) == nullptr);
    assert(tombstones.Empty());
    assert(inboundDeliveries.Empty());
    assert(candidates.Size() == 0U);
    assert(authentications.Size() == 0U);
    assert(probes.Size() == 0U);
    assert(directPeers.Size() == 0U);
    assert(topology.LinkCount() == 0U && topology.AuthorityCount() == 0U);
    assert(routes.Empty());
    assert(acknowledgements.Empty());
    assert(radioCorrelations.Size() == 0U);
    assert(clock.Size() == 0U);
    assert(traffic.Active(Mesh::MeshTrafficClass::GeneralControl) == 0U);

    assert(candidates.Resolve(candidate) == nullptr);
    assert(!probes.Release(probe));
    assert(routes.Resolve(routeHandle) == nullptr);
    assert(!radioCorrelations.Release(correlation));
    assert(!traffic.Release(trafficReservation));

    Mesh::NeighbourCandidateHandle replacementCandidate{};
    assert(candidates.Observe(1U, Radio::RadioPeerHandle{1U, 1U}, {remote, incarnation}, 200U,
                              replacementCandidate) == Mesh::PendingCandidateInsertResult::Inserted);
    assert(replacementCandidate.Slot == candidate.Slot && replacementCandidate.Generation != candidate.Generation);

    Mesh::LivenessProbeReservation replacementProbe{};
    assert(probes.TryReserve(remote, incarnation, replacementProbe) == Mesh::LivenessProbeReservationResult::Reserved);
    assert(replacementProbe.Slot == probe.Slot && replacementProbe.Generation != probe.Generation);

    const auto replacementCorrelation = radioCorrelations.Reserve();
    assert(replacementCorrelation.Slot == correlation.Slot &&
           replacementCorrelation.Generation != correlation.Generation);

    Mesh::MeshTrafficReservation replacementTraffic{};
    assert(traffic.TryAcquire(Mesh::MeshTrafficClass::GeneralControl, replacementTraffic) ==
           Mesh::MeshTrafficAdmissionResult::Admitted);
    assert(replacementTraffic.Slot == trafficReservation.Slot &&
           replacementTraffic.Generation != trafficReservation.Generation);
    return 0;
}
