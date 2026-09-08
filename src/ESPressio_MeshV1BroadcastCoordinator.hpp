#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <ESPressio_RadioTransport.hpp>
#include <ESPressio_SystemPlatformClock.hpp>

#include "ESPressio_ApplicationPayload.hpp"
#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_MembershipLiveness.hpp"
#include "ESPressio_MeshMessageIdGenerator.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_MeshV1BroadcastFrame.hpp"
#include "ESPressio_MeshV1FrameWorkspace.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"

namespace ESPressio::Mesh {

/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshBroadcastFanoutTarget final {
    System::DeviceIdentifier Neighbour{};
    MembershipIncarnation Incarnation{};
    RadioIdentifier LocalRadio{0U};
    Radio::RadioPeerHandle Peer{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Neighbour) && static_cast<bool>(Incarnation) &&
               LocalRadio != 0U && LocalRadio != 0xFFU && static_cast<bool>(Peer);
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
template<std::size_t Capacity = Limits::MaxMeshNodes>
class MeshBroadcastFanoutPlan final {
    static_assert(Capacity > 0U, "Broadcast fan-out capacity must be non-zero.");
    std::array<MeshBroadcastFanoutTarget, Capacity> _targets{};
    std::size_t _size{0U};

public:
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr const MeshBroadcastFanoutTarget* At(std::size_t index) const noexcept {
        return index < _size ? &_targets[index] : nullptr;
    }
    void Clear() noexcept { _targets = {}; _size = 0U; }
    bool TryAdd(const MeshBroadcastFanoutTarget& target) noexcept {
        if (!target || _size >= Capacity) return false;
        for (std::size_t index = 0U; index < _size; ++index) {
            if (_targets[index].Neighbour == target.Neighbour) return false;
        }
        std::size_t insertion = _size;
        while (insertion > 0U && target.Neighbour < _targets[insertion - 1U].Neighbour) {
            _targets[insertion] = _targets[insertion - 1U];
            --insertion;
        }
        _targets[insertion] = target;
        ++_size;
        return true;
    }
};

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class MeshV1BroadcastDisposition : std::uint8_t {
    Completed, Duplicate, TooOld, DeadlineExpired, ResourceUnavailable,
    WorkspaceCapacityExceeded, SerializationFailed, SequenceExhausted,
    UnknownAuthenticatedSender, UnknownAuthenticatedSource, ReplayRejected,
    AuthenticationFailed, NotForLocalNode, Invalid
};

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class MeshBroadcastLocalDispatch : std::uint8_t { Include, Exclude };

/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshV1BroadcastResult final {
    MeshV1BroadcastDisposition Disposition{MeshV1BroadcastDisposition::Invalid};
    MeshMessageId MessageId{0U};
    PrimitiveDispatchResult Dispatch{PrimitiveDispatchResult::Invalid};
    PrimitiveReceiveDisposition ReceiverDisposition{PrimitiveReceiveDisposition::Malformed};
    std::uint8_t FanoutAttempted{0U};
    std::uint8_t FanoutAccepted{0U};
};

/// <summary>Explicitly separates distributed deadline time from local liveness-evidence time.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshV1BroadcastReceiveTimes final {
    std::uint64_t DeadlineClockMilliseconds{0U};
    std::uint64_t MonotonicMilliseconds{0U};
    constexpr bool IsValid() const noexcept {
        return DeadlineClockMilliseconds != 0U && MonotonicMilliseconds != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/**
 * ESPressio Memory Audit
 * Members:
 * - _traffic (IMeshTrafficGovernor&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class MeshBroadcastTrafficReservationGuard final {
    IMeshTrafficGovernor& _traffic;
    MeshTrafficReservation _reservation{};
public:
    explicit MeshBroadcastTrafficReservationGuard(IMeshTrafficGovernor& traffic) noexcept : _traffic(traffic) {}
    bool Acquire() noexcept {
        return _traffic.TryAcquire(MeshTrafficClass::Application, _reservation) == MeshTrafficAdmissionResult::Admitted;
    }
    ~MeshBroadcastTrafficReservationGuard() { if (_reservation) (void)_traffic.Release(_reservation); }
};

/// <summary>Originates and relays one bounded best-effort Mesh v1 Broadcast without recipient outcome state.</summary>
/// <remarks>
/// Pairwise Hop authentication proves only the immediate sender is alive. Origin identity is not liveness evidence.
/// Distributed deadline time and local monotonic liveness time are intentionally separate. The convenience Receive
/// overload captures local monotonic time internally, making the ordinary API safe against synchronized-clock steps;
/// the explicit-times overload remains available to deterministic tests and compositions owning a monotonic timestamp.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _memberships (AuthenticatedMembershipTable<MembershipCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _liveness (MembershipLivenessTracker<MembershipCapacity>): 8 bytes [0 bytes dynamic allocation]
 * - _bindings (AuthenticatedDirectPeerBindingTable<BindingCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _sessions (MeshSecuritySessionTable<SessionCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _provider (IMeshV1CryptographicProvider&): 4 bytes [0 bytes dynamic allocation]
 * - _receivers (PrimitiveReceiverRegistry<ReceiverCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _transport (Radio::RadioTransport&): 4 bytes [0 bytes dynamic allocation]
 * - _traffic (IMeshTrafficGovernor&): 4 bytes [0 bytes dynamic allocation]
 * - _workspace (MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>&): 4 bytes [0 bytes dynamic allocation]
 * - _messageIds (MeshMessageIdGenerator&): 4 bytes [0 bytes dynamic allocation]
 * - _mesh (MeshIdentifier): 0 bytes [0 bytes dynamic allocation]
 * - _localDevice (System::DeviceIdentifier): sizeof(System::DeviceIdentifier) [0 bytes dynamic allocation]
 * - _localIncarnation (MembershipIncarnation): 0 bytes [0 bytes dynamic allocation]
 * Total Memory: 44 bytes known members + sizeof(System::DeviceIdentifier) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t FanoutCapacity = Limits::MaxMeshNodes,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t ReceiverCapacity = Limits::MaxPrimitiveReceivers,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1BroadcastCoordinator final {
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    DefaultMeshLivenessPolicy _livenessPolicy{};
    MembershipLivenessTracker<MembershipCapacity> _liveness;
    const AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    PrimitiveReceiverRegistry<ReceiverCapacity>& _receivers;
    Radio::RadioTransport& _transport;
    IMeshTrafficGovernor& _traffic;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshMessageIdGenerator& _messageIds;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;

    bool CommitAuthenticatedHop(MeshSecuritySessionRecordHandle session,
                                const MeshV1BroadcastHopHeader& hop,
                                std::uint64_t livenessMonotonicMilliseconds) noexcept {
        if (!_sessions.CommitAuthenticatedInbound(session, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) return false;
        (void)_liveness.ObserveAuthenticatedEvidence(hop.Sender, hop.SenderIncarnation, livenessMonotonicMilliseconds);
        return true;
    }

    void DispatchLocal(const MeshV1BroadcastOriginHeader& origin,
                       const MeshV1BroadcastOriginView& view,
                       RemainingHopLimit remainingHops,
                       MeshV1BroadcastResult& result) noexcept {
        const MeshReceiveContext context{
            origin.Source, origin.SourceIncarnation, origin.MessageId, remainingHops, true};
        result.Dispatch = _receivers.Dispatch(origin.PrimitiveFamily, origin.PrimitiveVersion, context,
            {view.Payload, view.PayloadByteCount}, result.ReceiverDisposition);
    }

    void Fanout(const std::uint8_t* originPacket,
                std::size_t originPacketBytes,
                const MeshV1BroadcastOriginHeader& origin,
                const MeshBroadcastFanoutPlan<FanoutCapacity>& plan,
                const System::DeviceIdentifier& previousSender,
                RemainingHopLimit hopLimit,
                MeshV1BroadcastResult& result) noexcept {
        if (hopLimit == 0U || originPacketBytes > std::numeric_limits<std::uint16_t>::max()) return;
        const auto packetBytes = MeshV1BroadcastFrameCodec::HopPacketBytes(originPacketBytes);
        auto* packet = _workspace.Packet(packetBytes);
        if (packet == nullptr) return;

        for (std::size_t index = 0U; index < plan.Size(); ++index) {
            const auto* target = plan.At(index);
            if (target == nullptr || target->Neighbour == previousSender || target->Neighbour == _localDevice) continue;
            ++result.FanoutAttempted;
            const auto* member = _memberships.FindExact(target->Neighbour, target->Incarnation);
            const auto* binding = _bindings.Resolve(target->LocalRadio, target->Neighbour, target->Incarnation);
            const auto session = _sessions.Find(target->Neighbour, target->Incarnation);
            if (member == nullptr || member->State != MembershipState::Active ||
                member->Reachability == ReachabilityState::Unreachable || binding == nullptr ||
                binding->Peer != target->Peer || !session) continue;
            const auto sequence = _sessions.IssueSequence(session, MeshSecurityTrafficPurpose::Hop);
            if (sequence == 0U) continue;
            const MeshV1BroadcastHopHeader header{
                _mesh, _sessions.Identifier(session), sequence,
                _localDevice, _localIncarnation, target->Neighbour, target->Incarnation,
                origin.Source, origin.SourceIncarnation, origin.MessageId, hopLimit,
                static_cast<std::uint16_t>(originPacketBytes)};
            if (!MeshV1BroadcastFrameCodec::EncodeHopAuthenticatedHeader(header, packet, packetBytes)) continue;
            MeshAuthenticationTag tag{};
            if (!_provider.Seal(_sessions.ProviderSession(session), MeshSecurityTrafficPurpose::Hop, sequence,
                    packet, MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes,
                    originPacket, originPacketBytes,
                    packet + MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes, tag)) continue;
            std::memcpy(packet + packetBytes - tag.Value.size(), tag.Value.data(), tag.Value.size());
            const auto radio = _transport.Send(target->Peer, packet, packetBytes);
            if (radio.Status == Radio::RadioTransportSendStatus::Accepted) ++result.FanoutAccepted;
        }
    }

public:
    MeshV1BroadcastCoordinator(AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        const AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        PrimitiveReceiverRegistry<ReceiverCapacity>& receivers,
        Radio::RadioTransport& transport,
        IMeshTrafficGovernor& traffic,
        MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& workspace,
        MeshMessageIdGenerator& messageIds,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation) noexcept :
        _memberships(memberships), _liveness(_memberships, _livenessPolicy), _bindings(bindings),
        _sessions(sessions), _provider(provider), _receivers(receivers), _transport(transport),
        _traffic(traffic), _workspace(workspace), _messageIds(messageIds), _mesh(mesh),
        _localDevice(localDevice), _localIncarnation(localIncarnation) {}

    bool ObserveAuthenticatedLivenessEvidence(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation, std::uint64_t nowMilliseconds) noexcept {
        return _liveness.ObserveAuthenticatedEvidence(device, incarnation, nowMilliseconds);
    }

    ReachabilityState EvaluateMembershipReachability(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation, std::uint64_t nowMilliseconds) noexcept {
        return _liveness.Evaluate(device, incarnation, nowMilliseconds);
    }

    const AuthenticatedLivenessEvidence* MembershipLivenessEvidence(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation) const noexcept {
        return _liveness.EvidenceFor(device, incarnation);
    }

    MembershipLivenessTracker<MembershipCapacity>& LivenessTracker() noexcept { return _liveness; }
    const MembershipLivenessTracker<MembershipCapacity>& LivenessTracker() const noexcept { return _liveness; }

    bool IsMembershipUnreachableRetentionElapsed(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation, std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds = Limits::UnreachableMemberRetentionMilliseconds) noexcept {
        return _liveness.IsUnreachableRetentionElapsed(device, incarnation, nowMilliseconds, retentionMilliseconds);
    }

    bool ForgetMembershipLiveness(const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation) noexcept {
        return _liveness.Forget(device, incarnation);
    }

    MeshV1BroadcastResult Submit(ApplicationPrimitiveDescriptor primitive,
        const ApplicationPayload& payload,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds,
        RemainingHopLimit hopLimit,
        const MeshBroadcastFanoutPlan<FanoutCapacity>& plan,
        MeshBroadcastLocalDispatch localDispatch = MeshBroadcastLocalDispatch::Include) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> workspaceReset(_workspace);
        MeshV1BroadcastResult result{};
        if (!_mesh || !_localDevice || !_localIncarnation || !primitive || !payload ||
            payload.Size() > std::numeric_limits<std::uint16_t>::max() ||
            nowMilliseconds == 0U || absoluteDeadlineMilliseconds == 0U || hopLimit == 0U) return result;
        if (nowMilliseconds >= absoluteDeadlineMilliseconds) {
            result.Disposition = MeshV1BroadcastDisposition::DeadlineExpired;
            return result;
        }
        MeshBroadcastTrafficReservationGuard traffic(_traffic);
        if (!traffic.Acquire()) { result.Disposition = MeshV1BroadcastDisposition::ResourceUnavailable; return result; }
        const auto originBytes = MeshV1BroadcastFrameCodec::OriginPacketBytes(payload.Size());
        if (originBytes == 0U || MeshV1BroadcastFrameCodec::HopPacketBytes(originBytes) == 0U) {
            result.Disposition = MeshV1BroadcastDisposition::Invalid; return result;
        }
        auto* originPacket = _workspace.Inner(originBytes);
        if (originPacket == nullptr || _workspace.Packet(MeshV1BroadcastFrameCodec::HopPacketBytes(originBytes)) == nullptr) {
            result.Disposition = MeshV1BroadcastDisposition::WorkspaceCapacityExceeded; return result;
        }
        if (!_messageIds.TryIssue(result.MessageId)) { result.Disposition = MeshV1BroadcastDisposition::SequenceExhausted; return result; }
        const MeshV1BroadcastOriginHeader origin{
            _mesh, _localDevice, _localIncarnation, result.MessageId, absoluteDeadlineMilliseconds,
            primitive.Family, primitive.Version, static_cast<std::uint16_t>(payload.Size())};
        if (!MeshV1BroadcastFrameCodec::EncodeOriginAuthenticatedHeader(origin, originPacket, originBytes) ||
            !payload.Read(0U, originPacket + MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes, payload.Size())) {
            result.Disposition = MeshV1BroadcastDisposition::SerializationFailed; return result;
        }
        MeshSecurityDigest digest{};
        const auto signedBytes = MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes + payload.Size();
        if (!_provider.Hash(originPacket, signedBytes, digest)) { result.Disposition = MeshV1BroadcastDisposition::AuthenticationFailed; return result; }
        MeshIdentitySignature signature{};
        if (!_provider.SignIdentityDigest(_localDevice, digest, signature)) { result.Disposition = MeshV1BroadcastDisposition::AuthenticationFailed; return result; }
        std::memcpy(originPacket + signedBytes, signature.Value.data(), signature.Value.size());
        MeshV1BroadcastOriginView view{originPacket, signedBytes,
            originPacket + MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes, payload.Size(), signature};
        if (localDispatch == MeshBroadcastLocalDispatch::Include) DispatchLocal(origin, view, hopLimit, result);
        Fanout(originPacket, originBytes, origin, plan, {}, hopLimit, result);
        result.Disposition = MeshV1BroadcastDisposition::Completed;
        return result;
    }

    /// <summary>
    /// Safe ordinary receive overload: the caller supplies synchronized deadline time while Mesh captures its own local
    /// monotonic liveness evidence time. This prevents the two domains from being accidentally aliased by composition.
    /// </summary>
    MeshV1BroadcastResult Receive(const std::uint8_t* packet,
        std::size_t packetBytes,
        std::uint64_t deadlineClockMilliseconds,
        const MeshBroadcastFanoutPlan<FanoutCapacity>& plan) noexcept {
        const auto monotonicNanoseconds = System::Clock::Monotonic().NowNanoseconds();
        auto monotonicMilliseconds = monotonicNanoseconds / 1000000ULL;
        if (monotonicMilliseconds == 0U) monotonicMilliseconds = 1U;
        return Receive(packet, packetBytes,
            MeshV1BroadcastReceiveTimes{deadlineClockMilliseconds, monotonicMilliseconds}, plan);
    }

    MeshV1BroadcastResult Receive(const std::uint8_t* packet,
        std::size_t packetBytes,
        const MeshV1BroadcastReceiveTimes& times,
        const MeshBroadcastFanoutPlan<FanoutCapacity>& plan) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> workspaceReset(_workspace);
        MeshV1BroadcastResult result{};
        MeshV1BroadcastHopHeader hop{};
        MeshV1BroadcastHopView hopView{};
        if (!_mesh || !_localDevice || !_localIncarnation || !times ||
            !MeshV1BroadcastFrameCodec::DecodeHop(packet, packetBytes, hop, hopView)) return result;
        result.MessageId = hop.MessageId;
        if (hop.Mesh != _mesh || hop.NextHop != _localDevice || hop.NextHopIncarnation != _localIncarnation) {
            result.Disposition = MeshV1BroadcastDisposition::NotForLocalNode; return result;
        }
        MeshBroadcastTrafficReservationGuard traffic(_traffic);
        if (!traffic.Acquire()) { result.Disposition = MeshV1BroadcastDisposition::ResourceUnavailable; return result; }
        auto* sender = _memberships.FindExact(hop.Sender, hop.SenderIncarnation);
        if (sender == nullptr || sender->State != MembershipState::Active) {
            result.Disposition = MeshV1BroadcastDisposition::UnknownAuthenticatedSender; return result;
        }
        const auto hopSession = _sessions.Find(hop.Sender, hop.SenderIncarnation);
        if (!hopSession || _sessions.Identifier(hopSession).Value != hop.Session.Value ||
            !_sessions.CanAcceptInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) {
            result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
        }
        auto* originPacket = _workspace.Inner(hopView.CiphertextBytes);
        if (originPacket == nullptr || _workspace.Packet(packetBytes) == nullptr) {
            result.Disposition = MeshV1BroadcastDisposition::WorkspaceCapacityExceeded; return result;
        }
        if (!_provider.Open(_sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, hop.Sequence,
                hopView.AuthenticatedHeader, hopView.AuthenticatedHeaderBytes,
                hopView.Ciphertext, hopView.CiphertextBytes, hopView.Tag, originPacket)) {
            result.Disposition = MeshV1BroadcastDisposition::AuthenticationFailed; return result;
        }
        MeshV1BroadcastOriginHeader origin{};
        MeshV1BroadcastOriginView originView{};
        if (!MeshV1BroadcastFrameCodec::DecodeOrigin(originPacket, hopView.CiphertextBytes, origin, originView) ||
            origin.Mesh != _mesh || origin.Source != hop.Source ||
            origin.SourceIncarnation != hop.SourceIncarnation || origin.MessageId != hop.MessageId) {
            result.Disposition = MeshV1BroadcastDisposition::AuthenticationFailed; return result;
        }

        const bool localLoop = origin.Source == _localDevice && origin.SourceIncarnation == _localIncarnation;
        if (localLoop) {
            if (!CommitAuthenticatedHop(hopSession, hop, times.MonotonicMilliseconds)) {
                result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
            }
            result.Disposition = MeshV1BroadcastDisposition::Duplicate; return result;
        }

        auto* source = _memberships.FindExact(origin.Source, origin.SourceIncarnation);
        if (source == nullptr || source->State != MembershipState::Active) {
            if (!CommitAuthenticatedHop(hopSession, hop, times.MonotonicMilliseconds)) {
                result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
            }
            result.Disposition = MeshV1BroadcastDisposition::UnknownAuthenticatedSource; return result;
        }

        const auto duplicate = source->BroadcastDeduplication.Classify(origin.MessageId);
        if (duplicate == DeduplicationDisposition::Duplicate || duplicate == DeduplicationDisposition::TooOld) {
            if (!CommitAuthenticatedHop(hopSession, hop, times.MonotonicMilliseconds)) {
                result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
            }
            result.Disposition = duplicate == DeduplicationDisposition::Duplicate
                ? MeshV1BroadcastDisposition::Duplicate : MeshV1BroadcastDisposition::TooOld;
            return result;
        }
        if (duplicate != DeduplicationDisposition::Unseen) return result;

        if (times.DeadlineClockMilliseconds >= origin.AbsoluteDeadlineMilliseconds) {
            if (!CommitAuthenticatedHop(hopSession, hop, times.MonotonicMilliseconds)) {
                result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
            }
            result.Disposition = MeshV1BroadcastDisposition::DeadlineExpired; return result;
        }

        MeshSecurityDigest digest{};
        if (!_provider.Hash(originView.SignedBytes, originView.SignedByteCount, digest)) {
            result.Disposition = MeshV1BroadcastDisposition::AuthenticationFailed; return result;
        }
        const auto verification = _provider.VerifyRegisteredIdentityDigest(origin.Source, digest, originView.Signature);
        if (verification == MeshIdentityVerificationResult::ResourceUnavailable) {
            result.Disposition = MeshV1BroadcastDisposition::ResourceUnavailable; return result;
        }
        if (verification != MeshIdentityVerificationResult::Verified) {
            if (!CommitAuthenticatedHop(hopSession, hop, times.MonotonicMilliseconds)) {
                result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
            }
            result.Disposition = MeshV1BroadcastDisposition::AuthenticationFailed; return result;
        }
        if (!CommitAuthenticatedHop(hopSession, hop, times.MonotonicMilliseconds)) {
            result.Disposition = MeshV1BroadcastDisposition::ReplayRejected; return result;
        }
        if (source->BroadcastDeduplication.Commit(origin.MessageId) != DeduplicationDisposition::Unseen) return result;

        DispatchLocal(origin, originView, hop.HopLimit, result);
        if (hop.HopLimit > 1U) {
            Fanout(originPacket, hopView.CiphertextBytes, origin, plan, hop.Sender,
                static_cast<RemainingHopLimit>(hop.HopLimit - 1U), result);
        }
        result.Disposition = MeshV1BroadcastDisposition::Completed;
        return result;
    }
};

} // namespace ESPressio::Mesh
