#include <cassert>
#include <cstdint>
#include <type_traits>

#include "ESPressio_MeshBroadcastPolicy.hpp"

using namespace ESPressio;

struct FireAndForget final {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::NoRemoteEvidence;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1'000'000'000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=100'000'000ULL;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1'000'000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=10'000'000ULL;
};
struct AdmissionRequired final {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::ReportTerminalFailureToFamily;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1'000'000'000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=100'000'000ULL;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1'000'000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=10'000'000ULL;
};
struct LatestState final {
    using PolicyCategory=Primitive::StateConvergencePolicyTag;
    using RequiredEvidence=Primitive::NoRemoteEvidence;
    using Supersession=Primitive::LatestAuthoritativeValue;
    using ExhaustionDisposition=Primitive::DormantNeedsConvergence;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1'000'000'000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=100'000'000ULL;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1'000'000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=10'000'000ULL;
};

int main(){
    constexpr auto eventNoEvidence=Mesh::MakeMeshBroadcastBindingPolicy<FireAndForget>(Primitive::FamilyIds::Event);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(eventNoEvidence)==Mesh::MeshBroadcastPolicyDisposition::Permitted);

    constexpr auto eventAdmission=Mesh::MakeMeshBroadcastBindingPolicy<AdmissionRequired>(Primitive::FamilyIds::Event);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(eventAdmission)==Mesh::MeshBroadcastPolicyDisposition::RequiresRemoteEvidence);

    constexpr auto commandNoResponse=Mesh::MakeMeshBroadcastBindingPolicy<FireAndForget>(Primitive::FamilyIds::Command,false,false);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(commandNoResponse)==Mesh::MeshBroadcastPolicyDisposition::Permitted);

    constexpr auto commandResponse=Mesh::MakeMeshBroadcastBindingPolicy<FireAndForget>(Primitive::FamilyIds::Command,true,false);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(commandResponse)==Mesh::MeshBroadcastPolicyDisposition::ResponseBearingCommand);

    constexpr auto commandAdmission=Mesh::MakeMeshBroadcastBindingPolicy<AdmissionRequired>(Primitive::FamilyIds::Command,false,false);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(commandAdmission)==Mesh::MeshBroadcastPolicyDisposition::RequiresRemoteEvidence);

    constexpr auto fakeStatelessState=Mesh::MakeMeshBroadcastBindingPolicy<FireAndForget>(Primitive::FamilyIds::State,false,false);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(fakeStatelessState)==Mesh::MeshBroadcastPolicyDisposition::StateBroadcastUnsupported);

    constexpr auto stateConvergence=Mesh::MakeMeshBroadcastBindingPolicy<LatestState>(Primitive::FamilyIds::State,false,true);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(stateConvergence)==Mesh::MeshBroadcastPolicyDisposition::NotOccurrenceDelivery);

    constexpr auto privateStateful=Mesh::MakeMeshBroadcastBindingPolicy<FireAndForget>(Primitive::FamilyIds::ApplicationPrivateFirst,false,true);
    static_assert(Mesh::ValidateMeshBroadcastPolicy(privateStateful)==Mesh::MeshBroadcastPolicyDisposition::StatefulOrSessionBased);

    Mesh::MeshBroadcastBindingTable<2> bindings;
    const Mesh::MeshBroadcastBindingDescriptor eventBinding{eventNoEvidence,Mesh::MeshRelayServiceClass::Responsive};
    assert(bindings.Register(eventBinding)==Mesh::MeshBroadcastBindingRegistrationResult::Registered);
    assert(bindings.Register(eventBinding)==Mesh::MeshBroadcastBindingRegistrationResult::FamilyAlreadyRegistered);
    assert(bindings.Find(Primitive::FamilyIds::Event)==nullptr);
    assert(bindings.Freeze());
    assert(!bindings.Freeze());
    const auto* frozen=bindings.Find(Primitive::FamilyIds::Event);
    assert(frozen!=nullptr&&frozen->Service==Mesh::MeshRelayServiceClass::Responsive);
    assert(bindings.Register({commandNoResponse,Mesh::MeshRelayServiceClass::Critical})==
           Mesh::MeshBroadcastBindingRegistrationResult::Frozen);

    return 0;
}
