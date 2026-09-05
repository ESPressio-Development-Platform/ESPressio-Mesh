#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_PrimitiveFamilyRegistry.hpp>

#include "ESPressio_DeliveryAcknowledgementCoordinator.hpp"
#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_MeshV1Control.hpp"
#include "ESPressio_MeshV1FrameWorkspace.hpp"
#include "ESPressio_MeshV1ProtectedFrame.hpp"

namespace ESPressio::Mesh {

enum class MeshV1ControlSubmissionDisposition : std::uint8_t {
    Submitted, DeadlineExpired, RouteMismatch, DestinationSessionUnavailable,
    NextHopSessionUnavailable, TrafficCapacityUnavailable, WorkspaceCapacityExceeded, SequenceExhausted,
    ProtectionFailed, ForwardingFailed, Invalid
};

class MeshV1ControlTrafficReservationGuard final {
    IMeshTrafficGovernor& _traffic;
    MeshTrafficReservation _reservation{};
public:
    explicit MeshV1ControlTrafficReservationGuard(IMeshTrafficGovernor& traffic) noexcept : _traffic(traffic) {}
    bool Acquire() noexcept {
        return _traffic.TryAcquire(MeshTrafficClass::InfrastructureResponse, _reservation) ==
               MeshTrafficAdmissionResult::Admitted;
    }
    ~MeshV1ControlTrafficReservationGuard() {
        if (_reservation) (void)_traffic.Release(_reservation);
    }
};

struct MeshV1ControlSubmissionResult final {
    MeshV1ControlSubmissionDisposition Disposition{MeshV1ControlSubmissionDisposition::Invalid};
    ForwardingSubmissionResult Submission{};
};

enum class MeshV1AuthenticatedControlDisposition : std::uint8_t {
    NextHopAcceptance, DestinationDeliveryAcknowledgement, DeadlineExpired,
    ResourceUnavailable, UnknownAuthenticatedSource, HopSessionUnavailable, EndToEndSessionUnavailable,
    ReplayRejected, AuthenticationFailed, NotForLocalNode, Invalid
};

struct MeshV1AuthenticatedControlResult final {
    MeshV1AuthenticatedControlDisposition Disposition{MeshV1AuthenticatedControlDisposition::Invalid};
    System::DeviceIdentifier AuthenticatedSource{};
    MembershipIncarnation AuthenticatedSourceIncarnation{};
    MeshV1AcknowledgedDelivery Acknowledged{};
    MeshMessageId ControlMessageId{0U};
    MeshV1NextHopAcceptanceIntent NextHopAcceptance{};
};

/// <summary>Emits one direct-hop responsibility acceptance under the exact neighbour session.</summary>
template<std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t HopCapacity = Limits::MaxRouteHops,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1NextHopAcceptanceSubmissionCoordinator final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    IMeshTrafficGovernor& _traffic;
    ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& _forwarding;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;

public:
    MeshV1NextHopAcceptanceSubmissionCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        IMeshTrafficGovernor& traffic,
        ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& forwarding,
        MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& workspace,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation
    ) noexcept :
        _memberships(memberships), _sessions(sessions), _provider(provider), _traffic(traffic), _forwarding(forwarding),
        _workspace(workspace), _mesh(mesh), _localDevice(localDevice), _localIncarnation(localIncarnation) {}

    MeshV1ControlSubmissionResult Submit(
        const MeshV1NextHopAcceptanceIntent& intent,
        const ResolvedRoute<HopCapacity>& directRoute,
        std::uint64_t nowMilliseconds
    ) {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        if (!_mesh || !_localDevice || !_localIncarnation || !intent || nowMilliseconds == 0U) {
            return {MeshV1ControlSubmissionDisposition::Invalid, {}};
        }
        if (nowMilliseconds >= intent.Acknowledged.AbsoluteDeadlineMilliseconds) {
            return {MeshV1ControlSubmissionDisposition::DeadlineExpired, {}};
        }
        if (directRoute.Source() != _localDevice || directRoute.Destination() != intent.Recipient ||
            directRoute.HopCount() != 1U) return {MeshV1ControlSubmissionDisposition::RouteMismatch, {}};
        MeshV1ControlTrafficReservationGuard traffic(_traffic);
        if (!traffic.Acquire()) return {MeshV1ControlSubmissionDisposition::TrafficCapacityUnavailable, {}};
        const auto* hop = directRoute.NextHop();
        const auto* membership = hop == nullptr ? nullptr :
            _memberships.FindExact(intent.Recipient, intent.RecipientIncarnation);
        if (hop == nullptr || hop->Neighbour != intent.Recipient || membership == nullptr) {
            return {MeshV1ControlSubmissionDisposition::NextHopSessionUnavailable, {}};
        }
        const auto session = _sessions.Find(intent.Recipient, intent.RecipientIncarnation);
        if (!session) return {MeshV1ControlSubmissionDisposition::NextHopSessionUnavailable, {}};
        auto* control = _workspace.Inner(MeshV1ControlCodec::PacketBytes);
        const auto packetBytes = MeshV1ProtectedFrameCodec::HopPacketBytes(MeshV1ControlCodec::PacketBytes);
        auto* packet = _workspace.Packet(packetBytes);
        if (control == nullptr || packet == nullptr) {
            return {MeshV1ControlSubmissionDisposition::WorkspaceCapacityExceeded, {}};
        }
        if (!MeshV1ControlCodec::Encode(
                MeshV1ControlMessageType::NextHopAcceptance, intent.Acknowledged,
                control, MeshV1ControlCodec::PacketBytes)) {
            return {MeshV1ControlSubmissionDisposition::Invalid, {}};
        }
        const auto sequence = _sessions.IssueSequence(session, MeshSecurityTrafficPurpose::Hop);
        if (sequence == 0U) return {MeshV1ControlSubmissionDisposition::SequenceExhausted, {}};
        const MeshV1HopFrameHeader header{
            _mesh, _sessions.Identifier(session), sequence,
            _localDevice, _localIncarnation, intent.Recipient, intent.RecipientIncarnation,
            intent.Recipient, intent.RecipientIncarnation, intent.Acknowledged.MessageId, 1U,
            static_cast<std::uint16_t>(MeshV1ControlCodec::PacketBytes)};
        if (!MeshV1ProtectedFrameCodec::EncodeHopAuthenticatedHeader(header, packet, packetBytes)) {
            return {MeshV1ControlSubmissionDisposition::ProtectionFailed, {}};
        }
        MeshAuthenticationTag tag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(session), MeshSecurityTrafficPurpose::Hop, sequence,
                packet, MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes,
                control, MeshV1ControlCodec::PacketBytes,
                packet + MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes, tag)) {
            return {MeshV1ControlSubmissionDisposition::ProtectionFailed, {}};
        }
        std::memcpy(packet + packetBytes - tag.Value.size(), tag.Value.data(), tag.Value.size());
        const auto submission = _forwarding.Submit(
            _localDevice, directRoute, 1U, packet, packetBytes,
            nowMilliseconds, intent.Acknowledged.AbsoluteDeadlineMilliseconds);
        return {submission ? MeshV1ControlSubmissionDisposition::Submitted
                           : MeshV1ControlSubmissionDisposition::ForwardingFailed,
                submission};
    }
};

/// <summary>Emits final framework-delivery acknowledgement under EndToEnd and current Hop protection.</summary>
template<std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t HopCapacity = Limits::MaxRouteHops,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1DestinationAcknowledgementSubmissionCoordinator final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    IMeshTrafficGovernor& _traffic;
    ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& _forwarding;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;

public:
    MeshV1DestinationAcknowledgementSubmissionCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        IMeshTrafficGovernor& traffic,
        ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& forwarding,
        MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& workspace,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation
    ) noexcept :
        _memberships(memberships), _sessions(sessions), _provider(provider), _traffic(traffic), _forwarding(forwarding),
        _workspace(workspace), _mesh(mesh), _localDevice(localDevice), _localIncarnation(localIncarnation) {}

    MeshV1ControlSubmissionResult Submit(
        const DeliveryAcknowledgementIntent& intent,
        MeshMessageId controlMessageId,
        const ResolvedRoute<HopCapacity>& route,
        std::uint64_t nowMilliseconds
    ) {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        if (!_mesh || !_localDevice || !_localIncarnation || !intent || controlMessageId == 0U ||
            nowMilliseconds == 0U) return {MeshV1ControlSubmissionDisposition::Invalid, {}};
        if (nowMilliseconds >= intent.AbsoluteDeadlineMilliseconds) {
            return {MeshV1ControlSubmissionDisposition::DeadlineExpired, {}};
        }
        if (route.Source() != _localDevice || route.Destination() != intent.Recipient) {
            return {MeshV1ControlSubmissionDisposition::RouteMismatch, {}};
        }
        MeshV1ControlTrafficReservationGuard traffic(_traffic);
        if (!traffic.Acquire()) return {MeshV1ControlSubmissionDisposition::TrafficCapacityUnavailable, {}};
        const auto* nextHop = route.NextHop();
        const auto* nextMembership = nextHop == nullptr ? nullptr : _memberships.FindDevice(nextHop->Neighbour);
        if (nextHop == nullptr || nextHop->Advertiser != _localDevice || nextMembership == nullptr ||
            !nextMembership->IsValid() || nextMembership->Reachability == ReachabilityState::Unreachable) {
            return {MeshV1ControlSubmissionDisposition::NextHopSessionUnavailable, {}};
        }
        const auto destinationSession = _sessions.Find(intent.Recipient, intent.RecipientIncarnation);
        if (!destinationSession) {
            return {MeshV1ControlSubmissionDisposition::DestinationSessionUnavailable, {}};
        }
        const auto hopSession = _sessions.Find(nextHop->Neighbour, nextMembership->Incarnation);
        if (!hopSession) return {MeshV1ControlSubmissionDisposition::NextHopSessionUnavailable, {}};
        const auto innerBytes = MeshV1ProtectedFrameCodec::EndToEndPacketBytes(MeshV1ControlCodec::PacketBytes);
        const auto packetBytes = MeshV1ProtectedFrameCodec::HopPacketBytes(innerBytes);
        auto* inner = _workspace.Inner(innerBytes);
        auto* packet = _workspace.Packet(packetBytes);
        if (inner == nullptr || packet == nullptr) {
            return {MeshV1ControlSubmissionDisposition::WorkspaceCapacityExceeded, {}};
        }
        MeshV1AcknowledgedDelivery acknowledged{
            intent.Recipient, intent.RecipientIncarnation, intent.AcknowledgedMessageId,
            intent.AbsoluteDeadlineMilliseconds};
        if (!MeshV1ControlCodec::Encode(
                MeshV1ControlMessageType::DestinationDeliveryAcknowledgement,
                acknowledged, packet, MeshV1ControlCodec::PacketBytes)) {
            return {MeshV1ControlSubmissionDisposition::Invalid, {}};
        }
        const auto endToEndSequence = _sessions.IssueSequence(
            destinationSession, MeshSecurityTrafficPurpose::EndToEnd);
        if (endToEndSequence == 0U) return {MeshV1ControlSubmissionDisposition::SequenceExhausted, {}};
        const MeshV1EndToEndFrameHeader endToEndHeader{
            _mesh, _sessions.Identifier(destinationSession), endToEndSequence,
            _localDevice, _localIncarnation, intent.Recipient, intent.RecipientIncarnation,
            controlMessageId, intent.AbsoluteDeadlineMilliseconds,
            Primitive::FamilyIds::MeshControl, MeshV1ControlCodec::Version,
            static_cast<std::uint16_t>(MeshV1ControlCodec::PacketBytes)};
        if (!MeshV1ProtectedFrameCodec::EncodeEndToEndAuthenticatedHeader(
                endToEndHeader, inner, innerBytes)) {
            return {MeshV1ControlSubmissionDisposition::ProtectionFailed, {}};
        }
        MeshAuthenticationTag endToEndTag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(destinationSession), MeshSecurityTrafficPurpose::EndToEnd,
                endToEndSequence, inner, MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes,
                packet, MeshV1ControlCodec::PacketBytes,
                inner + MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes,
                endToEndTag)) {
            return {MeshV1ControlSubmissionDisposition::ProtectionFailed, {}};
        }
        std::memcpy(inner + innerBytes - endToEndTag.Value.size(),
                    endToEndTag.Value.data(), endToEndTag.Value.size());
        const auto hopSequence = _sessions.IssueSequence(hopSession, MeshSecurityTrafficPurpose::Hop);
        if (hopSequence == 0U) return {MeshV1ControlSubmissionDisposition::SequenceExhausted, {}};
        const MeshV1HopFrameHeader hopHeader{
            _mesh, _sessions.Identifier(hopSession), hopSequence,
            _localDevice, _localIncarnation, nextHop->Neighbour, nextMembership->Incarnation,
            intent.Recipient, intent.RecipientIncarnation, controlMessageId,
            Limits::DefaultHopLimit, static_cast<std::uint16_t>(innerBytes)};
        if (!MeshV1ProtectedFrameCodec::EncodeHopAuthenticatedHeader(hopHeader, packet, packetBytes)) {
            return {MeshV1ControlSubmissionDisposition::ProtectionFailed, {}};
        }
        MeshAuthenticationTag hopTag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, hopSequence,
                packet, MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes,
                inner, innerBytes, packet + MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes, hopTag)) {
            return {MeshV1ControlSubmissionDisposition::ProtectionFailed, {}};
        }
        std::memcpy(packet + packetBytes - hopTag.Value.size(), hopTag.Value.data(), hopTag.Value.size());
        const auto submission = _forwarding.Submit(
            _localDevice, route, Limits::DefaultHopLimit, packet, packetBytes,
            nowMilliseconds, intent.AbsoluteDeadlineMilliseconds);
        return {submission ? MeshV1ControlSubmissionDisposition::Submitted
                           : MeshV1ControlSubmissionDisposition::ForwardingFailed,
                submission};
    }
};

/// <summary>Authenticates concrete one-hop acceptance or end-to-end destination-ACK control evidence.</summary>
template<std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1ControlReceiveCoordinator final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;

public:
    MeshV1ControlReceiveCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& workspace,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation
    ) noexcept :
        _memberships(memberships), _sessions(sessions), _provider(provider), _workspace(workspace),
        _mesh(mesh), _localDevice(localDevice), _localIncarnation(localIncarnation) {}

    MeshV1AuthenticatedControlResult Receive(
        const std::uint8_t* packet,
        std::size_t packetBytes,
        std::uint64_t nowMilliseconds
    ) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        MeshV1HopFrameHeader hop{};
        MeshV1ProtectedFrameView hopFrame{};
        if (!_mesh || !_localDevice || !_localIncarnation || nowMilliseconds == 0U ||
            !MeshV1ProtectedFrameCodec::DecodeHop(packet, packetBytes, hop, hopFrame)) {
            return {MeshV1AuthenticatedControlDisposition::Invalid};
        }
        if (hop.Mesh != _mesh || hop.NextHop != _localDevice ||
            hop.NextHopIncarnation != _localIncarnation || hop.Destination != _localDevice ||
            hop.DestinationIncarnation != _localIncarnation) {
            return {MeshV1AuthenticatedControlDisposition::NotForLocalNode};
        }
        if (_memberships.FindExact(hop.Sender, hop.SenderIncarnation) == nullptr) {
            return {MeshV1AuthenticatedControlDisposition::UnknownAuthenticatedSource};
        }
        const auto hopSession = _sessions.Find(hop.Sender, hop.SenderIncarnation);
        if (!hopSession || _sessions.Identifier(hopSession).Value != hop.Session.Value) {
            return {MeshV1AuthenticatedControlDisposition::HopSessionUnavailable};
        }
        auto* inner = _workspace.Inner(hopFrame.CiphertextBytes);
        if (inner == nullptr) return {MeshV1AuthenticatedControlDisposition::ResourceUnavailable};
        if (!_sessions.CanAcceptInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) {
            return {MeshV1AuthenticatedControlDisposition::ReplayRejected};
        }
        if (!_provider.Open(
                _sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, hop.Sequence,
                hopFrame.AuthenticatedHeader, hopFrame.AuthenticatedHeaderBytes,
                hopFrame.Ciphertext, hopFrame.CiphertextBytes, hopFrame.Tag, inner)) {
            return {MeshV1AuthenticatedControlDisposition::AuthenticationFailed};
        }

        MeshV1ControlMessageType type{};
        MeshV1AcknowledgedDelivery acknowledged{};
        if (MeshV1ControlCodec::Decode(inner, hopFrame.CiphertextBytes, type, acknowledged)) {
            if (type != MeshV1ControlMessageType::NextHopAcceptance ||
                hop.MessageId != acknowledged.MessageId) {
                _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
                return {MeshV1AuthenticatedControlDisposition::Invalid};
            }
            if (!_sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) {
                return {MeshV1AuthenticatedControlDisposition::ReplayRejected};
            }
            if (nowMilliseconds >= acknowledged.AbsoluteDeadlineMilliseconds) {
                return {MeshV1AuthenticatedControlDisposition::DeadlineExpired};
            }
            return {MeshV1AuthenticatedControlDisposition::NextHopAcceptance,
                    hop.Sender, hop.SenderIncarnation, acknowledged, hop.MessageId};
        }

        MeshV1EndToEndFrameHeader endToEnd{};
        MeshV1ProtectedFrameView endToEndFrame{};
        if (!MeshV1ProtectedFrameCodec::DecodeEndToEnd(
                inner, hopFrame.CiphertextBytes, endToEnd, endToEndFrame) ||
            endToEnd.Mesh != hop.Mesh || endToEnd.Destination != _localDevice ||
            endToEnd.DestinationIncarnation != _localIncarnation || endToEnd.MessageId != hop.MessageId ||
            endToEnd.PrimitiveFamily != Primitive::FamilyIds::MeshControl ||
            endToEnd.PrimitiveVersion != MeshV1ControlCodec::Version) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1AuthenticatedControlDisposition::Invalid};
        }
        const auto endToEndSession = _sessions.Find(endToEnd.Source, endToEnd.SourceIncarnation);
        if (!endToEndSession || _sessions.Identifier(endToEndSession).Value != endToEnd.Session.Value) {
            return {MeshV1AuthenticatedControlDisposition::EndToEndSessionUnavailable};
        }
        auto* plaintext = _workspace.Packet(endToEndFrame.CiphertextBytes);
        if (plaintext == nullptr) return {MeshV1AuthenticatedControlDisposition::ResourceUnavailable};
        if (!_sessions.CanAcceptInbound(
                endToEndSession, MeshSecurityTrafficPurpose::EndToEnd, endToEnd.Sequence)) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1AuthenticatedControlDisposition::ReplayRejected};
        }
        if (!_provider.Open(
                _sessions.ProviderSession(endToEndSession), MeshSecurityTrafficPurpose::EndToEnd,
                endToEnd.Sequence, endToEndFrame.AuthenticatedHeader, endToEndFrame.AuthenticatedHeaderBytes,
                endToEndFrame.Ciphertext, endToEndFrame.CiphertextBytes, endToEndFrame.Tag, plaintext)) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1AuthenticatedControlDisposition::AuthenticationFailed};
        }
        if (!MeshV1ControlCodec::Decode(
                plaintext, endToEndFrame.CiphertextBytes, type, acknowledged) ||
            type != MeshV1ControlMessageType::DestinationDeliveryAcknowledgement ||
            acknowledged.Source != _localDevice ||
            acknowledged.SourceIncarnation != _localIncarnation ||
            acknowledged.AbsoluteDeadlineMilliseconds != endToEnd.AbsoluteDeadlineMilliseconds) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            _sessions.CommitAuthenticatedInbound(
                endToEndSession, MeshSecurityTrafficPurpose::EndToEnd, endToEnd.Sequence);
            return {MeshV1AuthenticatedControlDisposition::Invalid};
        }
        if (!_sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence) ||
            !_sessions.CommitAuthenticatedInbound(
                endToEndSession, MeshSecurityTrafficPurpose::EndToEnd, endToEnd.Sequence)) {
            return {MeshV1AuthenticatedControlDisposition::ReplayRejected};
        }
        if (nowMilliseconds >= acknowledged.AbsoluteDeadlineMilliseconds) {
            return {MeshV1AuthenticatedControlDisposition::DeadlineExpired};
        }
        return {MeshV1AuthenticatedControlDisposition::DestinationDeliveryAcknowledgement,
                endToEnd.Source, endToEnd.SourceIncarnation, acknowledged, endToEnd.MessageId,
                {hop.Sender, hop.SenderIncarnation,
                 {endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                  endToEnd.AbsoluteDeadlineMilliseconds}}};
    }
};

} // namespace ESPressio::Mesh
