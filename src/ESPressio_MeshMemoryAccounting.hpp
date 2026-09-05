#pragma once

#include <cstddef>

#include <ESPressio_RadioTransport.hpp>

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
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_MeshCapacityProfile.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
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
/// The principal total excludes task stacks, RadioTransport/provider storage, variable payload/reassembly/control buffers,
/// application objects, security-authority private state, delivery-acknowledgement storage and clock-coordination storage.
/// The latter two are reported by explicit templated helpers because the architecture intentionally leaves acknowledgement
/// capacity and the clock-quality representation to the composition root. Those terms must be added separately for a
/// whole-device budget. ApplicationTransmissionBytes accounts only for frozen recipient/outcome metadata; immutable shared
/// payload backing remains a separate variable-capacity term.
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
        TrafficGovernorBytes +
        ApplicationTransmissionBytes +
        SecuritySessionBytes;

    /// <summary>Returns fixed storage for the composition-selected clock-quality representation at MaxMeshNodes.</summary>
    template<typename TClockQuality>
    static constexpr std::size_t ClockCoordinationBytes() noexcept {
        return sizeof(ClockCoordinationTable<TClockQuality>);
    }

    /// <summary>Returns the additional fixed storage selected by an explicit delivery-acknowledgement capacity.</summary>
    template<std::size_t AcknowledgementCapacity>
    static constexpr std::size_t DeliveryAcknowledgementBytes() noexcept {
        static_assert(AcknowledgementCapacity > 0, "Acknowledgement capacity must be explicitly finite and non-zero.");
        return sizeof(DeliveryAcknowledgementTracker<AcknowledgementCapacity>);
    }
};

/// <summary>Whole-device static storage accounting for an explicitly selected platform profile.</summary>
/// <remarks>
/// TSecurityAuthority must be the concrete bounded security-composition owner used by the build, not its interface type;
/// it includes provider, signer/identity storage and pending-handshake records when those are separately composed.
/// RadioTransportBytes includes its fixed reassembly arrays and all other retained Radio transport state. Task stacks
/// and composition-owned storage not represented by concrete types remain explicit profile terms.
/// </remarks>
template<
    typename TTopologyCharacteristics,
    typename TClockQuality,
    std::size_t AcknowledgementCapacity,
    typename TCapacityProfile,
    typename TSecurityAuthority,
    typename TRadioTransport = Radio::RadioTransport
>
struct MeshWholeDeviceMemoryAccounting final {
    static_assert(TRadioTransport::ReassemblyCapacity == TCapacityProfile::RadioReassemblies,
                  "The selected profile does not match the build-selected Radio reassembly count.");
    static_assert(TRadioTransport::LogicalTransferCapacityBytes == TCapacityProfile::RadioLogicalTransferBytes,
                  "The selected profile does not match the build-selected Radio logical-transfer byte capacity.");

    using Fixed = MeshFixedMemoryAccounting<TTopologyCharacteristics>;
    static constexpr std::size_t MeshPrincipalBytes = Fixed::PrincipalFixedCardinalityBytes;
    static constexpr std::size_t ClockCoordinationBytes = Fixed::template ClockCoordinationBytes<TClockQuality>();
    static constexpr std::size_t DeliveryAcknowledgementBytes =
        Fixed::template DeliveryAcknowledgementBytes<AcknowledgementCapacity>();
    static constexpr std::size_t InboundOwnedPoolBytes = sizeof(typename TCapacityProfile::InboundDeliveryPool);
    static constexpr std::size_t ControlOwnedPoolBytes = sizeof(typename TCapacityProfile::ControlFramePool);
    static constexpr std::size_t ApplicationOwnedPoolBytes = sizeof(typename TCapacityProfile::ApplicationPayloadPool);
    static constexpr std::size_t RadioTransportBytes = sizeof(TRadioTransport);
    static constexpr std::size_t RadioReassemblyPayloadBytes = TRadioTransport::ReassemblyPayloadCapacityBytes;
    static constexpr std::size_t SecurityAuthorityBytes = sizeof(TSecurityAuthority);
    static constexpr std::size_t TaskStackBytes = TCapacityProfile::ReservedTaskStackBytes;
    static constexpr std::size_t OtherCompositionBytes = TCapacityProfile::ReservedOtherCompositionBytes;

    static constexpr std::size_t TotalAccountedBytes =
        MeshPrincipalBytes + ClockCoordinationBytes + DeliveryAcknowledgementBytes +
        InboundOwnedPoolBytes + ControlOwnedPoolBytes + ApplicationOwnedPoolBytes +
        RadioTransportBytes + SecurityAuthorityBytes + TaskStackBytes + OtherCompositionBytes;
};

} // namespace ESPressio::Mesh
