#include <cassert>
#include <array>
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

struct TestClockQuality final {
    std::uint32_t UncertaintyNanoseconds{0};
};

struct TestSecurityAuthorityStorage final {
    std::array<std::uint8_t, 512> Bytes{};
};

using TestPlatformProfile = MeshPlatformCapacityProfile<
    0x54455354U, // TEST
    4096,
    512,
    4096,
    ESPRESSIO_RADIO_MAX_REASSEMBLIES,
    ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES,
    8192,
    1024
>;
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
        Accounting::TrafficGovernorBytes +
        Accounting::ApplicationTransmissionBytes +
        Accounting::SecuritySessionBytes;
    assert(Accounting::PrincipalFixedCardinalityBytes == independentlySummed);

    using WholeDevice = MeshWholeDeviceMemoryAccounting<
        TestTopologyCharacteristics,
        TestClockQuality,
        8,
        TestPlatformProfile,
        TestSecurityAuthorityStorage
    >;
    static_assert(WholeDevice::RadioReassemblyPayloadBytes == ESPressio::Radio::RadioTransport::ReassemblyPayloadCapacityBytes);
    static_assert(WholeDevice::RadioTransportBytes == sizeof(ESPressio::Radio::RadioTransport));
    static_assert(WholeDevice::SecurityAuthorityBytes == sizeof(TestSecurityAuthorityStorage));
    static_assert(WholeDevice::InboundOwnedPoolBytes == sizeof(TestPlatformProfile::InboundDeliveryPool));
    static_assert(WholeDevice::ControlOwnedPoolBytes == sizeof(TestPlatformProfile::ControlFramePool));
    static_assert(WholeDevice::ApplicationOwnedPoolBytes == sizeof(TestPlatformProfile::ApplicationPayloadPool));
    static_assert(WholeDevice::TotalAccountedBytes ==
        WholeDevice::MeshPrincipalBytes +
        WholeDevice::ClockCoordinationBytes +
        WholeDevice::DeliveryAcknowledgementBytes +
        WholeDevice::InboundOwnedPoolBytes +
        WholeDevice::ControlOwnedPoolBytes +
        WholeDevice::ApplicationOwnedPoolBytes +
        WholeDevice::RadioTransportBytes +
        WholeDevice::SecurityAuthorityBytes +
        WholeDevice::TaskStackBytes +
        WholeDevice::OtherCompositionBytes
    );

    TestPlatformProfile::InboundDeliveryPool pool;
    const std::array<std::uint8_t, 4> payload{{1, 2, 3, 4}};
    OwnedBytePoolHandle first{};
    assert(pool.Store(payload.data(), payload.size(), first));
    assert(first);
    const auto retained = pool.Resolve(first);
    assert(retained && retained.Size == payload.size());
    assert(retained.Data[0] == 1U && retained.Data[3] == 4U);
    assert(pool.Release(first));
    assert(!pool.Resolve(first));

    OwnedBytePoolHandle second{};
    MutableOwnedByteView writable{};
    assert(pool.Acquire(payload.size(), second, writable));
    assert(second && second.Generation != first.Generation);
    assert(!pool.Resolve(first));
    pool.ResetForControlledShutdown();
    assert(!pool.Resolve(second));

    // Printed values are intentionally ABI-specific. CI's x86-64 numbers must never be presented as an ESP32 budget.
    std::cout << "principal_fixed_cardinality_bytes=" << Accounting::PrincipalFixedCardinalityBytes << '\n';
    std::cout << "application_transmission_bytes=" << Accounting::ApplicationTransmissionBytes << '\n';
    std::cout << "clock_coordination_test_quality_bytes=" << Accounting::ClockCoordinationBytes<TestClockQuality>() << '\n';
    std::cout << "ack_tracker_8_bytes=" << Accounting::DeliveryAcknowledgementBytes<8>() << '\n';
    std::cout << "whole_device_test_profile_bytes=" << WholeDevice::TotalAccountedBytes << '\n';
    return 0;
}
