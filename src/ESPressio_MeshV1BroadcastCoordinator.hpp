#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "ESPressio_ApplicationPayload.hpp"
#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MeshBroadcastLifecycle.hpp"
#include "ESPressio_MeshBroadcastPolicy.hpp"
#include "ESPressio_MeshMessageIdGenerator.hpp"
#include "ESPressio_MeshRadioSubmission.hpp"
#include "ESPressio_MeshRemainingResidence.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshV1BroadcastFrame.hpp"
#include "ESPressio_MeshV1FrameWorkspace.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"

namespace ESPressio::Mesh {

struct MeshBroadcastFanoutTarget final {
    System::DeviceIdentifier Neighbour{};
    MembershipIncarnation Incarnation{};
    RadioIdentifier LocalRadio{0U};
    Radio::RadioPeerHandle Peer{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Neighbour)&&static_cast<bool>(Incarnation)&&
               LocalRadio!=0U&&LocalRadio!=0xFFU&&static_cast<bool>(Peer);
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

template<std::size_t TCapacity=Limits::MaxMeshNodes>
class MeshBroadcastFanoutPlan final {
    static_assert(TCapacity>0,"Broadcast fan-out capacity must be non-zero");
    std::array<MeshBroadcastFanoutTarget,TCapacity> _targets{};
    std::size_t _size{0};
public:
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr const MeshBroadcastFanoutTarget* At(std::size_t index) const noexcept {
        return index<_size?&_targets[index]:nullptr;
    }
    void Clear() noexcept { _targets={};_size=0; }
    bool TryAdd(const MeshBroadcastFanoutTarget& target) noexcept {
        if(!target||_size>=TCapacity) return false;
        for(std::size_t index=0;index<_size;++index)
            if(_targets[index].Neighbour==target.Neighbour) return false;
        std::size_t insertion=_size;
        while(insertion>0&&target.Neighbour<_targets[insertion-1].Neighbour){
            _targets[insertion]=_targets[insertion-1];--insertion;
        }
        _targets[insertion]=target;++_size;return true;
    }
};

enum class MeshV1BroadcastDisposition : std::uint8_t {
    Completed=0,
    DeferredLocal,
    Duplicate,
    TooOld,
    DeadlineExpired,
    PolicyRejected,
    ResourceUnavailable,
    WorkspaceCapacityExceeded,
    SerializationFailed,
    SequenceExhausted,
    UnknownAuthenticatedSender,
    UnknownAuthenticatedSource,
    ReplayRejected,
    AuthenticationFailed,
    NotForLocalNode,
    Invalid
};

struct MeshV1BroadcastResult final {
    MeshV1BroadcastDisposition Disposition{MeshV1BroadcastDisposition::Invalid};
    MeshMessageId MessageId{0U};
    PrimitiveDispatchResult Dispatch{PrimitiveDispatchResult::Invalid};
    Primitive::PrimitiveAdmissionDisposition Admission{Primitive::PrimitiveAdmissionDisposition::Malformed};
    MeshBroadcastNetworkDisposition Network{MeshBroadcastNetworkDisposition::Invalid};
    MeshDeferredLocalStoreResult DeferredStore{MeshDeferredLocalStoreResult::Invalid};
    std::uint8_t FanoutAttempted{0U};
    std::uint8_t FanoutAccepted{0U};
};

/// <summary>
/// M2 generic broadcast coordinator: authenticated forward-once network state plus independent bounded local admission.
/// </summary>
/// <remarks>
/// Generic broadcast is NoRemoteEvidence-only. Source submission never feeds bytes back into the source family receiver.
/// Every next-hop copy enters managed Radio through MeshRadioSubmissionTarget; Mesh never calls a physical IRadio directly.
/// Receive commits authenticated Seen/Forwarded independently of local M1 admission. A retryable local disposition may
/// retain one immutable DeferredLocal copy only within both received remaining residence and the signed current-attempt
/// adapter-admission-wait ceiling. Deferred retry is externally wake/generation driven through ServiceDeferredLocal().
/// </remarks>
template<std::size_t TInnerWorkspaceBytes,
         std::size_t TPacketWorkspaceBytes,
         std::size_t TFanoutCapacity=Limits::MaxMeshNodes,
         std::size_t TMembershipCapacity=Limits::MaxMeshNodes,
         std::size_t TBindingCapacity=Limits::MaxTopologyLinks,
         std::size_t TSessionCapacity=Limits::MaxMeshNodes,
         std::size_t TNetworkSourceCapacity=Limits::MaxMeshNodes,
         std::size_t TDeferredLocalCapacity=Limits::MaxActiveInboundDeliveries,
         std::size_t TDeferredPayloadBytes=TInnerWorkspaceBytes>
class MeshV1BroadcastCoordinator final {
    AuthenticatedMembershipTable<TMembershipCapacity>& _memberships;
    DefaultMeshLivenessPolicy _livenessPolicy{};
    MembershipLivenessTracker<TMembershipCapacity> _liveness;
    const AuthenticatedDirectPeerBindingTable<TBindingCapacity>& _bindings;
    MeshSecuritySessionTable<TSessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    PrimitiveReceiverRegistry<Limits::MaxPrimitiveReceivers>& _receivers;
    MeshRadioSubmissionTarget _radio{};
    MeshV1FrameWorkspace<TInnerWorkspaceBytes,TPacketWorkspaceBytes>& _workspace;
    MeshMessageIdGenerator& _messageIds;
    MeshBroadcastNetworkStateTable<TNetworkSourceCapacity> _network{};
    MeshDeferredLocalTable<TDeferredLocalCapacity,TDeferredPayloadBytes> _deferred{};
    MeshIdentifier _mesh{};
    System::DeviceIdentifier _localDevice{};
    MembershipIncarnation _localIncarnation{};
    bool _accepting{true};

    bool CommitAuthenticatedHop(MeshSecuritySessionRecordHandle session,
                                const MeshV1BroadcastHopHeader& hop,
                                std::uint64_t monotonicMilliseconds) noexcept {
        if(!_sessions.CommitAuthenticatedInbound(session,MeshSecurityTrafficPurpose::Hop,hop.Sequence)) return false;
        (void)_liveness.ObserveAuthenticatedEvidence(hop.Sender,hop.SenderIncarnation,monotonicMilliseconds);
        return true;
    }

    void Fanout(const std::uint8_t* originPacket,
                std::size_t originPacketBytes,
                const MeshV1BroadcastOriginHeader& origin,
                const MeshBroadcastFanoutPlan<TFanoutCapacity>& plan,
                const System::DeviceIdentifier& previousSender,
                RemainingHopLimit hopLimit,
                std::uint32_t remainingResidenceMilliseconds,
                std::uint64_t localExpiryNanoseconds,
                MeshV1BroadcastResult& result) noexcept {
        if(hopLimit==0U||remainingResidenceMilliseconds==0U||localExpiryNanoseconds==0U||
           originPacket==nullptr||originPacketBytes>std::numeric_limits<std::uint16_t>::max()) return;
        const auto packetBytes=MeshV1BroadcastFrameCodec::HopPacketBytes(originPacketBytes);
        auto* packet=_workspace.Packet(packetBytes);
        if(packet==nullptr) return;

        for(std::size_t index=0;index<plan.Size();++index){
            const auto* target=plan.At(index);
            if(target==nullptr||target->Neighbour==previousSender||target->Neighbour==_localDevice) continue;
            ++result.FanoutAttempted;
            const auto* member=_memberships.FindExact(target->Neighbour,target->Incarnation);
            const auto* binding=_bindings.Resolve(target->LocalRadio,target->Neighbour,target->Incarnation);
            const auto session=_sessions.Find(target->Neighbour,target->Incarnation);
            if(member==nullptr||member->State!=MembershipState::Active||
               member->Reachability==ReachabilityState::Unreachable||binding==nullptr||binding->Peer!=target->Peer||!session) continue;
            const auto sequence=_sessions.IssueSequence(session,MeshSecurityTrafficPurpose::Hop);
            if(sequence==0U) continue;

            MeshV1BroadcastHopHeader hop{};
            hop.Mesh=_mesh;
            hop.Session=_sessions.Identifier(session);
            hop.Sequence=sequence;
            hop.Sender=_localDevice;
            hop.SenderIncarnation=_localIncarnation;
            hop.NextHop=target->Neighbour;
            hop.NextHopIncarnation=target->Incarnation;
            hop.Source=origin.Source;
            hop.SourceIncarnation=origin.SourceIncarnation;
            hop.MessageId=origin.MessageId;
            hop.RemainingResidenceMilliseconds=remainingResidenceMilliseconds;
            hop.HopLimit=hopLimit;
            hop.InnerFrameBytes=static_cast<std::uint16_t>(originPacketBytes);
            if(!MeshV1BroadcastFrameCodec::EncodeHopAuthenticatedHeader(hop,packet,packetBytes)) continue;
            MeshAuthenticationTag tag{};
            if(!_provider.Seal(_sessions.ProviderSession(session),MeshSecurityTrafficPurpose::Hop,sequence,
                    packet,MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes,
                    originPacket,originPacketBytes,
                    packet+MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes,tag)) continue;
            std::memcpy(packet+packetBytes-tag.Value.size(),tag.Value.data(),tag.Value.size());
            const auto submitted=_radio.SubmitPeer(binding->Peer,origin.RelayService,localExpiryNanoseconds,packet,packetBytes);
            if(submitted.Accepted) ++result.FanoutAccepted;
        }
    }

public:
    MeshV1BroadcastCoordinator(
        AuthenticatedMembershipTable<TMembershipCapacity>& memberships,
        const AuthenticatedDirectPeerBindingTable<TBindingCapacity>& bindings,
        MeshSecuritySessionTable<TSessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        PrimitiveReceiverRegistry<Limits::MaxPrimitiveReceivers>& receivers,
        MeshRadioSubmissionTarget radio,
        MeshV1FrameWorkspace<TInnerWorkspaceBytes,TPacketWorkspaceBytes>& workspace,
        MeshMessageIdGenerator& messageIds,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation) noexcept :
        _memberships(memberships),_liveness(_memberships,_livenessPolicy),_bindings(bindings),
        _sessions(sessions),_provider(provider),_receivers(receivers),_radio(radio),_workspace(workspace),
        _messageIds(messageIds),_mesh(mesh),_localDevice(localDevice),_localIncarnation(localIncarnation) {}

    MeshV1BroadcastResult Submit(
        const MeshBroadcastSubmissionPolicy& policy,
        ApplicationPrimitiveDescriptor primitive,
        const ApplicationPayload& payload,
        std::uint64_t monotonicNowNanoseconds,
        std::uint32_t remainingResidenceMilliseconds,
        RemainingHopLimit hopLimit,
        const MeshBroadcastFanoutPlan<TFanoutCapacity>& plan) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> workspaceReset(_workspace);
        MeshV1BroadcastResult result{};
        if(!_accepting||!_mesh||!_localDevice||!_localIncarnation||!_radio||!policy.IsValid()||!primitive||!payload||
           primitive.Family!=policy.Broadcast.Family||primitive.Version==0U||
           payload.Size()>std::numeric_limits<std::uint16_t>::max()||monotonicNowNanoseconds==0U||
           remainingResidenceMilliseconds==0U||hopLimit==0U) return result;

        if(remainingResidenceMilliseconds>std::numeric_limits<std::uint64_t>::max()/MeshNanosecondsPerMillisecond){
            result.Disposition=MeshV1BroadcastDisposition::PolicyRejected;return result;
        }
        const auto requestedResidence=static_cast<std::uint64_t>(remainingResidenceMilliseconds)*MeshNanosecondsPerMillisecond;
        if(policy.Broadcast.Delivery.MaximumResidenceNanoseconds==0U||
           requestedResidence>policy.Broadcast.Delivery.MaximumResidenceNanoseconds||
           policy.Broadcast.Delivery.MaximumAdapterAdmissionWaitNanoseconds>policy.Broadcast.Delivery.MaximumResidenceNanoseconds){
            result.Disposition=MeshV1BroadcastDisposition::PolicyRejected;return result;
        }
        std::uint64_t localExpiry=0U;
        if(!TryEstablishMeshLocalExpiry(monotonicNowNanoseconds,remainingResidenceMilliseconds,0U,localExpiry)){
            result.Disposition=MeshV1BroadcastDisposition::DeadlineExpired;return result;
        }

        const auto originBytes=MeshV1BroadcastFrameCodec::OriginPacketBytes(payload.Size());
        const auto hopBytes=MeshV1BroadcastFrameCodec::HopPacketBytes(originBytes);
        if(originBytes==0U||hopBytes==0U){result.Disposition=MeshV1BroadcastDisposition::Invalid;return result;}
        auto* originPacket=_workspace.Inner(originBytes);
        if(originPacket==nullptr||_workspace.Packet(hopBytes)==nullptr){
            result.Disposition=MeshV1BroadcastDisposition::WorkspaceCapacityExceeded;return result;
        }
        if(!_messageIds.TryIssue(result.MessageId)){
            result.Disposition=MeshV1BroadcastDisposition::SequenceExhausted;return result;
        }

        MeshV1BroadcastOriginHeader origin{};
        origin.Mesh=_mesh;
        origin.Source=_localDevice;
        origin.SourceIncarnation=_localIncarnation;
        origin.MessageId=result.MessageId;
        origin.RemainingResidenceMilliseconds=remainingResidenceMilliseconds;
        origin.MaximumAdapterAdmissionWaitNanoseconds=policy.Broadcast.Delivery.MaximumAdapterAdmissionWaitNanoseconds;
        origin.RelayService=policy.Service;
        origin.PrimitiveFamily=primitive.Family;
        origin.PrimitiveVersion=primitive.Version;
        origin.PayloadBytes=static_cast<std::uint16_t>(payload.Size());
        if(!MeshV1BroadcastFrameCodec::EncodeOriginAuthenticatedHeader(origin,originPacket,originBytes)||
           !payload.Read(0U,originPacket+MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes,payload.Size())){
            result.Disposition=MeshV1BroadcastDisposition::SerializationFailed;return result;
        }
        MeshSecurityDigest digest{};
        const auto signedBytes=MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes+payload.Size();
        if(!_provider.Hash(originPacket,signedBytes,digest)){
            result.Disposition=MeshV1BroadcastDisposition::AuthenticationFailed;return result;
        }
        MeshIdentitySignature signature{};
        if(!_provider.SignIdentityDigest(_localDevice,digest,signature)){
            result.Disposition=MeshV1BroadcastDisposition::AuthenticationFailed;return result;
        }
        std::memcpy(originPacket+signedBytes,signature.Value.data(),signature.Value.size());

        result.Network=_network.CommitAuthenticatedSeen(_localDevice,_localIncarnation,result.MessageId);
        if(result.Network!=MeshBroadcastNetworkDisposition::NewlySeen){
            result.Disposition=MeshV1BroadcastDisposition::ResourceUnavailable;return result;
        }
        Fanout(originPacket,originBytes,origin,plan,{},hopLimit,remainingResidenceMilliseconds,localExpiry,result);
        (void)_network.CommitForwarded(_localDevice,_localIncarnation,result.MessageId);
        result.Network=MeshBroadcastNetworkDisposition::Forwarded;
        result.Disposition=result.FanoutAccepted!=0U?MeshV1BroadcastDisposition::Completed:MeshV1BroadcastDisposition::ResourceUnavailable;
        return result;
    }

    MeshV1BroadcastResult Receive(
        const std::uint8_t* packet,
        std::size_t packetBytes,
        std::uint64_t monotonicNowNanoseconds,
        std::uint64_t admissionGeneration,
        const MeshBroadcastFanoutPlan<TFanoutCapacity>& plan) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> workspaceReset(_workspace);
        MeshV1BroadcastResult result{};
        MeshV1BroadcastHopHeader hop{};
        MeshV1BroadcastHopView hopView{};
        if(!_accepting||!_mesh||!_localDevice||!_localIncarnation||!_radio||monotonicNowNanoseconds==0U||
           !MeshV1BroadcastFrameCodec::DecodeHop(packet,packetBytes,hop,hopView)) return result;
        result.MessageId=hop.MessageId;
        if(hop.Mesh!=_mesh||hop.NextHop!=_localDevice||hop.NextHopIncarnation!=_localIncarnation){
            result.Disposition=MeshV1BroadcastDisposition::NotForLocalNode;return result;
        }
        auto* sender=_memberships.FindExact(hop.Sender,hop.SenderIncarnation);
        if(sender==nullptr||sender->State!=MembershipState::Active){
            result.Disposition=MeshV1BroadcastDisposition::UnknownAuthenticatedSender;return result;
        }
        const auto hopSession=_sessions.Find(hop.Sender,hop.SenderIncarnation);
        if(!hopSession||_sessions.Identifier(hopSession).Value!=hop.Session.Value||
           !_sessions.CanAcceptInbound(hopSession,MeshSecurityTrafficPurpose::Hop,hop.Sequence)){
            result.Disposition=MeshV1BroadcastDisposition::ReplayRejected;return result;
        }
        auto* originPacket=_workspace.Inner(hopView.CiphertextBytes);
        if(originPacket==nullptr||_workspace.Packet(packetBytes)==nullptr){
            result.Disposition=MeshV1BroadcastDisposition::WorkspaceCapacityExceeded;return result;
        }
        if(!_provider.Open(_sessions.ProviderSession(hopSession),MeshSecurityTrafficPurpose::Hop,hop.Sequence,
                hopView.AuthenticatedHeader,hopView.AuthenticatedHeaderBytes,
                hopView.Ciphertext,hopView.CiphertextBytes,hopView.Tag,originPacket)){
            result.Disposition=MeshV1BroadcastDisposition::AuthenticationFailed;return result;
        }

        MeshV1BroadcastOriginHeader origin{};
        MeshV1BroadcastOriginView originView{};
        if(!MeshV1BroadcastFrameCodec::DecodeOrigin(originPacket,hopView.CiphertextBytes,origin,originView)||
           origin.Mesh!=_mesh||origin.Source!=hop.Source||origin.SourceIncarnation!=hop.SourceIncarnation||
           origin.MessageId!=hop.MessageId||hop.RemainingResidenceMilliseconds>origin.RemainingResidenceMilliseconds){
            result.Disposition=MeshV1BroadcastDisposition::AuthenticationFailed;return result;
        }

        const auto monotonicMilliseconds=monotonicNowNanoseconds/MeshNanosecondsPerMillisecond;
        const bool localLoop=origin.Source==_localDevice&&origin.SourceIncarnation==_localIncarnation;
        if(localLoop){
            if(!CommitAuthenticatedHop(hopSession,hop,monotonicMilliseconds==0U?1U:monotonicMilliseconds)){
                result.Disposition=MeshV1BroadcastDisposition::ReplayRejected;return result;
            }
            result.Disposition=MeshV1BroadcastDisposition::Duplicate;return result;
        }

        auto* source=_memberships.FindExact(origin.Source,origin.SourceIncarnation);
        if(source==nullptr||source->State!=MembershipState::Active){
            if(!CommitAuthenticatedHop(hopSession,hop,monotonicMilliseconds==0U?1U:monotonicMilliseconds)){
                result.Disposition=MeshV1BroadcastDisposition::ReplayRejected;return result;
            }
            result.Disposition=MeshV1BroadcastDisposition::UnknownAuthenticatedSource;return result;
        }

        MeshSecurityDigest digest{};
        if(!_provider.Hash(originView.SignedBytes,originView.SignedByteCount,digest)){
            result.Disposition=MeshV1BroadcastDisposition::AuthenticationFailed;return result;
        }
        const auto verification=_provider.VerifyRegisteredIdentityDigest(origin.Source,digest,originView.Signature);
        if(verification==MeshIdentityVerificationResult::ResourceUnavailable){
            result.Disposition=MeshV1BroadcastDisposition::ResourceUnavailable;return result;
        }
        if(verification!=MeshIdentityVerificationResult::Verified){
            if(!CommitAuthenticatedHop(hopSession,hop,monotonicMilliseconds==0U?1U:monotonicMilliseconds)){
                result.Disposition=MeshV1BroadcastDisposition::ReplayRejected;return result;
            }
            result.Disposition=MeshV1BroadcastDisposition::AuthenticationFailed;return result;
        }
        if(!CommitAuthenticatedHop(hopSession,hop,monotonicMilliseconds==0U?1U:monotonicMilliseconds)){
            result.Disposition=MeshV1BroadcastDisposition::ReplayRejected;return result;
        }

        std::uint64_t networkExpiry=0U;
        if(!TryEstablishMeshLocalExpiry(monotonicNowNanoseconds,hop.RemainingResidenceMilliseconds,0U,networkExpiry)){
            result.Disposition=MeshV1BroadcastDisposition::DeadlineExpired;return result;
        }

        result.Network=_network.CommitAuthenticatedSeen(origin.Source,origin.SourceIncarnation,origin.MessageId);
        if(result.Network==MeshBroadcastNetworkDisposition::TooOld){
            result.Disposition=MeshV1BroadcastDisposition::TooOld;return result;
        }
        if(result.Network==MeshBroadcastNetworkDisposition::Forwarded){
            result.Disposition=MeshV1BroadcastDisposition::Duplicate;return result;
        }
        if(result.Network==MeshBroadcastNetworkDisposition::Invalid||
           result.Network==MeshBroadcastNetworkDisposition::SourceCapacityUnavailable){
            result.Disposition=MeshV1BroadcastDisposition::ResourceUnavailable;return result;
        }

        const bool firstSeen=result.Network==MeshBroadcastNetworkDisposition::NewlySeen;
        std::uint32_t forwardResidence=0U;
        if(hop.HopLimit>1U&&TryEncodeMeshRemainingResidenceMilliseconds(
                networkExpiry,monotonicNowNanoseconds,hop.RemainingResidenceMilliseconds,forwardResidence)){
            Fanout(originPacket,hopView.CiphertextBytes,origin,plan,hop.Sender,
                static_cast<RemainingHopLimit>(hop.HopLimit-1U),forwardResidence,networkExpiry,result);
        }
        (void)_network.CommitForwarded(origin.Source,origin.SourceIncarnation,origin.MessageId);
        result.Network=MeshBroadcastNetworkDisposition::Forwarded;

        if(!firstSeen){
            result.Disposition=MeshV1BroadcastDisposition::Duplicate;return result;
        }

        const MeshReceiveContext context{origin.Source,origin.SourceIncarnation,origin.MessageId,hop.HopLimit,true};
        result.Dispatch=_receivers.Dispatch(origin.PrimitiveFamily,origin.PrimitiveVersion,context,
            {originView.Payload,originView.PayloadByteCount},result.Admission);
        if(result.Dispatch!=PrimitiveDispatchResult::Dispatched||
           result.Admission==Primitive::PrimitiveAdmissionDisposition::Unsupported||
           result.Admission==Primitive::PrimitiveAdmissionDisposition::Rejected||
           result.Admission==Primitive::PrimitiveAdmissionDisposition::Malformed||
           Primitive::EstablishesDestinationAdmission(result.Admission)){
            result.Disposition=MeshV1BroadcastDisposition::Completed;return result;
        }

        if(!Primitive::IsAdmissionRetryCandidate(result.Admission)||origin.MaximumAdapterAdmissionWaitNanoseconds==0U){
            result.Disposition=MeshV1BroadcastDisposition::Completed;return result;
        }
        std::uint64_t deferredExpiry=0U;
        if(!TryEstablishMeshLocalExpiry(monotonicNowNanoseconds,hop.RemainingResidenceMilliseconds,
                origin.MaximumAdapterAdmissionWaitNanoseconds,deferredExpiry)){
            result.Disposition=MeshV1BroadcastDisposition::DeadlineExpired;return result;
        }
        result.DeferredStore=_deferred.Store(context,origin.PrimitiveFamily,origin.PrimitiveVersion,
            {originView.Payload,originView.PayloadByteCount},deferredExpiry,admissionGeneration,monotonicNowNanoseconds);
        if(result.DeferredStore==MeshDeferredLocalStoreResult::Stored||
           result.DeferredStore==MeshDeferredLocalStoreResult::AlreadyRetained){
            result.Disposition=MeshV1BroadcastDisposition::DeferredLocal;
        }else if(result.DeferredStore==MeshDeferredLocalStoreResult::Expired){
            result.Disposition=MeshV1BroadcastDisposition::DeadlineExpired;
        }else{
            result.Disposition=MeshV1BroadcastDisposition::ResourceUnavailable;
        }
        return result;
    }

    MeshDeferredLocalServiceResult ServiceDeferredLocal(
        std::uint64_t currentAdmissionGeneration,
        std::uint64_t monotonicNowNanoseconds,
        Primitive::PrimitiveAdmissionDisposition& disposition) noexcept {
        if(!_accepting||monotonicNowNanoseconds==0U){
            disposition=Primitive::PrimitiveAdmissionDisposition::Malformed;
            return MeshDeferredLocalServiceResult::NoWork;
        }
        return _deferred.ServiceOne(_receivers,currentAdmissionGeneration,monotonicNowNanoseconds,disposition);
    }

    std::size_t ExpireDeferredLocal(std::uint64_t monotonicNowNanoseconds) noexcept {
        return _deferred.Expire(monotonicNowNanoseconds);
    }

    void ShutdownVolatileBroadcastState() noexcept {
        _accepting=false;
        _deferred.Clear();
        _network.Clear();
    }

    bool ObserveAuthenticatedLivenessEvidence(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,std::uint64_t nowMilliseconds) noexcept {
        return _liveness.ObserveAuthenticatedEvidence(device,incarnation,nowMilliseconds);
    }
    ReachabilityState EvaluateMembershipReachability(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,std::uint64_t nowMilliseconds) noexcept {
        return _liveness.Evaluate(device,incarnation,nowMilliseconds);
    }
    const AuthenticatedLivenessEvidence* MembershipLivenessEvidence(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation) const noexcept {
        return _liveness.EvidenceFor(device,incarnation);
    }
};

} // namespace ESPressio::Mesh
