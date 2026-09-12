#pragma once

#include <cstdint>

#include <ESPressio_PrimitiveFamilyRegistry.hpp>
#include <ESPressio_PrimitivePolicy.hpp>

#include "ESPressio_MeshRelayCapacity.hpp"

namespace ESPressio::Mesh {

struct MeshBroadcastBindingPolicy final {
    Primitive::PrimitiveFamilyId Family{Primitive::FamilyIds::Invalid};
    Primitive::PrimitivePolicyDescriptor Delivery{};
    bool HasResponseRelationship{false};
    bool RequiresSessionState{false};
};

enum class MeshBroadcastPolicyDisposition : std::uint8_t {
    Permitted=0,
    Invalid,
    RequiresRemoteEvidence,
    NotOccurrenceDelivery,
    ResponseBearingCommand,
    StatefulOrSessionBased,
    StateBroadcastUnsupported
};

/// <summary>Family-neutral gate for the locked NoRemoteEvidence-only generic Mesh broadcast surface.</summary>
constexpr MeshBroadcastPolicyDisposition ValidateMeshBroadcastPolicy(const MeshBroadcastBindingPolicy& policy) noexcept {
    if(!Primitive::FamilyIds::IsUsable(policy.Family)||policy.Family==Primitive::FamilyIds::MeshControl)
        return MeshBroadcastPolicyDisposition::Invalid;
    if(policy.Delivery.Category!=1U) return MeshBroadcastPolicyDisposition::NotOccurrenceDelivery;
    if(policy.Delivery.Evidence!=0U) return MeshBroadcastPolicyDisposition::RequiresRemoteEvidence;
    if(policy.Family==Primitive::FamilyIds::State) return MeshBroadcastPolicyDisposition::StateBroadcastUnsupported;
    if(policy.RequiresSessionState) return MeshBroadcastPolicyDisposition::StatefulOrSessionBased;
    if(policy.Family==Primitive::FamilyIds::Command&&policy.HasResponseRelationship)
        return MeshBroadcastPolicyDisposition::ResponseBearingCommand;
    return MeshBroadcastPolicyDisposition::Permitted;
}

template<class TPolicy>
constexpr MeshBroadcastBindingPolicy MakeMeshBroadcastBindingPolicy(
    Primitive::PrimitiveFamilyId family,
    bool hasResponseRelationship=false,
    bool requiresSessionState=false) noexcept {
    return {family,Primitive::PrimitivePolicyContract<TPolicy>::Descriptor(),hasResponseRelationship,requiresSessionState};
}

/// <summary>
/// Trusted immutable per-operation descriptor emitted by a frozen MeshAdapters Type binding before network admission.
/// </summary>
/// <remarks>
/// Service is intentionally not fixed per PrimitiveFamilyId: different Types in one family may map to different neutral
/// service classes. Mesh core never accepts a free-form application priority; the adapter binding supplies this descriptor.
/// </remarks>
struct MeshBroadcastSubmissionPolicy final {
    MeshBroadcastBindingPolicy Broadcast{};
    MeshRelayServiceClass Service{MeshRelayServiceClass::BestEffort};

    constexpr bool IsValid() const noexcept {
        return ValidateMeshBroadcastPolicy(Broadcast)==MeshBroadcastPolicyDisposition::Permitted&&
               IsMeshRelayServiceClass(Service);
    }
};

template<class TPolicy>
constexpr MeshBroadcastSubmissionPolicy MakeMeshBroadcastSubmissionPolicy(
    Primitive::PrimitiveFamilyId family,
    MeshRelayServiceClass service,
    bool hasResponseRelationship=false,
    bool requiresSessionState=false) noexcept {
    return {MakeMeshBroadcastBindingPolicy<TPolicy>(family,hasResponseRelationship,requiresSessionState),service};
}

} // namespace ESPressio::Mesh
