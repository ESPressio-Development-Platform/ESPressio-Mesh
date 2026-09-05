#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "ESPressio_ApplicationRadioSubmissionCoordinator.hpp"
#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_DeliveryAcknowledgementCoordinator.hpp"
#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_InboundDeliveryCoordinator.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshV1Control.hpp"
#include "ESPressio_MeshV1FrameWorkspace.hpp"
#include "ESPressio_MeshV1ProtectedFrame.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"

namespace ESPressio::Mesh {

enum class MeshV1ProtectedApplicationSubmissionDisposition : std::uint8_t {
    Submitted, RetryCurrentRoute, ReplanDistinctRoute, DeadlineExpired, PermanentFailure,
    UnknownTransmission, UnknownRecipient, RecipientTerminal, RouteMismatch,
    WorkspaceCapacityExceeded, SerializationFailed, DestinationSessionUnavailable,
    NextHopSessionUnavailable, SequenceExhausted, ProtectionFailed, Invalid
};

struct MeshV1ProtectedApplicationSubmissionResult final {
    MeshV1ProtectedApplicationSubmissionDisposition Disposition{
        MeshV1ProtectedApplicationSubmissionDisposition::Invalid};
    ApplicationRadioSubmissionResult Radio{};

    constexpr explicit operator bool() const noexcept {
        return Disposition == MeshV1ProtectedApplicationSubmissionDisposition::Submitted &&
               static_cast<bool>(Radio.Radio.Submission);
    }
};

enum class MeshV1ProtectedDestinationDisposition : std::uint8_t {
    Dispatched, Duplicate, TooOld, AlreadyInProgress, DeadlineExpired, ResourceUnavailable,
    UnsupportedFamily, UnsupportedVersion, RetryableReceiver, UnknownAuthenticatedSource,
    HopSessionUnavailable, EndToEndSessionUnavailable, ReplayRejected, AuthenticationFailed,
    NotForLocalNode, Invalid
};

struct MeshV1ProtectedDestinationResult final {
    MeshV1ProtectedDestinationDisposition Disposition{MeshV1ProtectedDestinationDisposition::Invalid};
    PrimitiveDispatchResult Dispatch{PrimitiveDispatchResult::Invalid};
    PrimitiveReceiveDisposition ReceiverDisposition{PrimitiveReceiveDisposition::Malformed};
    DeliveryAcknowledgementIntent Acknowledgement{};
    MeshV1NextHopAcceptanceIntent NextHopAcceptance{};
};

/// <summary>Protects one frozen application recipient and submits the complete Hop frame to existing routing.</summary>
template<std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t AcknowledgementCapacity,
         std::size_t CorrelationCapacity,
         std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
         std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t HopCapacity = Limits::MaxRouteHops,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1ProtectedApplicationSubmissionCoordinator final {
    using ApplicationSubmission = ApplicationRadioSubmissionCoordinator<
        AcknowledgementCapacity, CorrelationCapacity, TransmissionCapacity, RecipientCapacity,
        MembershipCapacity, BindingCapacity, HopCapacity>;
    using RadioDelivery = OutboundRadioDeliveryCoordinator<
        AcknowledgementCapacity, CorrelationCapacity, MembershipCapacity, BindingCapacity, HopCapacity>;

    const ApplicationTransmissionTable<TransmissionCapacity, RecipientCapacity>& _transmissions;
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    ApplicationSubmission& _applicationSubmission;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;

    static MeshV1ProtectedApplicationSubmissionResult Map(
        const ApplicationRadioSubmissionResult& radio
    ) noexcept {
        switch (radio.Disposition) {
            case ApplicationRadioSubmissionDisposition::AwaitingNextHopAcceptance:
                return {MeshV1ProtectedApplicationSubmissionDisposition::Submitted, radio};
            case ApplicationRadioSubmissionDisposition::RetryCurrentRoute:
                return {MeshV1ProtectedApplicationSubmissionDisposition::RetryCurrentRoute, radio};
            case ApplicationRadioSubmissionDisposition::ReplanDistinctRoute:
                return {MeshV1ProtectedApplicationSubmissionDisposition::ReplanDistinctRoute, radio};
            case ApplicationRadioSubmissionDisposition::DeadlineExpired:
                return {MeshV1ProtectedApplicationSubmissionDisposition::DeadlineExpired, radio};
            case ApplicationRadioSubmissionDisposition::PermanentFailure:
                return {MeshV1ProtectedApplicationSubmissionDisposition::PermanentFailure, radio};
            case ApplicationRadioSubmissionDisposition::UnknownTransmission:
                return {MeshV1ProtectedApplicationSubmissionDisposition::UnknownTransmission, radio};
            case ApplicationRadioSubmissionDisposition::UnknownRecipient:
                return {MeshV1ProtectedApplicationSubmissionDisposition::UnknownRecipient, radio};
            case ApplicationRadioSubmissionDisposition::AlreadyTerminal:
                return {MeshV1ProtectedApplicationSubmissionDisposition::RecipientTerminal, radio};
            case ApplicationRadioSubmissionDisposition::Invalid:
                return {MeshV1ProtectedApplicationSubmissionDisposition::Invalid, radio};
        }
        return {MeshV1ProtectedApplicationSubmissionDisposition::Invalid, radio};
    }

public:
    MeshV1ProtectedApplicationSubmissionCoordinator(
        const ApplicationTransmissionTable<TransmissionCapacity, RecipientCapacity>& transmissions,
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        ApplicationSubmission& applicationSubmission,
        MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& workspace,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation
    ) noexcept :
        _transmissions(transmissions), _memberships(memberships), _sessions(sessions), _provider(provider),
        _applicationSubmission(applicationSubmission), _workspace(workspace), _mesh(mesh), _localDevice(localDevice),
        _localIncarnation(localIncarnation) {}

    MeshV1ProtectedApplicationSubmissionResult SubmitRecipient(
        ApplicationTransmissionHandle transmission,
        std::size_t recipientIndex,
        RadioDelivery& delivery,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        std::uint64_t nowMilliseconds
    ) {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        if (!_mesh || !_localDevice || !_localIncarnation || !_transmissions.Contains(transmission)) {
            return {_transmissions.Contains(transmission)
                        ? MeshV1ProtectedApplicationSubmissionDisposition::Invalid
                        : MeshV1ProtectedApplicationSubmissionDisposition::UnknownTransmission,
                    {}};
        }
        ApplicationTransmissionRecipient recipient{};
        ApplicationRecipientOutcome outcome{};
        if (!_transmissions.TryGetRecipient(transmission, recipientIndex, recipient, outcome)) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::UnknownRecipient, {}};
        }
        if (outcome != ApplicationRecipientOutcome::Pending) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::RecipientTerminal, {}};
        }
        if (!delivery.IsActive() || delivery.MessageId() != recipient.MessageId) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::Invalid, {}};
        }
        if (route.Source() != _localDevice || route.Destination() != recipient.Device) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::RouteMismatch, {}};
        }
        const auto* nextHop = route.NextHop();
        if (nextHop == nullptr || nextHop->Advertiser != _localDevice) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::RouteMismatch, {}};
        }
        const auto* primitive = _transmissions.PrimitiveDescriptor(transmission);
        const auto* payload = _transmissions.Payload(transmission);
        const auto deadline = _transmissions.AbsoluteDeadlineMilliseconds(transmission);
        if (primitive == nullptr || !*primitive || payload == nullptr || !*payload || deadline == 0U ||
            payload->Size() > std::numeric_limits<std::uint16_t>::max()) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::Invalid, {}};
        }
        if (nowMilliseconds >= deadline || remainingHopLimit == 0U) {
            return Map(_applicationSubmission.Submit(
                transmission, delivery, _localDevice, route, remainingHopLimit,
                nullptr, 0U, nowMilliseconds));
        }
        const auto* nextHopMembership = _memberships.FindDevice(nextHop->Neighbour);
        if (nextHopMembership == nullptr || !nextHopMembership->IsValid() ||
            nextHopMembership->Reachability == ReachabilityState::Unreachable) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::NextHopSessionUnavailable, {}};
        }
        const auto destinationSession = _sessions.Find(recipient.Device, recipient.Incarnation);
        if (!destinationSession) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::DestinationSessionUnavailable, {}};
        }
        const auto hopSession = _sessions.Find(nextHop->Neighbour, nextHopMembership->Incarnation);
        if (!hopSession) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::NextHopSessionUnavailable, {}};
        }
        const auto innerBytes = MeshV1ProtectedFrameCodec::EndToEndPacketBytes(payload->Size());
        const auto packetBytes = MeshV1ProtectedFrameCodec::HopPacketBytes(innerBytes);
        auto* inner = _workspace.Inner(innerBytes);
        auto* packet = _workspace.Packet(packetBytes);
        if (inner == nullptr || packet == nullptr) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::WorkspaceCapacityExceeded, {}};
        }

        const std::uint8_t* plaintext = payload->StableData();
        if (payload->Type() == ApplicationPayload::Kind::RepeatableSerialized) {
            if (payload->Size() > PacketWorkspaceBytes || !payload->Read(0U, packet, payload->Size())) {
                return {MeshV1ProtectedApplicationSubmissionDisposition::SerializationFailed, {}};
            }
            plaintext = packet;
        }
        if (plaintext == nullptr) return {MeshV1ProtectedApplicationSubmissionDisposition::Invalid, {}};

        const auto endToEndSequence = _sessions.IssueSequence(
            destinationSession, MeshSecurityTrafficPurpose::EndToEnd);
        if (endToEndSequence == 0U) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::SequenceExhausted, {}};
        }
        const MeshV1EndToEndFrameHeader endToEndHeader{
            _mesh, _sessions.Identifier(destinationSession), endToEndSequence,
            _localDevice, _localIncarnation, recipient.Device, recipient.Incarnation,
            recipient.MessageId, deadline, primitive->Family, primitive->Version,
            static_cast<std::uint16_t>(payload->Size())};
        if (!MeshV1ProtectedFrameCodec::EncodeEndToEndAuthenticatedHeader(
                endToEndHeader, inner, innerBytes)) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::ProtectionFailed, {}};
        }
        MeshAuthenticationTag endToEndTag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(destinationSession), MeshSecurityTrafficPurpose::EndToEnd,
                endToEndSequence, inner, MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes,
                plaintext, payload->Size(), inner + MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes,
                endToEndTag)) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::ProtectionFailed, {}};
        }
        std::memcpy(inner + innerBytes - endToEndTag.Value.size(),
                    endToEndTag.Value.data(), endToEndTag.Value.size());

        const auto hopSequence = _sessions.IssueSequence(hopSession, MeshSecurityTrafficPurpose::Hop);
        if (hopSequence == 0U) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::SequenceExhausted, {}};
        }
        const MeshV1HopFrameHeader hopHeader{
            _mesh, _sessions.Identifier(hopSession), hopSequence,
            _localDevice, _localIncarnation, nextHop->Neighbour, nextHopMembership->Incarnation,
            recipient.Device, recipient.Incarnation, recipient.MessageId, remainingHopLimit,
            static_cast<std::uint16_t>(innerBytes)};
        if (!MeshV1ProtectedFrameCodec::EncodeHopAuthenticatedHeader(hopHeader, packet, packetBytes)) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::ProtectionFailed, {}};
        }
        MeshAuthenticationTag hopTag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, hopSequence,
                packet, MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes,
                inner, innerBytes, packet + MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes, hopTag)) {
            return {MeshV1ProtectedApplicationSubmissionDisposition::ProtectionFailed, {}};
        }
        std::memcpy(packet + packetBytes - hopTag.Value.size(), hopTag.Value.data(), hopTag.Value.size());
        return Map(_applicationSubmission.Submit(
            transmission, delivery, _localDevice, route, remainingHopLimit,
            packet, packetBytes, nowMilliseconds));
    }
};

/// <summary>Authenticates and opens a final-destination Hop/EndToEnd frame before bounded primitive dispatch.</summary>
template<std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t InProgressCapacity = Limits::MaxActiveInboundDeliveries,
         std::size_t ReceiverCapacity = Limits::MaxPrimitiveReceivers,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1ProtectedDestinationCoordinator final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    InboundDeliveryCoordinator<MembershipCapacity, InProgressCapacity>& _inbound;
    PrimitiveReceiverRegistry<ReceiverCapacity>& _receivers;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;

public:
    MeshV1ProtectedDestinationCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        InboundDeliveryCoordinator<MembershipCapacity, InProgressCapacity>& inbound,
        PrimitiveReceiverRegistry<ReceiverCapacity>& receivers,
        MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& workspace,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation
    ) noexcept :
        _memberships(memberships), _sessions(sessions), _provider(provider), _inbound(inbound),
        _receivers(receivers), _workspace(workspace), _mesh(mesh), _localDevice(localDevice),
        _localIncarnation(localIncarnation) {}

    MeshV1ProtectedDestinationResult Receive(
        const std::uint8_t* packet,
        std::size_t packetBytes,
        std::uint64_t nowMilliseconds
    ) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        MeshV1HopFrameHeader hop{};
        MeshV1ProtectedFrameView hopFrame{};
        if (!_mesh || !_localDevice || !_localIncarnation || nowMilliseconds == 0U ||
            !MeshV1ProtectedFrameCodec::DecodeHop(packet, packetBytes, hop, hopFrame)) {
            return {MeshV1ProtectedDestinationDisposition::Invalid};
        }
        if (hop.Mesh != _mesh || hop.NextHop != _localDevice ||
            hop.NextHopIncarnation != _localIncarnation || hop.Destination != _localDevice ||
            hop.DestinationIncarnation != _localIncarnation) {
            return {MeshV1ProtectedDestinationDisposition::NotForLocalNode};
        }
        if (_memberships.FindExact(hop.Sender, hop.SenderIncarnation) == nullptr) {
            return {MeshV1ProtectedDestinationDisposition::UnknownAuthenticatedSource};
        }
        const auto hopSession = _sessions.Find(hop.Sender, hop.SenderIncarnation);
        if (!hopSession || _sessions.Identifier(hopSession).Value != hop.Session.Value) {
            return {MeshV1ProtectedDestinationDisposition::HopSessionUnavailable};
        }
        auto* inner = _workspace.Inner(hopFrame.CiphertextBytes);
        if (inner == nullptr) return {MeshV1ProtectedDestinationDisposition::ResourceUnavailable};
        if (!_sessions.CanAcceptInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) {
            return {MeshV1ProtectedDestinationDisposition::ReplayRejected};
        }
        if (!_provider.Open(
                _sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, hop.Sequence,
                hopFrame.AuthenticatedHeader, hopFrame.AuthenticatedHeaderBytes,
                hopFrame.Ciphertext, hopFrame.CiphertextBytes, hopFrame.Tag, inner)) {
            return {MeshV1ProtectedDestinationDisposition::AuthenticationFailed};
        }
        MeshV1EndToEndFrameHeader endToEnd{};
        MeshV1ProtectedFrameView endToEndFrame{};
        if (!MeshV1ProtectedFrameCodec::DecodeEndToEnd(
                inner, hopFrame.CiphertextBytes, endToEnd, endToEndFrame) ||
            endToEnd.Mesh != hop.Mesh || endToEnd.Destination != hop.Destination ||
            endToEnd.DestinationIncarnation != hop.DestinationIncarnation ||
            endToEnd.MessageId != hop.MessageId) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1ProtectedDestinationDisposition::Invalid};
        }
        const auto endToEndSession = _sessions.Find(endToEnd.Source, endToEnd.SourceIncarnation);
        if (!endToEndSession || _sessions.Identifier(endToEndSession).Value != endToEnd.Session.Value) {
            return {MeshV1ProtectedDestinationDisposition::EndToEndSessionUnavailable};
        }
        auto* plaintext = _workspace.Packet(endToEndFrame.CiphertextBytes);
        if (plaintext == nullptr) return {MeshV1ProtectedDestinationDisposition::ResourceUnavailable};
        if (!_sessions.CanAcceptInbound(
                endToEndSession, MeshSecurityTrafficPurpose::EndToEnd, endToEnd.Sequence)) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1ProtectedDestinationDisposition::ReplayRejected};
        }
        if (!_provider.Open(
                _sessions.ProviderSession(endToEndSession), MeshSecurityTrafficPurpose::EndToEnd,
                endToEnd.Sequence, endToEndFrame.AuthenticatedHeader, endToEndFrame.AuthenticatedHeaderBytes,
                endToEndFrame.Ciphertext, endToEndFrame.CiphertextBytes, endToEndFrame.Tag, plaintext)) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1ProtectedDestinationDisposition::AuthenticationFailed};
        }
        if (!_sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence) ||
            !_sessions.CommitAuthenticatedInbound(
                endToEndSession, MeshSecurityTrafficPurpose::EndToEnd, endToEnd.Sequence)) {
            return {MeshV1ProtectedDestinationDisposition::ReplayRejected};
        }

        const InboundDeliveryIdentity identity{
            endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId};
        switch (_inbound.TryBegin(identity)) {
            case InboundDeliveryBeginResult::Duplicate:
                return {MeshV1ProtectedDestinationDisposition::Duplicate,
                        PrimitiveDispatchResult::Invalid, PrimitiveReceiveDisposition::Malformed,
                        _inbound.WasAccepted(identity) == InboundDeliveryAcceptanceResult::Accepted
                            ? DeliveryAcknowledgementIntent{
                                  endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                                  endToEnd.AbsoluteDeadlineMilliseconds}
                            : DeliveryAcknowledgementIntent{},
                        {hop.Sender, hop.SenderIncarnation,
                         {endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                          endToEnd.AbsoluteDeadlineMilliseconds}}};
            case InboundDeliveryBeginResult::TooOld:
                return {MeshV1ProtectedDestinationDisposition::TooOld};
            case InboundDeliveryBeginResult::AlreadyInProgress:
                return {MeshV1ProtectedDestinationDisposition::AlreadyInProgress};
            case InboundDeliveryBeginResult::ResourceUnavailable:
                return {MeshV1ProtectedDestinationDisposition::ResourceUnavailable};
            case InboundDeliveryBeginResult::UnknownAuthenticatedMembership:
                return {MeshV1ProtectedDestinationDisposition::UnknownAuthenticatedSource};
            case InboundDeliveryBeginResult::Invalid:
                return {MeshV1ProtectedDestinationDisposition::Invalid};
            case InboundDeliveryBeginResult::Reserved:
                break;
        }
        if (nowMilliseconds >= endToEnd.AbsoluteDeadlineMilliseconds) {
            const auto committed = _inbound.CommitDefinitive(identity);
            return {committed == InboundDeliveryCommitResult::Committed ||
                            committed == InboundDeliveryCommitResult::AlreadyCommitted
                        ? MeshV1ProtectedDestinationDisposition::DeadlineExpired
                        : MeshV1ProtectedDestinationDisposition::ResourceUnavailable};
        }

        const MeshReceiveContext receiveContext{
            endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
            static_cast<RemainingHopLimit>(hop.HopLimit - 1U), false};
        PrimitiveReceiveDisposition receiverDisposition{PrimitiveReceiveDisposition::Malformed};
        const auto dispatch = _receivers.Dispatch(
            endToEnd.PrimitiveFamily, endToEnd.PrimitiveVersion, receiveContext,
            {plaintext, endToEndFrame.CiphertextBytes}, receiverDisposition);
        if (dispatch == PrimitiveDispatchResult::Dispatched &&
            (receiverDisposition == PrimitiveReceiveDisposition::TemporarilyUnavailable ||
             receiverDisposition == PrimitiveReceiveDisposition::ResourceUnavailable)) {
            _inbound.ReleaseRetryable(identity);
            return {MeshV1ProtectedDestinationDisposition::RetryableReceiver, dispatch, receiverDisposition};
        }
        const auto committed = dispatch == PrimitiveDispatchResult::Dispatched &&
                                       receiverDisposition == PrimitiveReceiveDisposition::Accepted
            ? _inbound.CommitAccepted(identity)
            : _inbound.CommitDefinitive(identity);
        if (committed != InboundDeliveryCommitResult::Committed &&
            committed != InboundDeliveryCommitResult::AlreadyCommitted) {
            return {MeshV1ProtectedDestinationDisposition::ResourceUnavailable, dispatch, receiverDisposition};
        }
        if (dispatch == PrimitiveDispatchResult::UnsupportedFamily) {
            return {MeshV1ProtectedDestinationDisposition::UnsupportedFamily, dispatch, receiverDisposition, {},
                    {hop.Sender, hop.SenderIncarnation,
                     {endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                      endToEnd.AbsoluteDeadlineMilliseconds}}};
        }
        if (dispatch == PrimitiveDispatchResult::UnsupportedVersion) {
            return {MeshV1ProtectedDestinationDisposition::UnsupportedVersion, dispatch, receiverDisposition, {},
                    {hop.Sender, hop.SenderIncarnation,
                     {endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                      endToEnd.AbsoluteDeadlineMilliseconds}}};
        }
        if (dispatch != PrimitiveDispatchResult::Dispatched) {
            return {MeshV1ProtectedDestinationDisposition::Invalid, dispatch, receiverDisposition};
        }
        return {MeshV1ProtectedDestinationDisposition::Dispatched, dispatch, receiverDisposition,
                receiverDisposition == PrimitiveReceiveDisposition::Accepted
                    ? DeliveryAcknowledgementIntent{
                          endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                          endToEnd.AbsoluteDeadlineMilliseconds}
                    : DeliveryAcknowledgementIntent{},
                {hop.Sender, hop.SenderIncarnation,
                 {endToEnd.Source, endToEnd.SourceIncarnation, endToEnd.MessageId,
                  endToEnd.AbsoluteDeadlineMilliseconds}}};
    }
};

} // namespace ESPressio::Mesh
