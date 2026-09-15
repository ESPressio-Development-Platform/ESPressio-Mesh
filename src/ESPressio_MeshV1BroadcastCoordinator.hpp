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
            if(submitted) ++result.FanoutAccepted;
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
        const MeshBroadcastFanoutPlan<TFanoutCapacity>& plan) noexcept;

    MeshV1BroadcastResult Receive(
        const std::uint8_t* packet,
        std::size_t packetBytes,
        std::uint64_t monotonicNowNanoseconds,
        std::uint64_t admissionGeneration,
        const MeshBroadcastFanoutPlan<TFanoutCapacity>& plan) noexcept;

    MeshDeferredLocalServiceResult ServiceDeferredLocal(
        std::uint64_t currentAdmissionGeneration,
        std::uint64_t monotonicNowNanoseconds,
        Primitive::PrimitiveAdmissionDisposition& disposition) noexcept;

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
