#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshRelayCapacity.hpp"

namespace ESPressio::Mesh {

/// <summary>One protected relay-service minimum advertised/required by Mesh membership compatibility.</summary>
struct MeshRelayServiceCapacityRequirement final {
    std::size_t Records{0};
    std::size_t FittingBytes{0};

    constexpr bool IsValid() const noexcept { return Records != 0 && FittingBytes != 0; }
    constexpr bool Satisfies(const MeshRelayServiceCapacityRequirement& required) const noexcept {
        return IsValid() && required.IsValid() && Records >= required.Records && FittingBytes >= required.FittingBytes;
    }
};

/// <summary>Immutable neutral minimum relay-resource configuration used by membership compatibility.</summary>
/// <remarks>
/// This profile is configuration capability, never live free-space telemetry. Temporary runtime saturation is expressed
/// by M1/Q1 admission results. A node whose configured profile cannot satisfy the Mesh-required profile is permanently
/// incompatible and must not enter membership. SharedOverflow is opportunistic but its configured reserve is still part
/// of compatibility when the Mesh policy requires it. DeferredLocal and seen/dedup counts are explicit because the M2
/// lifecycle must never discover mandatory storage only after a broadcast has already committed network forwarding.
/// </remarks>
struct MeshRelayCapacityProfile final {
    std::array<MeshRelayServiceCapacityRequirement,MeshRelayServiceClassCount> InboundPrivate{};
    std::array<MeshRelayServiceCapacityRequirement,MeshRelayServiceClassCount> OutboundPrivate{};

    std::size_t InboundSharedOverflowRecords{0};
    std::size_t InboundSharedOverflowFittingBytes{0};
    std::size_t OutboundSharedOverflowRecords{0};
    std::size_t OutboundSharedOverflowFittingBytes{0};

    std::size_t UntrustedIngressRecords{0};
    std::size_t UntrustedIngressFittingBytes{0};
    std::size_t MaximumRelayPayloadBytes{0};
    std::size_t DeferredLocalRecords{0};
    std::size_t SeenDedupEntries{0};
    std::size_t ExecutionContexts{0};
    std::size_t RequiredTaskStackBytes{0};

    constexpr bool IsValid() const noexcept {
        for(const auto& requirement:InboundPrivate) if(!requirement.IsValid()) return false;
        for(const auto& requirement:OutboundPrivate) if(!requirement.IsValid()) return false;
        if(MaximumRelayPayloadBytes==0 || SeenDedupEntries==0 || ExecutionContexts==0 || RequiredTaskStackBytes==0)
            return false;
        const bool inboundSharedValid=(InboundSharedOverflowRecords==0&&InboundSharedOverflowFittingBytes==0)||
            (InboundSharedOverflowRecords!=0&&InboundSharedOverflowFittingBytes!=0);
        const bool outboundSharedValid=(OutboundSharedOverflowRecords==0&&OutboundSharedOverflowFittingBytes==0)||
            (OutboundSharedOverflowRecords!=0&&OutboundSharedOverflowFittingBytes!=0);
        const bool quarantineValid=UntrustedIngressRecords!=0&&UntrustedIngressFittingBytes!=0;
        return inboundSharedValid&&outboundSharedValid&&quarantineValid;
    }

    constexpr bool Satisfies(const MeshRelayCapacityProfile& required) const noexcept {
        if(!IsValid()||!required.IsValid()) return false;
        for(std::size_t i=0;i<MeshRelayServiceClassCount;++i){
            if(!InboundPrivate[i].Satisfies(required.InboundPrivate[i]) ||
               !OutboundPrivate[i].Satisfies(required.OutboundPrivate[i])) return false;
        }
        return InboundSharedOverflowRecords>=required.InboundSharedOverflowRecords &&
               InboundSharedOverflowFittingBytes>=required.InboundSharedOverflowFittingBytes &&
               OutboundSharedOverflowRecords>=required.OutboundSharedOverflowRecords &&
               OutboundSharedOverflowFittingBytes>=required.OutboundSharedOverflowFittingBytes &&
               UntrustedIngressRecords>=required.UntrustedIngressRecords &&
               UntrustedIngressFittingBytes>=required.UntrustedIngressFittingBytes &&
               MaximumRelayPayloadBytes>=required.MaximumRelayPayloadBytes &&
               DeferredLocalRecords>=required.DeferredLocalRecords &&
               SeenDedupEntries>=required.SeenDedupEntries &&
               ExecutionContexts>=required.ExecutionContexts &&
               RequiredTaskStackBytes>=required.RequiredTaskStackBytes;
    }
};

enum class MeshRelayMembershipCompatibility : std::uint8_t {
    Compatible=0,
    LocalProfileInvalid,
    RequiredProfileInvalid,
    RelayCapacityInsufficient
};

/// <summary>Bounded configuration-only membership compatibility check; never inspects runtime occupancy.</summary>
constexpr MeshRelayMembershipCompatibility ValidateMeshRelayMembershipCompatibility(
    const MeshRelayCapacityProfile& local,
    const MeshRelayCapacityProfile& required) noexcept {
    if(!local.IsValid()) return MeshRelayMembershipCompatibility::LocalProfileInvalid;
    if(!required.IsValid()) return MeshRelayMembershipCompatibility::RequiredProfileInvalid;
    return local.Satisfies(required)
        ? MeshRelayMembershipCompatibility::Compatible
        : MeshRelayMembershipCompatibility::RelayCapacityInsufficient;
}

} // namespace ESPressio::Mesh
