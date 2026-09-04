#include <Arduino.h>
#include <cstdint>

#include <ESPressio_MeshMemoryAccounting.hpp>

using namespace ESPressio::Mesh;

namespace {
struct TestTopologyCharacteristics final {
    std::int16_t SignalDbm{0};
    std::uint16_t CostHint{0};
};

using Accounting = MeshFixedMemoryAccounting<TestTopologyCharacteristics>;
volatile std::uint8_t espressio_mesh_probe_sink = 0;
}

extern "C" {
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_authenticated_membership[Accounting::AuthenticatedMembershipBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_membership_liveness[Accounting::MembershipLivenessBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_membership_tombstones[Accounting::MembershipTombstoneBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_inbound_delivery_reservations[Accounting::InboundDeliveryReservationBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_pending_neighbour_candidates[Accounting::PendingNeighbourCandidateBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_inbound_authentication_reservations[Accounting::InboundAuthenticationReservationBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_liveness_probe_reservations[Accounting::LivenessProbeReservationBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_authenticated_direct_peer_bindings[Accounting::AuthenticatedDirectPeerBindingBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_topology_graph[Accounting::TopologyGraphBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_route_cache[Accounting::RouteCacheBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_primitive_receiver_registry[Accounting::PrimitiveReceiverRegistryBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_traffic_governor[Accounting::TrafficGovernorBytes];
__attribute__((used)) volatile std::uint8_t espressio_mesh_accounting_ack_tracker_8[Accounting::DeliveryAcknowledgementBytes<8>()];
}

void setup() {
    // Volatile reads make every probe array a live linker dependency so the Xtensa ELF retains its exact ABI-sized symbol.
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_authenticated_membership[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_membership_liveness[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_membership_tombstones[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_inbound_delivery_reservations[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_pending_neighbour_candidates[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_inbound_authentication_reservations[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_liveness_probe_reservations[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_authenticated_direct_peer_bindings[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_topology_graph[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_route_cache[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_primitive_receiver_registry[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_traffic_governor[0];
    espressio_mesh_probe_sink ^= espressio_mesh_accounting_ack_tracker_8[0];
}

void loop() {}
