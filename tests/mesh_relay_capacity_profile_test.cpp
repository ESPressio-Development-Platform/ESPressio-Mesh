#include <cassert>
#include <cstddef>

#include "ESPressio_MeshRelayCapacityProfile.hpp"

using namespace ESPressio::Mesh;

static constexpr MeshRelayCapacityProfile Required(){
    MeshRelayCapacityProfile profile{};
    for(std::size_t i=0;i<MeshRelayServiceClassCount;++i){
        profile.InboundPrivate[i]={2,256};
        profile.OutboundPrivate[i]={2,256};
    }
    profile.InboundSharedOverflowRecords=2;
    profile.InboundSharedOverflowFittingBytes=512;
    profile.OutboundSharedOverflowRecords=2;
    profile.OutboundSharedOverflowFittingBytes=512;
    profile.UntrustedIngressRecords=2;
    profile.UntrustedIngressFittingBytes=512;
    profile.MaximumRelayPayloadBytes=4096;
    profile.DeferredLocalRecords=4;
    profile.SeenDedupEntries=64;
    profile.ExecutionContexts=1;
    profile.RequiredTaskStackBytes=4096;
    return profile;
}

int main(){
    constexpr auto required=Required();
    static_assert(required.IsValid());
    static_assert(required.Satisfies(required));
    static_assert(ValidateMeshRelayMembershipCompatibility(required,required)==MeshRelayMembershipCompatibility::Compatible);

    auto larger=required;
    for(auto& item:larger.InboundPrivate){++item.Records;item.FittingBytes+=128;}
    for(auto& item:larger.OutboundPrivate){++item.Records;item.FittingBytes+=128;}
    larger.InboundSharedOverflowRecords+=2;
    larger.InboundSharedOverflowFittingBytes+=128;
    larger.OutboundSharedOverflowRecords+=2;
    larger.OutboundSharedOverflowFittingBytes+=128;
    larger.UntrustedIngressRecords+=1;
    larger.UntrustedIngressFittingBytes+=128;
    larger.MaximumRelayPayloadBytes+=1024;
    larger.DeferredLocalRecords+=1;
    larger.SeenDedupEntries+=32;
    larger.ExecutionContexts+=1;
    larger.RequiredTaskStackBytes+=1024;
    assert(ValidateMeshRelayMembershipCompatibility(larger,required)==MeshRelayMembershipCompatibility::Compatible);

    // Every declared minimum is a configuration compatibility boundary, not a live free-space hint.
    for(std::size_t service=0;service<MeshRelayServiceClassCount;++service){
        auto candidate=required;candidate.InboundPrivate[service].Records=1;
        assert(ValidateMeshRelayMembershipCompatibility(candidate,required)==MeshRelayMembershipCompatibility::RelayCapacityInsufficient);
        candidate=required;candidate.InboundPrivate[service].FittingBytes=128;
        assert(ValidateMeshRelayMembershipCompatibility(candidate,required)==MeshRelayMembershipCompatibility::RelayCapacityInsufficient);
        candidate=required;candidate.OutboundPrivate[service].Records=1;
        assert(ValidateMeshRelayMembershipCompatibility(candidate,required)==MeshRelayMembershipCompatibility::RelayCapacityInsufficient);
        candidate=required;candidate.OutboundPrivate[service].FittingBytes=128;
        assert(ValidateMeshRelayMembershipCompatibility(candidate,required)==MeshRelayMembershipCompatibility::RelayCapacityInsufficient);
    }

    auto candidate=required;candidate.InboundSharedOverflowRecords=1;
    assert(!candidate.Satisfies(required));
    candidate=required;candidate.OutboundSharedOverflowFittingBytes=256;
    assert(!candidate.Satisfies(required));
    candidate=required;candidate.UntrustedIngressRecords=1;
    assert(!candidate.Satisfies(required));
    candidate=required;candidate.MaximumRelayPayloadBytes=2048;
    assert(!candidate.Satisfies(required));
    candidate=required;candidate.DeferredLocalRecords=3;
    assert(!candidate.Satisfies(required));
    candidate=required;candidate.SeenDedupEntries=63;
    assert(!candidate.Satisfies(required));
    candidate=required;candidate.ExecutionContexts=0;
    assert(ValidateMeshRelayMembershipCompatibility(candidate,required)==MeshRelayMembershipCompatibility::LocalProfileInvalid);
    candidate=required;candidate.RequiredTaskStackBytes=2048;
    assert(!candidate.Satisfies(required));

    auto invalidRequired=required;invalidRequired.UntrustedIngressRecords=0;
    assert(ValidateMeshRelayMembershipCompatibility(required,invalidRequired)==MeshRelayMembershipCompatibility::RequiredProfileInvalid);

    return 0;
}
