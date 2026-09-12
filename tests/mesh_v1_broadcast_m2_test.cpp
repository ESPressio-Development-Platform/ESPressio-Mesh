#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ESPressio_MeshV1BroadcastCoordinator.hpp"

using namespace ESPressio;

namespace {

System::DeviceIdentifier Device(std::uint8_t value){
    System::DeviceIdentifier::Storage bytes{};bytes.back()=value;return System::DeviceIdentifier{bytes};
}
Mesh::MembershipIncarnation Incarnation(std::uint8_t value){
    Mesh::MembershipIncarnation::Storage bytes{};bytes.back()=value;return Mesh::MembershipIncarnation{bytes};
}
Mesh::MeshIdentifier MeshId(std::uint8_t value){
    Mesh::MeshIdentifier::Storage bytes{};bytes.back()=value;return Mesh::MeshIdentifier{bytes};
}
Mesh::MeshSecuritySessionIdentifier SessionId(std::uint8_t value){
    Mesh::MeshSecuritySessionIdentifier id{};id.Value.fill(value);return id;
}

class Provider final : public Mesh::IMeshV1CryptographicProvider {
    std::array<bool,8> _sessions{};
    static Mesh::MeshAuthenticationTag Tag(Mesh::MeshSecurityTrafficPurpose purpose,std::uint64_t sequence,
        const std::uint8_t* aad,std::size_t aadBytes,const std::uint8_t* ciphertext,std::size_t ciphertextBytes) noexcept {
        Mesh::MeshAuthenticationTag tag{};tag.Value[0]=static_cast<std::uint8_t>(purpose);
        for(std::size_t i=0;i<8;++i) tag.Value[i+1]^=static_cast<std::uint8_t>(sequence>>(i*8U));
        for(std::size_t i=0;i<aadBytes;++i) tag.Value[i%tag.Value.size()]^=aad[i];
        for(std::size_t i=0;i<ciphertextBytes;++i) tag.Value[(i+5U)%tag.Value.size()]^=ciphertext[i];
        return tag;
    }
public:
    Mesh::MeshSecuritySessionHandle CreateSession(std::size_t slot) noexcept {
        assert(slot>0&&slot<_sessions.size()&&!_sessions[slot]);_sessions[slot]=true;
        return {static_cast<std::uint16_t>(slot),1};
    }
    bool GenerateEphemeralKey(Mesh::MeshEphemeralKeyHandle&,Mesh::MeshEphemeralPublicKey&) noexcept override{return false;}
    bool GenerateHandshakeNonce(Mesh::MeshHandshakeNonce&) noexcept override{return false;}
    bool Hash(const std::uint8_t* bytes,std::size_t size,Mesh::MeshSecurityDigest& digest) noexcept override {
        if(bytes==nullptr||size==0) return false;
        digest={};
        for(std::size_t i=0;i<size;++i) digest.Value[i%digest.Value.size()]^=static_cast<std::uint8_t>(bytes[i]+static_cast<std::uint8_t>(i));
        digest.Value[0]^=0xA5U;return true;
    }
    bool SignIdentityDigest(const System::DeviceIdentifier& device,const Mesh::MeshSecurityDigest& digest,
        Mesh::MeshIdentitySignature& signature) noexcept override {
        if(!device||!digest) return false;
        signature={};
        std::memcpy(signature.Value.data(),digest.Value.data(),digest.Value.size());
        std::memcpy(signature.Value.data()+digest.Value.size(),device.Bytes().data(),device.Bytes().size());
        signature.Value.back()=0x5AU;return true;
    }
    Mesh::MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(const System::DeviceIdentifier& device,
        const Mesh::MeshSecurityDigest& digest,const Mesh::MeshIdentitySignature& signature) noexcept override {
        Mesh::MeshIdentitySignature expected{};
        if(!SignIdentityDigest(device,digest,expected)) return Mesh::MeshIdentityVerificationResult::Invalid;
        return expected.Value==signature.Value?Mesh::MeshIdentityVerificationResult::Verified:
            Mesh::MeshIdentityVerificationResult::InvalidSignature;
    }
    bool DeriveSession(Mesh::MeshEphemeralKeyHandle,const Mesh::MeshEphemeralPublicKey&,const Mesh::MeshIdentifier&,
        const Mesh::MeshSecurityChannelBinding&,const System::DeviceIdentifier&,const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&,const System::DeviceIdentifier&,const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&,const Mesh::MeshSecurityDigest&,Mesh::MeshSecuritySessionRole,
        Mesh::MeshSecuritySessionHandle&,Mesh::MeshSecuritySessionIdentifier&) noexcept override{return false;}
    bool Seal(Mesh::MeshSecuritySessionHandle session,Mesh::MeshSecurityTrafficPurpose purpose,std::uint64_t sequence,
        const std::uint8_t* aad,std::size_t aadBytes,const std::uint8_t* plaintext,std::size_t plaintextBytes,
        std::uint8_t* ciphertext,Mesh::MeshAuthenticationTag& tag) noexcept override {
        if(!session||session.Slot>=_sessions.size()||!_sessions[session.Slot]||sequence==0||aad==nullptr||
           plaintext==nullptr||ciphertext==nullptr||plaintextBytes==0)return false;
        std::memcpy(ciphertext,plaintext,plaintextBytes);tag=Tag(purpose,sequence,aad,aadBytes,ciphertext,plaintextBytes);return true;
    }
    bool Open(Mesh::MeshSecuritySessionHandle session,Mesh::MeshSecurityTrafficPurpose purpose,std::uint64_t sequence,
        const std::uint8_t* aad,std::size_t aadBytes,const std::uint8_t* ciphertext,std::size_t ciphertextBytes,
        const Mesh::MeshAuthenticationTag& tag,std::uint8_t* plaintext) noexcept override {
        if(!session||session.Slot>=_sessions.size()||!_sessions[session.Slot]||sequence==0||aad==nullptr||
           ciphertext==nullptr||plaintext==nullptr||ciphertextBytes==0||
           Tag(purpose,sequence,aad,aadBytes,ciphertext,ciphertextBytes).Value!=tag.Value)return false;
        std::memcpy(plaintext,ciphertext,ciphertextBytes);return true;
    }
    bool ReleaseEphemeralKey(Mesh::MeshEphemeralKeyHandle) noexcept override{return false;}
    bool ReleaseSession(Mesh::MeshSecuritySessionHandle session) noexcept override {
        if(!session||session.Slot>=_sessions.size()||!_sessions[session.Slot])return false;
        _sessions[session.Slot]=false;return true;
    }
    void ResetForControlledShutdown() noexcept override{_sessions={};}
};

class Receiver final : public Mesh::IPrimitiveReceiver {
public:
    Primitive::PrimitiveAdmissionDisposition Next{Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable};
    std::size_t Calls{0};
    Primitive::PrimitiveAdmissionDisposition Receive(const Mesh::MeshReceiveContext& context,
        Primitive::PrimitiveProtocolVersion version,Mesh::PrimitivePayloadView payload) noexcept override {
        assert(context.Broadcast&&context.IsValid()&&version==1&&payload.Size==3&&payload.Data[0]==4&&payload.Data[2]==6);
        ++Calls;return Next;
    }
};

struct CaptureRadio final {
    std::array<std::uint8_t,512> Packet{};
    std::size_t PacketBytes{0};
    Radio::RadioPeerHandle Peer{};
    Mesh::MeshRelayServiceClass Service{Mesh::MeshRelayServiceClass::BestEffort};
    std::uint64_t Expiry{0};
    std::size_t Calls{0};
    static Mesh::MeshRadioSubmissionResult Submit(void* context,Radio::RadioPeerHandle peer,
        Mesh::MeshRelayServiceClass service,std::uint64_t expiry,const std::uint8_t* bytes,std::size_t byteCount) noexcept {
        auto& self=*static_cast<CaptureRadio*>(context);
        if(!peer||bytes==nullptr||byteCount==0||byteCount>self.Packet.size())return {};
        self.Peer=peer;self.Service=service;self.Expiry=expiry;self.PacketBytes=byteCount;++self.Calls;
        std::memcpy(self.Packet.data(),bytes,byteCount);return {true,77};
    }
    Mesh::MeshRadioSubmissionTarget Target() noexcept{return {this,&Submit};}
};

struct FireAndForget final {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::NoRemoteEvidence;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=500'000'000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=100'000'000ULL;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1'000'000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=10'000'000ULL;
};

} // namespace

int main(){
    constexpr std::size_t Memberships=4;
    constexpr std::size_t Bindings=4;
    constexpr std::size_t Sessions=4;
    const auto mesh=MeshId(9);const auto deviceA=Device(1);const auto deviceB=Device(2);
    const auto incA=Incarnation(1);const auto incB=Incarnation(2);
    const Radio::RadioPeerHandle peerAB{1,1};

    Provider crypto;
    auto providerA=crypto.CreateSession(1);auto providerB=crypto.CreateSession(2);
    Mesh::AuthenticatedMembershipTable<Memberships> membersA,membersB;
    assert(membersA.UpsertAuthenticated(deviceB,incB,Mesh::MembershipState::Active,Mesh::ReachabilityState::Reachable)==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(membersB.UpsertAuthenticated(deviceA,incA,Mesh::MembershipState::Active,Mesh::ReachabilityState::Reachable)==
           Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Mesh::AuthenticatedDirectPeerBindingTable<Bindings> bindingsA,bindingsB;
    assert(bindingsA.Bind({deviceB,incB,1,peerAB})==Mesh::DirectPeerBindingResult::Bound);

    Mesh::MeshSecuritySessionTable<Sessions> sessionsA,sessionsB;
    Mesh::MeshSecuritySessionRecordHandle handleA{},handleB{};
    assert(sessionsA.Install(deviceB,incB,SessionId(7),providerA,crypto,handleA));
    assert(sessionsB.Install(deviceA,incA,SessionId(7),providerB,crypto,handleB));

    Mesh::PrimitiveReceiverRegistry<Mesh::Limits::MaxPrimitiveReceivers> receiversA,receiversB;
    Receiver receiverB;
    Mesh::PrimitiveReceiverHandle receiverHandle{};
    assert(receiversB.Register({Primitive::FamilyIds::Event,{1,1},{},Mesh::PrimitiveReceiverExposure::Advertised},
        receiverB,receiverHandle)==Mesh::PrimitiveReceiverRegistrationResult::Registered);

    CaptureRadio radioA,radioB;
    Mesh::MeshV1FrameWorkspace<512,512> workspaceA,workspaceB;
    Mesh::MeshMessageIdGenerator idsA,idsB;
    Mesh::MeshV1BroadcastCoordinator<512,512,4,Memberships,Bindings,Sessions> coordinatorA(
        membersA,bindingsA,sessionsA,crypto,receiversA,radioA.Target(),workspaceA,idsA,mesh,deviceA,incA);
    Mesh::MeshV1BroadcastCoordinator<512,512,4,Memberships,Bindings,Sessions> coordinatorB(
        membersB,bindingsB,sessionsB,crypto,receiversB,radioB.Target(),workspaceB,idsB,mesh,deviceB,incB);

    Mesh::MeshBroadcastFanoutPlan<4> planA;
    assert(planA.TryAdd({deviceB,incB,1,peerAB}));
    Mesh::MeshBroadcastFanoutPlan<4> emptyPlan;
    constexpr auto policy=Mesh::MakeMeshBroadcastSubmissionPolicy<FireAndForget>(
        Primitive::FamilyIds::Event,Mesh::MeshRelayServiceClass::Responsive);
    const std::array<std::uint8_t,3> payloadBytes{{4,5,6}};
    const auto payload=Mesh::ApplicationPayload::Borrowed(payloadBytes.data(),payloadBytes.size());

    const auto submitted=coordinatorA.Submit(policy,{Primitive::FamilyIds::Event,1},payload,
        1'000'000'000ULL,250,2,planA);
    assert(submitted.Disposition==Mesh::MeshV1BroadcastDisposition::Completed);
    assert(receiverB.Calls==0);
    assert(radioA.Calls==1&&radioA.Service==Mesh::MeshRelayServiceClass::Responsive&&radioA.PacketBytes!=0);

    const auto received=coordinatorB.Receive(radioA.Packet.data(),radioA.PacketBytes,1'010'000'000ULL,10,emptyPlan);
    assert(received.Disposition==Mesh::MeshV1BroadcastDisposition::DeferredLocal);
    assert(received.Network==Mesh::MeshBroadcastNetworkDisposition::Forwarded);
    assert(received.Admission==Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable);
    assert(receiverB.Calls==1);

    Mesh::MeshV1BroadcastHopHeader firstHop{};Mesh::MeshV1BroadcastHopView firstView{};
    assert(Mesh::MeshV1BroadcastFrameCodec::DecodeHop(radioA.Packet.data(),radioA.PacketBytes,firstHop,firstView));
    Mesh::MeshV1BroadcastHopHeader duplicateHop=firstHop;duplicateHop.Sequence=2;
    std::array<std::uint8_t,512> duplicatePacket{};
    assert(Mesh::MeshV1BroadcastFrameCodec::EncodeHopAuthenticatedHeader(duplicateHop,duplicatePacket.data(),radioA.PacketBytes));
    Mesh::MeshAuthenticationTag duplicateTag{};
    assert(crypto.Seal(providerA,Mesh::MeshSecurityTrafficPurpose::Hop,2,duplicatePacket.data(),
        Mesh::MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes,firstView.Ciphertext,firstView.CiphertextBytes,
        duplicatePacket.data()+Mesh::MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes,duplicateTag));
    std::memcpy(duplicatePacket.data()+radioA.PacketBytes-duplicateTag.Value.size(),duplicateTag.Value.data(),duplicateTag.Value.size());

    const auto duplicate=coordinatorB.Receive(duplicatePacket.data(),radioA.PacketBytes,1'020'000'000ULL,10,emptyPlan);
    assert(duplicate.Disposition==Mesh::MeshV1BroadcastDisposition::Duplicate);
    assert(receiverB.Calls==1);

    Primitive::PrimitiveAdmissionDisposition retryDisposition{};
    assert(coordinatorB.ServiceDeferredLocal(10,1'030'000'000ULL,retryDisposition)==Mesh::MeshDeferredLocalServiceResult::NoWork);
    receiverB.Next=Primitive::PrimitiveAdmissionDisposition::Accepted;
    assert(coordinatorB.ServiceDeferredLocal(11,1'030'000'000ULL,retryDisposition)==Mesh::MeshDeferredLocalServiceResult::Admitted);
    assert(retryDisposition==Primitive::PrimitiveAdmissionDisposition::Accepted&&receiverB.Calls==2);
    assert(coordinatorB.ServiceDeferredLocal(12,1'040'000'000ULL,retryDisposition)==Mesh::MeshDeferredLocalServiceResult::NoWork);

    coordinatorB.ShutdownVolatileBroadcastState();
    const auto afterShutdown=coordinatorB.Receive(radioA.Packet.data(),radioA.PacketBytes,1'050'000'000ULL,13,emptyPlan);
    assert(afterShutdown.Disposition==Mesh::MeshV1BroadcastDisposition::Invalid);
    return 0;
}
