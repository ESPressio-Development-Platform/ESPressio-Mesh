#pragma once

#include <cstddef>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_DeliveryAcknowledgementTracker.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_InboundDeliveryReservations.hpp"
#include "ESPressio_LivenessProbeReservations.hpp"
#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MembershipTombstoneTable.hpp"
#include "ESPressio_MeshCapacityProfile.hpp"
#include "ESPressio_MeshRelayCapacity.hpp"
#include "ESPressio_MeshRelayCapacityProfile.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"
#include "ESPressio_RouteCache.hpp"
#include "ESPressio_TopologyGraphStore.hpp"

namespace ESPressio::Mesh {

/// <summary>Target-native accounting for principal fixed/cardinality Mesh stores.</summary>
/// <remarks>
/// Values use sizeof so they describe the ABI being compiled, never an assumed host/ESP32 layout. Radio-owned Q1,
/// reassembly, scheduler, Clock exchange and provider resources are deliberately not repeated here: Radio publishes its
/// own deterministic resource snapshot. Mesh accounts only resources it owns, including complete relay Q1 planes.
/// </remarks>
template<typename TTopologyCharacteristics>
struct MeshFixedMemoryAccounting final {
    static constexpr std::size_t AuthenticatedMembershipBytes = sizeof(AuthenticatedMembershipTable<>);
    static constexpr std::size_t MembershipLivenessBytes = sizeof(MembershipLivenessTracker<>);
    static constexpr std::size_t MembershipTombstoneBytes = sizeof(MembershipTombstoneTable<>);
    static constexpr std::size_t InboundDeliveryReservationBytes = sizeof(InboundDeliveryReservationTable<>);
    static constexpr std::size_t PendingNeighbourCandidateBytes = sizeof(PendingNeighbourCandidateTable<>);
    static constexpr std::size_t InboundAuthenticationReservationBytes = sizeof(InboundAuthenticationReservationTable<>);
    static constexpr std::size_t LivenessProbeReservationBytes = sizeof(LivenessProbeReservationTable<>);
    static constexpr std::size_t AuthenticatedDirectPeerBindingBytes = sizeof(AuthenticatedDirectPeerBindingTable<>);
    static constexpr std::size_t TopologyGraphBytes = sizeof(TopologyGraphStore<TTopologyCharacteristics>);
    static constexpr std::size_t RouteCacheBytes = sizeof(RouteCache<>);
    static constexpr std::size_t PrimitiveReceiverRegistryBytes = sizeof(PrimitiveReceiverRegistry<>);
    static constexpr std::size_t TrafficGovernorBytes = sizeof(DefaultMeshTrafficGovernor);
    static constexpr std::size_t ApplicationTransmissionBytes = sizeof(ApplicationTransmissionTable<>);
    static constexpr std::size_t SecuritySessionBytes = sizeof(MeshSecuritySessionTable<>);

    static constexpr std::size_t PrincipalFixedCardinalityBytes =
        AuthenticatedMembershipBytes + MembershipLivenessBytes + MembershipTombstoneBytes +
        InboundDeliveryReservationBytes + PendingNeighbourCandidateBytes + InboundAuthenticationReservationBytes +
        LivenessProbeReservationBytes + AuthenticatedDirectPeerBindingBytes + TopologyGraphBytes + RouteCacheBytes +
        PrimitiveReceiverRegistryBytes + TrafficGovernorBytes + ApplicationTransmissionBytes + SecuritySessionBytes;

    template<typename TClockQuality>
    static constexpr std::size_t ClockCoordinationBytes() noexcept {
        return sizeof(ClockCoordinationTable<TClockQuality>);
    }

    template<std::size_t AcknowledgementCapacity>
    static constexpr std::size_t DeliveryAcknowledgementBytes() noexcept {
        static_assert(AcknowledgementCapacity > 0, "Acknowledgement capacity must be explicitly finite and non-zero.");
        return sizeof(DeliveryAcknowledgementTracker<AcknowledgementCapacity>);
    }
};

/// <summary>Exact Mesh-owned relay/runtime accounting for one statically composed target profile.</summary>
/// <remarks>
/// The supplied relay profile is immutable configuration capability (not live free-space telemetry) and is retained in
/// the report beside the concrete inbound/outbound plane object footprints. Worker stack bytes stay separate from the
/// worker object so platform-owned stack storage cannot be double-counted.
/// </remarks>
template<
    typename TTopologyCharacteristics,
    typename TClockQuality,
    std::size_t AcknowledgementCapacity,
    typename TCapacityProfile,
    typename TSecurityAuthority,
    typename TInboundRelayPlane,
    typename TOutboundRelayPlane,
    typename TMeshRuntimeWorker
>
struct MeshRuntimeMemoryAccounting final {
    using Fixed = MeshFixedMemoryAccounting<TTopologyCharacteristics>;

    static constexpr std::size_t MeshPrincipalBytes = Fixed::PrincipalFixedCardinalityBytes;
    static constexpr std::size_t ClockCoordinationBytes = Fixed::template ClockCoordinationBytes<TClockQuality>();
    static constexpr std::size_t DeliveryAcknowledgementBytes =
        Fixed::template DeliveryAcknowledgementBytes<AcknowledgementCapacity>();
    static constexpr std::size_t InboundOwnedPoolBytes = sizeof(typename TCapacityProfile::InboundDeliveryPool);
    static constexpr std::size_t ControlOwnedPoolBytes = sizeof(typename TCapacityProfile::ControlFramePool);
    static constexpr std::size_t ApplicationOwnedPoolBytes = sizeof(typename TCapacityProfile::ApplicationPayloadPool);
    static constexpr std::size_t InboundRelayPlaneBytes = sizeof(TInboundRelayPlane);
    static constexpr std::size_t OutboundRelayPlaneBytes = sizeof(TOutboundRelayPlane);
    static constexpr std::size_t RelayCapacityProfileBytes = sizeof(MeshRelayCapacityProfile);
    static constexpr std::size_t SecurityAuthorityBytes = sizeof(TSecurityAuthority);
    static constexpr std::size_t RuntimeWorkerObjectBytes = sizeof(TMeshRuntimeWorker);
    static constexpr std::size_t TaskStackBytes = TCapacityProfile::ReservedTaskStackBytes;
    static constexpr std::size_t OtherCompositionBytes = TCapacityProfile::ReservedOtherCompositionBytes;

    static constexpr std::size_t TotalMeshOwnedObjectBytes =
        MeshPrincipalBytes + ClockCoordinationBytes + DeliveryAcknowledgementBytes +
        InboundOwnedPoolBytes + ControlOwnedPoolBytes + ApplicationOwnedPoolBytes +
        InboundRelayPlaneBytes + OutboundRelayPlaneBytes + RelayCapacityProfileBytes +
        SecurityAuthorityBytes + RuntimeWorkerObjectBytes + OtherCompositionBytes;

    static constexpr std::size_t TotalMeshReservedBytes = TotalMeshOwnedObjectBytes + TaskStackBytes;
};

} // namespace ESPressio::Mesh
