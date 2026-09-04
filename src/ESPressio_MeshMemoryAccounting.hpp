#pragma once

#include <cstddef>

#include "ESPressio_AdmissionResources.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_InboundDeliveryReservations.hpp"
#include "ESPressio_LivenessProbeReservations.hpp"
#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MembershipTombstoneTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"
#include "ESPressio_RouteCache.hpp"
#include "ESPressio_TopologyGraphStore.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Target-native byte accounting for the principal fixed/cardinality Mesh stores whose capacities are already frozen.
/// </summary>
/// <remarks>
/// Values are expressed with sizeof so they reflect the actual compiler ABI of the target being built (including pointer
/// width, alignment and the application-selected topology-characteristics representation). This deliberately does not
/// pretend that a host x86-64 measurement is an ESP32 measurement.
///
/// The total excludes task stacks, RadioTransport/provider storage, variable payload/reassembly/control buffers,
/// application objects, security-authority private state, and delivery-acknowledgement storage because the architecture
/// intentionally leaves acknowledgement capacity to the composition root. Those terms must be added separately for a
/// whole-device budget.
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

    /// <summary>Sum of the principal frozen-capacity stores represented above.</summary>
    static constexpr std::size_t PrincipalFixedCardinalityBytes =
        AuthenticatedMembershipBytes +
        MembershipLivenessBytes +
        MembershipTombstoneBytes +
        InboundDeliveryReservationBytes +
        PendingNeighbourCandidateBytes +
        InboundAuthenticationReservationBytes +
        LivenessProbeReservationBytes +
        AuthenticatedDirectPeerBindingBytes +
        TopologyGraphBytes +
        RouteCacheBytes +
        PrimitiveReceiverRegistryBytes +
        TrafficGovernorBytes;

    /// <summary>Returns the additional fixed storage selected by an explicit delivery-acknowledgement capacity.</summary>
    template<std::size_t AcknowledgementCapacity>
    static constexpr std::size_t DeliveryAcknowledgementBytes() noexcept {
        static_assert(AcknowledgementCapacity > 0, "Acknowledgement capacity must be explicitly finite and non-zero.");
        return sizeof(DeliveryAcknowledgementTracker<AcknowledgementCapacity>);
    }
};

} // namespace ESPressio::Mesh
