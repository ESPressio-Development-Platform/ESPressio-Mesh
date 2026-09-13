#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <ESPressio_MeshMemoryAccounting.hpp>

using namespace ESPressio::Mesh;

namespace {
struct TestTopologyCharacteristics final { std::int16_t SignalDbm{0}; std::uint16_t CostHint{0}; };
struct TestClockQuality final { std::uint32_t UncertaintyNanoseconds{0}; };
struct TestSecurityAuthorityStorage final { std::array<std::uint8_t,512> Bytes{}; };
struct TestRuntimeWorkerStorage final { std::array<std::uint8_t,96> Bytes{}; };

using TestPlatformProfile = MeshPlatformCapacityProfile<
    0x54455354U, 4096, 512, 4096,
    ESPRESSIO_RADIO_MAX_REASSEMBLIES, ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES,
    8192, 1024>;

using RelayArena = MeshRelayByteArena<
    MeshRelayByteClass<64,8>, MeshRelayByteClass<256,4>, MeshRelayByteClass<1024,2>>;
using RelayDomain = MeshRelayCapacityDomain<4,128,RelayArena>;
using InboundRelay = MeshRelayCapacityPlane<MeshRelayDirection::Inbound,
    RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain>;
using OutboundRelay = MeshRelayCapacityPlane<MeshRelayDirection::Outbound,
    RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain,RelayDomain>;
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
    static_assert(Accounting::ApplicationTransmissionBytes == sizeof(ApplicationTransmissionTable<>));
    static_assert(Accounting::SecuritySessionBytes == sizeof(MeshSecuritySessionTable<>));
    static_assert(Accounting::ClockCoordinationBytes<TestClockQuality>() == sizeof(ClockCoordinationTable<TestClockQuality>));
    static_assert(Accounting::DeliveryAcknowledgementBytes<8>() == sizeof(DeliveryAcknowledgementTracker<8>));

    using Runtime = MeshRuntimeMemoryAccounting<
        TestTopologyCharacteristics,TestClockQuality,8,TestPlatformProfile,
        TestSecurityAuthorityStorage,InboundRelay,OutboundRelay,TestRuntimeWorkerStorage>;
    static_assert(Runtime::InboundRelayPlaneBytes == sizeof(InboundRelay));
    static_assert(Runtime::OutboundRelayPlaneBytes == sizeof(OutboundRelay));
    static_assert(Runtime::RelayCapacityProfileBytes == sizeof(MeshRelayCapacityProfile));
    static_assert(Runtime::RuntimeWorkerObjectBytes == sizeof(TestRuntimeWorkerStorage));
    static_assert(Runtime::SecurityAuthorityBytes == sizeof(TestSecurityAuthorityStorage));
    static_assert(Runtime::InboundOwnedPoolBytes == sizeof(TestPlatformProfile::InboundDeliveryPool));
    static_assert(Runtime::ControlOwnedPoolBytes == sizeof(TestPlatformProfile::ControlFramePool));
    static_assert(Runtime::ApplicationOwnedPoolBytes == sizeof(TestPlatformProfile::ApplicationPayloadPool));
    static_assert(Runtime::TotalMeshReservedBytes == Runtime::TotalMeshOwnedObjectBytes + Runtime::TaskStackBytes);

    TestPlatformProfile::InboundDeliveryPool pool;
    const std::array<std::uint8_t,4> payload{{1,2,3,4}};
    OwnedBytePoolHandle first{};
    assert(pool.Store(payload.data(),payload.size(),first));
    assert(first);
    const auto retained=pool.Resolve(first);
    assert(retained && retained.Size==payload.size());
    assert(retained.Data[0]==1U && retained.Data[3]==4U);
    assert(pool.Release(first));
    assert(!pool.Resolve(first));

    OwnedBytePoolHandle second{};
    MutableOwnedByteView writable{};
    assert(pool.Acquire(payload.size(),second,writable));
    assert(second && second.Generation!=first.Generation);
    pool.ResetForControlledShutdown();
    assert(!pool.Resolve(second));

    std::cout << "principal_fixed_cardinality_bytes=" << Accounting::PrincipalFixedCardinalityBytes << '\n';
    std::cout << "relay_inbound_plane_bytes=" << Runtime::InboundRelayPlaneBytes << '\n';
    std::cout << "relay_outbound_plane_bytes=" << Runtime::OutboundRelayPlaneBytes << '\n';
    std::cout << "runtime_worker_object_bytes=" << Runtime::RuntimeWorkerObjectBytes << '\n';
    std::cout << "task_stack_bytes=" << Runtime::TaskStackBytes << '\n';
    std::cout << "mesh_reserved_test_profile_bytes=" << Runtime::TotalMeshReservedBytes << '\n';
    return 0;
}
