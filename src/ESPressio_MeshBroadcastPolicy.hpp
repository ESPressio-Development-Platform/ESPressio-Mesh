#pragma once

#include <array>
#include <cstddef>
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

/// <summary>Frozen generic-broadcast semantic policy plus the protected Mesh/Radio service selected by composition.</summary>
struct MeshBroadcastBindingDescriptor final {
    MeshBroadcastBindingPolicy Policy{};
    MeshRelayServiceClass Service{MeshRelayServiceClass::BestEffort};

    constexpr bool IsValid() const noexcept {
        return ValidateMeshBroadcastPolicy(Policy)==MeshBroadcastPolicyDisposition::Permitted&&
               IsMeshRelayServiceClass(Service);
    }
};

enum class MeshBroadcastBindingRegistrationResult : std::uint8_t {
    Registered=0,
    FamilyAlreadyRegistered,
    Frozen,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed family-keyed broadcast binding table. Configuration is mutable only before Freeze(); runtime lookups are read-only.
/// </summary>
template<std::size_t TCapacity>
class MeshBroadcastBindingTable final {
    static_assert(TCapacity>0,"Mesh broadcast binding capacity must be finite and non-zero");
    struct Slot final { MeshBroadcastBindingDescriptor Descriptor{}; bool Occupied{false}; };
    std::array<Slot,TCapacity> _slots{};
    std::size_t _size{0};
    bool _frozen{false};
public:
    static constexpr std::size_t MaximumSize() noexcept { return TCapacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Frozen() const noexcept { return _frozen; }

    MeshBroadcastBindingRegistrationResult Register(const MeshBroadcastBindingDescriptor& descriptor) noexcept {
        if(_frozen) return MeshBroadcastBindingRegistrationResult::Frozen;
        if(!descriptor.IsValid()) return MeshBroadcastBindingRegistrationResult::Invalid;
        for(const auto& slot:_slots)
            if(slot.Occupied&&slot.Descriptor.Policy.Family==descriptor.Policy.Family)
                return MeshBroadcastBindingRegistrationResult::FamilyAlreadyRegistered;
        for(auto& slot:_slots){
            if(slot.Occupied) continue;
            slot.Descriptor=descriptor;slot.Occupied=true;++_size;
            return MeshBroadcastBindingRegistrationResult::Registered;
        }
        return MeshBroadcastBindingRegistrationResult::ResourceUnavailable;
    }

    bool Freeze() noexcept {
        if(_frozen) return false;
        _frozen=true;
        return true;
    }

    const MeshBroadcastBindingDescriptor* Find(Primitive::PrimitiveFamilyId family) const noexcept {
        if(!_frozen||!Primitive::FamilyIds::IsUsable(family)) return nullptr;
        for(const auto& slot:_slots)
            if(slot.Occupied&&slot.Descriptor.Policy.Family==family) return &slot.Descriptor;
        return nullptr;
    }
};

} // namespace ESPressio::Mesh
