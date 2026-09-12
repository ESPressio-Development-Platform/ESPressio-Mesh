#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshRadioSubmission.hpp"

using namespace ESPressio;

class FakeRuntime final {
public:
    Radio::RadioPeerHandle LastPeer{};
    Radio::RadioServiceProfile LastProfile{};
    Radio::RadioTransferTiming LastTiming{};
    std::array<std::uint8_t,16> LastPayload{};
    std::size_t LastPayloadBytes{0};
    Radio::RadioTransferId NextId{77};
    bool Accept{true};

    Radio::RadioTransferSubmissionResult SubmitPeer(
        Radio::RadioPeerHandle peer,
        const Radio::RadioServiceProfile& profile,
        const Radio::RadioTransferTiming& timing,
        const std::uint8_t* payload,
        std::size_t payloadBytes) noexcept {
        LastPeer=peer;LastProfile=profile;LastTiming=timing;LastPayloadBytes=payloadBytes;
        for(std::size_t i=0;i<payloadBytes&&i<LastPayload.size();++i) LastPayload[i]=payload[i];
        return {Accept?Radio::RadioSchedulerStatus::Success:Radio::RadioSchedulerStatus::ResourceUnavailable,
                Accept?NextId:Radio::RadioTransferId{0}};
    }
};

int main(){
    FakeRuntime runtime;
    const auto target=Mesh::MakeMeshRadioSubmissionTarget(runtime);
    assert(target);
    const Radio::RadioPeerHandle peer{1,2};
    const std::array<std::uint8_t,3> bytes{{4,5,6}};
    constexpr std::uint64_t expiry=9'000'000ULL;

    const std::array<Mesh::MeshRelayServiceClass,6> meshClasses{{
        Mesh::MeshRelayServiceClass::Infrastructure,Mesh::MeshRelayServiceClass::Clock,
        Mesh::MeshRelayServiceClass::Critical,Mesh::MeshRelayServiceClass::Responsive,
        Mesh::MeshRelayServiceClass::Convergent,Mesh::MeshRelayServiceClass::BestEffort}};
    const std::array<Radio::RadioServiceClass,6> radioClasses{{
        Radio::RadioServiceClass::Infrastructure,Radio::RadioServiceClass::Clock,
        Radio::RadioServiceClass::Critical,Radio::RadioServiceClass::Responsive,
        Radio::RadioServiceClass::Convergent,Radio::RadioServiceClass::BestEffort}};

    for(std::size_t i=0;i<meshClasses.size();++i){
        const auto result=target.SubmitPeer(peer,meshClasses[i],expiry,bytes.data(),bytes.size());
        assert(result.Accepted&&result.TransferId==runtime.NextId);
        assert(runtime.LastPeer==peer);
        assert(runtime.LastProfile.Class==radioClasses[i]);
        assert(runtime.LastProfile.RequiredDirectLinkEvidence==Radio::RadioDirectLinkEvidenceRequirement::TransmissionCompletion);
        assert(runtime.LastTiming.ExpiryNanoseconds==expiry);
        if(meshClasses[i]==Mesh::MeshRelayServiceClass::Clock){
            assert(runtime.LastProfile.DeadlineTreatment==Radio::RadioDeadlineTreatment::Promotable);
            assert(runtime.LastTiming.ServiceDeadlineNanoseconds==expiry);
        }else{
            assert(runtime.LastProfile.DeadlineTreatment==Radio::RadioDeadlineTreatment::ExpiryOnly);
            assert(runtime.LastTiming.ServiceDeadlineNanoseconds==0);
        }
        assert(runtime.LastPayloadBytes==bytes.size()&&runtime.LastPayload[0]==4&&runtime.LastPayload[2]==6);
    }

    runtime.Accept=false;
    const auto rejected=target.SubmitPeer(peer,Mesh::MeshRelayServiceClass::Responsive,expiry,bytes.data(),bytes.size());
    assert(!rejected.Accepted&&rejected.TransferId==0);
    assert(!target.SubmitPeer({},Mesh::MeshRelayServiceClass::Responsive,expiry,bytes.data(),bytes.size()).Accepted);
    assert(!target.SubmitPeer(peer,Mesh::MeshRelayServiceClass::Responsive,0,bytes.data(),bytes.size()).Accepted);
    return 0;
}
