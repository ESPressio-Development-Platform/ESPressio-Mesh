#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <ESPressio_MeshMemoryAccounting.hpp>

using namespace ESPressio::Mesh;

namespace {
struct TestTopologyCharacteristics final {
    std::int16_t SignalDbm{0};
    std::uint16_t CostHint{0};
};
}

int main() {
    using Accounting = MeshFixedMemoryAccounting<TestTopologyCharacteristics>;

    static_assert(Accounting::AuthenticatedMembershipBytes == sizeof(AuthenticatedMembershipTable<>));
    static_assert(Accounting::MembershipLivenessBytes == sizeof(MembershipLivenessTracker<>));
    static_assert(Accounting::MembershipTombstoneBytes == sizeof(MembershipTombstoneTable<>));
    static_assert(Accounting::InboundDeliveryReservationBytes == sizeof(InboundDeliveryReservationTable<>));
    static_assert(Accounting::PendingNeighbourCandidateBytes == sizeof(PendingNeighbourCandidateTable<>));
    static_assert(Accounting::InboundAuthenticationReservationBytes == sizeof(InboundAuthenticationReservationTable<>));
    static_assert(Accounting::LivenessProbeReservationBytes == sizeof(LivenessProbeReservationTable<>));
    static_assert(Accounting::AuthenticatedDirectPeerBindingBytes == sizeof(AuthenticatedDirectPeerBindingTable<>));
    static_assert(Accounting::TopologyGraphBytes == sizeof(TopologyGraphStore<TestTopologyCharacteristics>));
    static_assert(Accounting::RouteCacheBytes == sizeof(RouteCache<>));
    static_assert(Accounting::PrimitiveReceiverRegistryBytes == sizeof(PrimitiveReceiverRegistry<>));
    static_assert(Accounting::TrafficGovernorBytes == sizeof(DefaultMeshTrafficGovernor));
    static_assert(Accounting::DeliveryAcknowledgementBytes<8>() == sizeof(DeliveryAcknowledgementTracker<8>));

    const std::size_t independentlySummed =
        Accounting::AuthenticatedMembershipBytes +
        Accounting::MembershipLivenessBytes +
        Accounting::MembershipTombstoneBytes +
        Accounting::InboundDeliveryReservationBytes +
        Accounting::PendingNeighbourCandidateBytes +
        Accounting::InboundAuthenticationReservationBytes +
        Accounting::LivenessProbeReservationBytes +
        Accounting::AuthenticatedDirectPeerBindingBytes +
        Accounting::TopologyGraphBytes +
        Accounting::RouteCacheBytes +
        Accounting::PrimitiveReceiverRegistryBytes +
        Accounting::TrafficGovernorBytes;
    assert(Accounting::PrincipalFixedCardinalityBytes == independentlySummed);

    // The printed value is intentionally ABI-specific. CI's x86-64 number must never be presented as an ESP32 budget.
    std::cout << "principal_fixed_cardinality_bytes=" << Accounting::PrincipalFixedCardinalityBytes << '\n';
    std::cout << "ack_tracker_8_bytes=" << Accounting::DeliveryAcknowledgementBytes<8>() << '\n';
    return 0;
}
