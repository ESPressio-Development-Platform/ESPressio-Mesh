#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "ESPressio_ForwardingSubmissionCoordinator.hpp"
#include "ESPressio_ForwardingTransitionCoordinator.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshTrafficGovernor.hpp"
#include "ESPressio_MeshV1FrameWorkspace.hpp"
#include "ESPressio_MeshV1ProtectedFrame.hpp"

namespace ESPressio::Mesh {

struct MeshV1RelayHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0U};
    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

enum class MeshV1RelayReceiveDisposition : std::uint8_t {
    AcceptedResponsibility, AlreadyAccepted, DeadlineExpired, HopLimitExhausted,
    ResourceUnavailable, TrafficCapacityUnavailable, UnknownAuthenticatedSender, HopSessionUnavailable,
    ReplayRejected, AuthenticationFailed, NotForRelay, Invalid
};

struct MeshV1RelayReceiveResult final {
    MeshV1RelayReceiveDisposition Disposition{MeshV1RelayReceiveDisposition::Invalid};
    MeshV1RelayHandle Relay{};
    System::DeviceIdentifier PreviousHop{};
    MembershipIncarnation PreviousHopIncarnation{};
    MeshMessageId MessageId{0U};
    std::uint64_t AbsoluteDeadlineMilliseconds{0U};

    constexpr explicit operator bool() const noexcept {
        return Disposition == MeshV1RelayReceiveDisposition::AcceptedResponsibility ||
               Disposition == MeshV1RelayReceiveDisposition::AlreadyAccepted;
    }
};

enum class MeshV1RelaySubmissionDisposition : std::uint8_t {
    Submitted, AlreadySubmitted, DeadlineExpired, RouteMismatch, NextHopSessionUnavailable,
    WorkspaceCapacityExceeded, SequenceExhausted, ProtectionFailed, ForwardingFailed,
    UnknownRelay, Invalid
};

struct MeshV1RelaySubmissionResult final {
    MeshV1RelaySubmissionDisposition Disposition{MeshV1RelaySubmissionDisposition::Invalid};
    ForwardingSubmissionResult Submission{};
};

enum class MeshV1RelayAcceptanceDisposition : std::uint8_t {
    ResponsibilityTransferred, Unrelated, DeadlineExpired, UnknownRelay, Invalid
};

/// <summary>Fixed relay ownership for opaque EndToEnd frames between authenticated Hop transitions.</summary>
/// <remarks>
/// Incoming Hop replay is committed only after an EndToEnd frame has been validated structurally and retained in an
/// exact fixed slot. At that point the result authorizes composition to emit next-hop acceptance to PreviousHop. The
/// relay never opens EndToEnd protection. It retains responsibility across Radio retry and downstream waiting, and
/// releases the frame only after exact authenticated next-hop acceptance, immutable deadline expiry or controlled reset.
/// </remarks>
template<std::size_t RelayCapacity,
         std::size_t RetainedInnerBytes,
         std::size_t InnerWorkspaceBytes,
         std::size_t PacketWorkspaceBytes,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t HopCapacity = Limits::MaxRouteHops,
         std::size_t SessionCapacity = Limits::MaxMeshNodes>
class MeshV1RelayCoordinator final {
    static_assert(RelayCapacity > 0U && RelayCapacity < std::numeric_limits<std::uint16_t>::max(),
                  "Relay capacity must fit the generation-safe handle.");
    static_assert(RetainedInnerBytes > 0U, "Relay retained-frame bytes must be explicit and non-zero.");

    struct State final {
        bool Used{false};
        std::uint16_t Generation{0U};
        System::DeviceIdentifier Source{};
        MembershipIncarnation SourceIncarnation{};
        System::DeviceIdentifier Destination{};
        MembershipIncarnation DestinationIncarnation{};
        MeshMessageId MessageId{0U};
        std::uint64_t AbsoluteDeadlineMilliseconds{0U};
        RemainingHopLimit Remaining{0U};
        std::size_t InnerBytes{0U};
        std::array<std::uint8_t, RetainedInnerBytes> Inner{};
        ForwardingTransitionCoordinator Transition{};
        MeshTrafficReservation Traffic{};
    };

    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    IMeshTrafficGovernor& _traffic;
    ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& _forwarding;
    MeshV1FrameWorkspace<InnerWorkspaceBytes, PacketWorkspaceBytes>& _workspace;
    MeshIdentifier _mesh;
    System::DeviceIdentifier _localDevice;
    MembershipIncarnation _localIncarnation;
    std::array<State, RelayCapacity> _states{};

    static std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        const auto next = static_cast<std::uint16_t>(current + 1U);
        return next == 0U ? 1U : next;
    }
    void Clear(State& state) noexcept {
        const auto generation = state.Generation;
        if (state.Traffic) (void)_traffic.Release(state.Traffic);
        volatile std::uint8_t* bytes = state.Inner.data();
        for (std::size_t index = 0; index < state.Inner.size(); ++index) bytes[index] = 0U;
        state = {};
        state.Generation = generation;
    }
    State* Resolve(MeshV1RelayHandle handle) noexcept {
        if (!handle || handle.Slot >= RelayCapacity) return nullptr;
        auto& state = _states[handle.Slot];
        return state.Used && state.Generation == handle.Generation ? &state : nullptr;
    }

public:
    MeshV1RelayCoordinator(
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

    std::size_t Size() const noexcept {
        std::size_t count = 0U;
        for (const auto& state : _states) if (state.Used) ++count;
        return count;
    }

    MeshV1RelayReceiveResult Receive(
        const std::uint8_t* packet,
        std::size_t packetBytes,
        std::uint64_t nowMilliseconds
    ) noexcept {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        MeshV1HopFrameHeader hop{};
        MeshV1ProtectedFrameView hopFrame{};
        if (!_mesh || !_localDevice || !_localIncarnation || nowMilliseconds == 0U ||
            !MeshV1ProtectedFrameCodec::DecodeHop(packet, packetBytes, hop, hopFrame)) {
            return {MeshV1RelayReceiveDisposition::Invalid};
        }
        if (hop.Mesh != _mesh || hop.NextHop != _localDevice ||
            hop.NextHopIncarnation != _localIncarnation || hop.Destination == _localDevice) {
            return {MeshV1RelayReceiveDisposition::NotForRelay};
        }
        if (_memberships.FindExact(hop.Sender, hop.SenderIncarnation) == nullptr) {
            return {MeshV1RelayReceiveDisposition::UnknownAuthenticatedSender};
        }
        const auto hopSession = _sessions.Find(hop.Sender, hop.SenderIncarnation);
        if (!hopSession || _sessions.Identifier(hopSession).Value != hop.Session.Value) {
            return {MeshV1RelayReceiveDisposition::HopSessionUnavailable};
        }
        if (hopFrame.CiphertextBytes > RetainedInnerBytes) {
            return {MeshV1RelayReceiveDisposition::ResourceUnavailable};
        }
        auto* inner = _workspace.Inner(hopFrame.CiphertextBytes);
        if (inner == nullptr) return {MeshV1RelayReceiveDisposition::ResourceUnavailable};
        if (!_sessions.CanAcceptInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) {
            return {MeshV1RelayReceiveDisposition::ReplayRejected};
        }
        if (!_provider.Open(
                _sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, hop.Sequence,
                hopFrame.AuthenticatedHeader, hopFrame.AuthenticatedHeaderBytes,
                hopFrame.Ciphertext, hopFrame.CiphertextBytes, hopFrame.Tag, inner)) {
            return {MeshV1RelayReceiveDisposition::AuthenticationFailed};
        }
        MeshV1EndToEndFrameHeader endToEnd{};
        MeshV1ProtectedFrameView endToEndFrame{};
        if (!MeshV1ProtectedFrameCodec::DecodeEndToEnd(
                inner, hopFrame.CiphertextBytes, endToEnd, endToEndFrame) ||
            endToEnd.Mesh != hop.Mesh || endToEnd.Destination != hop.Destination ||
            endToEnd.DestinationIncarnation != hop.DestinationIncarnation ||
            endToEnd.MessageId != hop.MessageId) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1RelayReceiveDisposition::Invalid};
        }
        for (std::size_t index = 0; index < RelayCapacity; ++index) {
            const auto& state = _states[index];
            if (!state.Used || state.Source != endToEnd.Source ||
                state.SourceIncarnation != endToEnd.SourceIncarnation || state.MessageId != endToEnd.MessageId) continue;
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1RelayReceiveDisposition::AlreadyAccepted,
                    {static_cast<std::uint16_t>(index), state.Generation},
                    hop.Sender, hop.SenderIncarnation, hop.MessageId,
                    state.AbsoluteDeadlineMilliseconds};
        }
        if (nowMilliseconds >= endToEnd.AbsoluteDeadlineMilliseconds) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1RelayReceiveDisposition::DeadlineExpired};
        }
        if (hop.HopLimit <= 1U) {
            _sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence);
            return {MeshV1RelayReceiveDisposition::HopLimitExhausted};
        }
        State* available = nullptr;
        for (auto& state : _states) if (!state.Used) { available = &state; break; }
        if (available == nullptr) return {MeshV1RelayReceiveDisposition::ResourceUnavailable};
        MeshTrafficReservation traffic{};
        const auto trafficClass = endToEnd.PrimitiveFamily == Primitive::FamilyIds::MeshControl
            ? MeshTrafficClass::InfrastructureResponse
            : MeshTrafficClass::Application;
        if (_traffic.TryAcquire(trafficClass, traffic) != MeshTrafficAdmissionResult::Admitted) {
            return {MeshV1RelayReceiveDisposition::TrafficCapacityUnavailable};
        }
        available->Generation = NextGeneration(available->Generation);
        available->Used = true;
        available->Source = endToEnd.Source;
        available->SourceIncarnation = endToEnd.SourceIncarnation;
        available->Destination = endToEnd.Destination;
        available->DestinationIncarnation = endToEnd.DestinationIncarnation;
        available->MessageId = endToEnd.MessageId;
        available->AbsoluteDeadlineMilliseconds = endToEnd.AbsoluteDeadlineMilliseconds;
        available->Remaining = static_cast<RemainingHopLimit>(hop.HopLimit - 1U);
        available->InnerBytes = hopFrame.CiphertextBytes;
        available->Traffic = traffic;
        std::memcpy(available->Inner.data(), inner, available->InnerBytes);
        const auto slot = static_cast<std::size_t>(available - _states.data());
        const MeshV1RelayHandle handle{static_cast<std::uint16_t>(slot), available->Generation};
        if (!_sessions.CommitAuthenticatedInbound(hopSession, MeshSecurityTrafficPurpose::Hop, hop.Sequence)) {
            Clear(*available);
            return {MeshV1RelayReceiveDisposition::ReplayRejected};
        }
        return {MeshV1RelayReceiveDisposition::AcceptedResponsibility, handle,
                hop.Sender, hop.SenderIncarnation, hop.MessageId,
                endToEnd.AbsoluteDeadlineMilliseconds};
    }

    MeshV1RelaySubmissionResult Submit(
        MeshV1RelayHandle handle,
        const ResolvedRoute<HopCapacity>& route,
        std::uint64_t nowMilliseconds
    ) {
        MeshV1WorkspaceResetGuard<decltype(_workspace)> reset(_workspace);
        auto* state = Resolve(handle);
        if (state == nullptr) return {MeshV1RelaySubmissionDisposition::UnknownRelay, {}};
        if (state->Transition.HasPending()) {
            return {MeshV1RelaySubmissionDisposition::AlreadySubmitted, {}};
        }
        if (nowMilliseconds == 0U) return {MeshV1RelaySubmissionDisposition::Invalid, {}};
        if (nowMilliseconds >= state->AbsoluteDeadlineMilliseconds) {
            Clear(*state);
            return {MeshV1RelaySubmissionDisposition::DeadlineExpired, {}};
        }
        if (route.Source() != _localDevice || route.Destination() != state->Destination) {
            return {MeshV1RelaySubmissionDisposition::RouteMismatch, {}};
        }
        const auto* nextHop = route.NextHop();
        const auto* nextHopMembership = nextHop == nullptr ? nullptr : _memberships.FindDevice(nextHop->Neighbour);
        if (nextHop == nullptr || nextHop->Advertiser != _localDevice || nextHopMembership == nullptr ||
            !nextHopMembership->IsValid() || nextHopMembership->Reachability == ReachabilityState::Unreachable) {
            return {MeshV1RelaySubmissionDisposition::NextHopSessionUnavailable, {}};
        }
        const auto hopSession = _sessions.Find(nextHop->Neighbour, nextHopMembership->Incarnation);
        if (!hopSession) return {MeshV1RelaySubmissionDisposition::NextHopSessionUnavailable, {}};
        const auto packetBytes = MeshV1ProtectedFrameCodec::HopPacketBytes(state->InnerBytes);
        auto* packet = _workspace.Packet(packetBytes);
        if (packet == nullptr) return {MeshV1RelaySubmissionDisposition::WorkspaceCapacityExceeded, {}};
        const auto sequence = _sessions.IssueSequence(hopSession, MeshSecurityTrafficPurpose::Hop);
        if (sequence == 0U) return {MeshV1RelaySubmissionDisposition::SequenceExhausted, {}};
        const MeshV1HopFrameHeader hopHeader{
            _mesh, _sessions.Identifier(hopSession), sequence,
            _localDevice, _localIncarnation, nextHop->Neighbour, nextHopMembership->Incarnation,
            state->Destination, state->DestinationIncarnation, state->MessageId, state->Remaining,
            static_cast<std::uint16_t>(state->InnerBytes)};
        if (!MeshV1ProtectedFrameCodec::EncodeHopAuthenticatedHeader(hopHeader, packet, packetBytes)) {
            return {MeshV1RelaySubmissionDisposition::ProtectionFailed, {}};
        }
        MeshAuthenticationTag tag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(hopSession), MeshSecurityTrafficPurpose::Hop, sequence,
                packet, MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes,
                state->Inner.data(), state->InnerBytes,
                packet + MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes, tag)) {
            return {MeshV1RelaySubmissionDisposition::ProtectionFailed, {}};
        }
        std::memcpy(packet + packetBytes - tag.Value.size(), tag.Value.data(), tag.Value.size());
        if (state->Transition.Arm(
                nextHop->Neighbour, nextHopMembership->Incarnation, state->MessageId,
                nowMilliseconds, state->AbsoluteDeadlineMilliseconds) != ForwardingTransitionArmResult::Armed) {
            return {MeshV1RelaySubmissionDisposition::Invalid, {}};
        }
        ForwardingSubmissionResult submission{};
        try {
            submission = _forwarding.Submit(
                _localDevice, route, state->Remaining, packet, packetBytes,
                nowMilliseconds, state->AbsoluteDeadlineMilliseconds);
        } catch (...) {
            state->Transition.Cancel();
            throw;
        }
        if (!submission) state->Transition.Cancel();
        return {submission ? MeshV1RelaySubmissionDisposition::Submitted
                           : MeshV1RelaySubmissionDisposition::ForwardingFailed,
                submission};
    }

    MeshV1RelayAcceptanceDisposition AcceptNextHop(
        MeshV1RelayHandle handle,
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        const System::DeviceIdentifier& acknowledgedSource,
        const MembershipIncarnation& acknowledgedSourceIncarnation,
        MeshMessageId messageId,
        std::uint64_t nowMilliseconds
    ) noexcept {
        auto* state = Resolve(handle);
        if (state == nullptr) return MeshV1RelayAcceptanceDisposition::UnknownRelay;
        if (acknowledgedSource != state->Source ||
            acknowledgedSourceIncarnation != state->SourceIncarnation) {
            return MeshV1RelayAcceptanceDisposition::Unrelated;
        }
        const auto accepted = state->Transition.AcceptAuthenticated(
            authenticatedSource, authenticatedSourceIncarnation, messageId,
            nowMilliseconds, state->Remaining);
        switch (accepted) {
            case ForwardingAcceptanceResult::Committed:
                Clear(*state);
                return MeshV1RelayAcceptanceDisposition::ResponsibilityTransferred;
            case ForwardingAcceptanceResult::DeadlineExpired:
                Clear(*state);
                return MeshV1RelayAcceptanceDisposition::DeadlineExpired;
            case ForwardingAcceptanceResult::WrongNextHop:
            case ForwardingAcceptanceResult::WrongIncarnation:
            case ForwardingAcceptanceResult::WrongMessage:
            case ForwardingAcceptanceResult::NotPending:
                return MeshV1RelayAcceptanceDisposition::Unrelated;
            case ForwardingAcceptanceResult::HopLimitExhausted:
            case ForwardingAcceptanceResult::Invalid:
                return MeshV1RelayAcceptanceDisposition::Invalid;
        }
        return MeshV1RelayAcceptanceDisposition::Invalid;
    }

    std::size_t Expire(std::uint64_t nowMilliseconds) noexcept {
        if (nowMilliseconds == 0U) return 0U;
        std::size_t expired = 0U;
        for (auto& state : _states) {
            if (!state.Used || nowMilliseconds < state.AbsoluteDeadlineMilliseconds) continue;
            Clear(state);
            ++expired;
        }
        return expired;
    }

    void ResetForControlledShutdown() noexcept {
        for (auto& state : _states) if (state.Used) Clear(state);
    }
};

} // namespace ESPressio::Mesh
