#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Mesh::Limits {

/// <summary>Maximum authenticated members represented by the default Mesh configuration.</summary>
inline constexpr std::size_t MaxMeshNodes = 32;
/// <summary>Maximum Radios advertised/retained for one member.</summary>
inline constexpr std::size_t MaxRadiosPerNode = 4;
/// <summary>Maximum application-defined Groups self-declared by one member.</summary>
inline constexpr std::size_t MaxGroupsPerNode = 8;
/// <summary>Maximum simultaneously registered primitive-family receivers.</summary>
inline constexpr std::size_t MaxPrimitiveReceivers = 8;
/// <summary>Maximum accepted application transmission aggregates.</summary>
inline constexpr std::size_t MaxActiveApplicationTransmissions = 8;
/// <summary>Maximum directed links retained by the default link-state topology.</summary>
inline constexpr std::size_t MaxTopologyLinks = 96;
/// <summary>Maximum hops retained in one resolved route.</summary>
inline constexpr std::size_t MaxRouteHops = 16;
/// <summary>Initial forwarding hop limit applied by the default delivery configuration.</summary>
inline constexpr std::uint8_t DefaultHopLimit = 16;
/// <summary>Maximum cached resolved routes.</summary>
inline constexpr std::size_t MaxRouteCacheEntries = 32;
/// <summary>Maximum active inbound complete Mesh deliveries.</summary>
inline constexpr std::size_t MaxActiveInboundDeliveries = 8;
/// <summary>Maximum pending neighbour candidates awaiting admission work.</summary>
inline constexpr std::size_t MaxPendingNeighbourCandidates = 8;
/// <summary>Maximum active liveness probes.</summary>
inline constexpr std::size_t MaxActiveLivenessProbes = 4;
/// <summary>Maximum concurrent inbound authentication operations.</summary>
inline constexpr std::size_t MaxActiveInboundAuthentications = 4;
/// <summary>Maximum recipients frozen into one sender-local selective multicast aggregate.</summary>
inline constexpr std::size_t MaxRecipientsPerTransmission = 32;
/// <summary>Maximum compact historical membership tombstones, independent of active membership capacity.</summary>
inline constexpr std::size_t MaxMembershipTombstones = 64;
/// <summary>Default sequence positions represented by one delivery deduplication window.</summary>
inline constexpr std::size_t DeduplicationWindowBits = 128;
/// <summary>Maximum same-route attempts permitted by the default retry policy.</summary>
inline constexpr std::size_t MaxSameRouteAttempts = 3;
/// <summary>Maximum distinct routes attempted by the default route-attempt policy.</summary>
inline constexpr std::size_t MaxRoutesAttempted = 4;

/// <summary>Protected capacity reserved for infrastructure responses.</summary>
inline constexpr std::size_t InfrastructureResponseCapacity = 8;
/// <summary>Protected capacity reserved for clock-control work.</summary>
inline constexpr std::size_t ClockControlCapacity = 4;
/// <summary>Protected capacity reserved for general Mesh control work.</summary>
inline constexpr std::size_t GeneralControlCapacity = 8;
/// <summary>Capacity reserved for accepted application transmission aggregates.</summary>
inline constexpr std::size_t ApplicationTransmissionCapacity = 8;

/// <summary>Default retention for a complete locally unreachable member record.</summary>
inline constexpr std::uint64_t UnreachableMemberRetentionMilliseconds = 60'000ULL;
/// <summary>Default compact membership tombstone retention.</summary>
inline constexpr std::uint64_t MembershipTombstoneRetentionMilliseconds = 300'000ULL;

static_assert(MaxRecipientsPerTransmission <= MaxMeshNodes,
              "A frozen selective-multicast recipient set cannot exceed active membership capacity.");
static_assert(DeduplicationWindowBits == 128,
              "The baseline deduplication bitmap is architecturally fixed at 128 sequence positions.");

} // namespace ESPressio::Mesh::Limits
