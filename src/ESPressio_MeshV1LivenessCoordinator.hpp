#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_LivenessProbeCoordinator.hpp"
#include "ESPressio_MeshSecuritySessionTable.hpp"
#include "ESPressio_MeshV1Security.hpp"

namespace ESPressio::Mesh {

/// <summary>Protected direct-neighbour liveness control message.</summary>
enum class MeshV1LivenessMessageType : std::uint8_t {
    Probe = 1U,
    Response = 2U,
    GracefulDisconnect = 3U
};

struct MeshV1LivenessHeader final {
    MeshIdentifier Mesh{};
    MeshSecuritySessionIdentifier Session{};
    std::uint64_t Sequence{0U};
    System::DeviceIdentifier Sender{};
    MembershipIncarnation SenderIncarnation{};
    System::DeviceIdentifier Recipient{};
    MembershipIncarnation RecipientIncarnation{};
    MeshV1LivenessMessageType Type{MeshV1LivenessMessageType::Probe};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Session) && Sequence != 0U &&
               static_cast<bool>(Sender) && static_cast<bool>(SenderIncarnation) &&
               static_cast<bool>(Recipient) && static_cast<bool>(RecipientIncarnation) &&
               (Type == MeshV1LivenessMessageType::Probe ||
                Type == MeshV1LivenessMessageType::Response ||
                Type == MeshV1LivenessMessageType::GracefulDisconnect);
    }
};

struct MeshV1LivenessFrameView final {
    const std::uint8_t* AuthenticatedHeader{nullptr};
    std::size_t AuthenticatedHeaderBytes{0U};
    const std::uint8_t* Ciphertext{nullptr};
    std::size_t CiphertextBytes{0U};
    MeshAuthenticationTag Tag{};
};

/// <summary>Compact pairwise-session-protected Mesh v1 liveness/control codec.</summary>
/// <remarks>
/// Liveness deliberately uses a distinct control-plane frame rather than application Broadcast traffic. The
/// pairwise Hop traffic key authenticates the immediate sender, while the independent sequence/replay window in
/// MeshSecuritySessionTable prevents replay from manufacturing fresh liveness. The encrypted eight-byte token is
/// echoed by a response and has no authority beyond correlating the probe. No System Clock timestamp is required:
/// liveness scheduling and evidence age remain monotonic-time concerns.
/// </remarks>
class MeshV1LivenessFrameCodec final {
    inline static constexpr std::array<std::uint8_t, 4U> Magic{{0x45U, 0x53U, 0x4CU, 0x56U}}; // ESLV
    static constexpr std::uint8_t Version = 1U;

    static void WriteU16(std::uint8_t* output, std::uint16_t value) noexcept {
        output[0] = static_cast<std::uint8_t>(value >> 8U);
        output[1] = static_cast<std::uint8_t>(value);
    }
    static void WriteU64(std::uint8_t* output, std::uint64_t value) noexcept {
        for (std::size_t index = 0U; index < 8U; ++index) {
            output[index] = static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
        }
    }
    static std::uint16_t ReadU16(const std::uint8_t* input) noexcept {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[0]) << 8U) | input[1]);
    }
    static std::uint64_t ReadU64(const std::uint8_t* input) noexcept {
        std::uint64_t value = 0U;
        for (std::size_t index = 0U; index < 8U; ++index) value = (value << 8U) | input[index];
        return value;
    }
    static void Copy(std::uint8_t*& output, const std::uint8_t* input, std::size_t size) noexcept {
        std::memcpy(output, input, size);
        output += size;
    }
    static void Read(const std::uint8_t*& input, std::uint8_t* output, std::size_t size) noexcept {
        std::memcpy(output, input, size);
        input += size;
    }

public:
    static constexpr std::size_t CommonHeaderBytes = 10U;
    static constexpr std::size_t FixedBodyBytes = 104U;
    static constexpr std::size_t AuthenticatedHeaderBytes = CommonHeaderBytes + FixedBodyBytes;
    static constexpr std::size_t TokenBytes = 8U;
    static constexpr std::size_t PacketBytes =
        AuthenticatedHeaderBytes + TokenBytes + MeshV1SecuritySuite::AuthenticationTagBytes;

    static bool IsFrame(const std::uint8_t* input, std::size_t inputBytes) noexcept {
        return input != nullptr && inputBytes == PacketBytes &&
               std::memcmp(input, Magic.data(), Magic.size()) == 0 && input[4] == Version;
    }

    static bool EncodeAuthenticatedHeader(
        const MeshV1LivenessHeader& header,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (!header.IsValid() || output == nullptr || outputBytes < PacketBytes) return false;
        std::memcpy(output, Magic.data(), Magic.size());
        output[4] = Version;
        output[5] = static_cast<std::uint8_t>(header.Type);
        WriteU16(output + 6U, MeshV1SecuritySuite::Identifier);
        WriteU16(output + 8U, static_cast<std::uint16_t>(PacketBytes - CommonHeaderBytes));
        auto* cursor = output + CommonHeaderBytes;
        Copy(cursor, header.Mesh.Bytes().data(), header.Mesh.Bytes().size());
        Copy(cursor, header.Session.Value.data(), header.Session.Value.size());
        WriteU64(cursor, header.Sequence); cursor += 8U;
        Copy(cursor, header.Sender.Bytes().data(), header.Sender.Bytes().size());
        Copy(cursor, header.SenderIncarnation.Bytes().data(), header.SenderIncarnation.Bytes().size());
        Copy(cursor, header.Recipient.Bytes().data(), header.Recipient.Bytes().size());
        Copy(cursor, header.RecipientIncarnation.Bytes().data(), header.RecipientIncarnation.Bytes().size());
        return static_cast<std::size_t>(cursor - output) == AuthenticatedHeaderBytes;
    }

    static bool Decode(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1LivenessHeader& header,
        MeshV1LivenessFrameView& view
    ) noexcept {
        header = {};
        view = {};
        if (!IsFrame(input, inputBytes) || ReadU16(input + 6U) != MeshV1SecuritySuite::Identifier ||
            ReadU16(input + 8U) != PacketBytes - CommonHeaderBytes) return false;
        const auto type = static_cast<MeshV1LivenessMessageType>(input[5U]);
        if (type != MeshV1LivenessMessageType::Probe &&
            type != MeshV1LivenessMessageType::Response &&
            type != MeshV1LivenessMessageType::GracefulDisconnect) return false;
        const auto* cursor = input + CommonHeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage sender{};
        MembershipIncarnation::Storage senderIncarnation{};
        System::DeviceIdentifier::Storage recipient{};
        MembershipIncarnation::Storage recipientIncarnation{};
        Read(cursor, mesh.data(), mesh.size()); header.Mesh = MeshIdentifier{mesh};
        Read(cursor, header.Session.Value.data(), header.Session.Value.size());
        header.Sequence = ReadU64(cursor); cursor += 8U;
        Read(cursor, sender.data(), sender.size()); header.Sender = System::DeviceIdentifier{sender};
        Read(cursor, senderIncarnation.data(), senderIncarnation.size());
        header.SenderIncarnation = MembershipIncarnation{senderIncarnation};
        Read(cursor, recipient.data(), recipient.size()); header.Recipient = System::DeviceIdentifier{recipient};
        Read(cursor, recipientIncarnation.data(), recipientIncarnation.size());
        header.RecipientIncarnation = MembershipIncarnation{recipientIncarnation};
        header.Type = type;
        if (!header.IsValid() || static_cast<std::size_t>(cursor - input) != AuthenticatedHeaderBytes) {
            header = {};
            return false;
        }
        view.AuthenticatedHeader = input;
        view.AuthenticatedHeaderBytes = AuthenticatedHeaderBytes;
        view.Ciphertext = input + AuthenticatedHeaderBytes;
        view.CiphertextBytes = TokenBytes;
        std::memcpy(view.Tag.Value.data(), input + AuthenticatedHeaderBytes + TokenBytes,
                    view.Tag.Value.size());
        return true;
    }

    static void EncodeToken(std::uint64_t token, std::uint8_t* output) noexcept { WriteU64(output, token); }
    static std::uint64_t DecodeToken(const std::uint8_t* input) noexcept { return ReadU64(input); }
};

enum class MeshV1LivenessReceiveDisposition : std::uint8_t {
    ProbeAuthenticated,
    ResponseAuthenticated,
    GracefulDisconnectAuthenticated,
    NotLivenessFrame,
    NotForLocalNode,
    UnknownAuthenticatedSender,
    ReplayRejected,
    AuthenticationFailed,
    ResponseSendFailed,
    Invalid
};

struct MeshV1LivenessReceiveResult final {
    MeshV1LivenessReceiveDisposition Disposition{MeshV1LivenessReceiveDisposition::Invalid};
    System::DeviceIdentifier Sender{};
    MembershipIncarnation SenderIncarnation{};
    std::uint64_t Token{0U};

    constexpr bool IsAuthenticatedEvidence() const noexcept {
        return Disposition == MeshV1LivenessReceiveDisposition::ProbeAuthenticated ||
               Disposition == MeshV1LivenessReceiveDisposition::ResponseAuthenticated ||
               Disposition == MeshV1LivenessReceiveDisposition::GracefulDisconnectAuthenticated ||
               Disposition == MeshV1LivenessReceiveDisposition::ResponseSendFailed;
    }
};

/// <summary>Protected direct-neighbour Mesh v1 liveness probe initiator/responder.</summary>
/// <remarks>
/// The coordinator authenticates only the immediate neighbour using the already-established pairwise Mesh session.
/// It neither owns membership reachability state nor manufactures liveness evidence: callers pass successful receive
/// results to MembershipLivenessTracker only after this coordinator reports authenticated evidence. Probe scheduling
/// remains owned by LivenessProbeCoordinator/IMeshLivenessProbePolicy.
/// </remarks>
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks,
    std::size_t SessionCapacity = Limits::MaxMeshNodes
>
class MeshV1LivenessCoordinator final : public ILivenessProbeInitiator {
    AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    const AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;
    MeshSecuritySessionTable<SessionCapacity>& _sessions;
    IMeshV1CryptographicProvider& _provider;
    Radio::RadioTransport& _transport;
    MeshIdentifier _mesh{};
    System::DeviceIdentifier _localDevice{};
    MembershipIncarnation _localIncarnation{};
    std::uint64_t _nextToken{1U};

    const AuthenticatedDirectPeerBinding* FindBinding(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        for (RadioIdentifier radio = 1U; radio <= Limits::MaxRadiosPerNode; ++radio) {
            if (const auto* binding = _bindings.Resolve(radio, device, incarnation)) return binding;
        }
        return nullptr;
    }

    bool Send(
        MeshV1LivenessMessageType type,
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t token
    ) noexcept {
        if (!_mesh || !_localDevice || !_localIncarnation || !device || !incarnation || token == 0U) return false;
        const auto* member = _memberships.FindExact(device, incarnation);
        const auto* binding = FindBinding(device, incarnation);
        const auto session = _sessions.Find(device, incarnation);
        if (member == nullptr || member->State != MembershipState::Active || binding == nullptr || !session) return false;
        const auto sequence = _sessions.IssueSequence(session, MeshSecurityTrafficPurpose::Hop);
        if (sequence == 0U) return false;

        std::array<std::uint8_t, MeshV1LivenessFrameCodec::PacketBytes> packet{};
        const MeshV1LivenessHeader header{
            _mesh, _sessions.Identifier(session), sequence,
            _localDevice, _localIncarnation, device, incarnation, type
        };
        if (!MeshV1LivenessFrameCodec::EncodeAuthenticatedHeader(header, packet.data(), packet.size())) return false;
        std::array<std::uint8_t, MeshV1LivenessFrameCodec::TokenBytes> plaintext{};
        MeshV1LivenessFrameCodec::EncodeToken(token, plaintext.data());
        MeshAuthenticationTag tag{};
        if (!_provider.Seal(
                _sessions.ProviderSession(session), MeshSecurityTrafficPurpose::Hop, sequence,
                packet.data(), MeshV1LivenessFrameCodec::AuthenticatedHeaderBytes,
                plaintext.data(), plaintext.size(),
                packet.data() + MeshV1LivenessFrameCodec::AuthenticatedHeaderBytes, tag)) return false;
        std::memcpy(packet.data() + MeshV1LivenessFrameCodec::AuthenticatedHeaderBytes + plaintext.size(),
                    tag.Value.data(), tag.Value.size());
        return _transport.Send(binding->Peer, packet.data(), packet.size()).Status ==
               Radio::RadioTransportSendStatus::Accepted;
    }

    std::uint64_t IssueToken() noexcept {
        if (_nextToken == 0U) _nextToken = 1U;
        const auto issued = _nextToken++;
        if (_nextToken == 0U) _nextToken = 1U;
        return issued;
    }

public:
    MeshV1LivenessCoordinator(
        AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        const AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings,
        MeshSecuritySessionTable<SessionCapacity>& sessions,
        IMeshV1CryptographicProvider& provider,
        Radio::RadioTransport& transport,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& localDevice,
        const MembershipIncarnation& localIncarnation
    ) noexcept :
        _memberships(memberships), _bindings(bindings), _sessions(sessions), _provider(provider),
        _transport(transport), _mesh(mesh), _localDevice(localDevice), _localIncarnation(localIncarnation) {}

    LivenessProbeStartDisposition TryStartProbe(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        LivenessProbeReservation
    ) noexcept override {
        const auto* member = _memberships.FindExact(device, incarnation);
        if (member == nullptr || member->State != MembershipState::Active) {
            return LivenessProbeStartDisposition::Rejected;
        }
        return Send(MeshV1LivenessMessageType::Probe, device, incarnation, IssueToken())
            ? LivenessProbeStartDisposition::Started
            : LivenessProbeStartDisposition::TemporarilyUnavailable;
    }

    bool SendGracefulDisconnect(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        return Send(MeshV1LivenessMessageType::GracefulDisconnect, device, incarnation, IssueToken());
    }

    MeshV1LivenessReceiveResult Receive(const std::uint8_t* packet, std::size_t packetBytes) noexcept {
        MeshV1LivenessReceiveResult result{};
        MeshV1LivenessHeader header{};
        MeshV1LivenessFrameView view{};
        if (!MeshV1LivenessFrameCodec::IsFrame(packet, packetBytes)) {
            result.Disposition = MeshV1LivenessReceiveDisposition::NotLivenessFrame;
            return result;
        }
        if (!MeshV1LivenessFrameCodec::Decode(packet, packetBytes, header, view)) return result;
        result.Sender = header.Sender;
        result.SenderIncarnation = header.SenderIncarnation;
        if (header.Mesh != _mesh || header.Recipient != _localDevice ||
            header.RecipientIncarnation != _localIncarnation) {
            result.Disposition = MeshV1LivenessReceiveDisposition::NotForLocalNode;
            return result;
        }
        const auto* member = _memberships.FindExact(header.Sender, header.SenderIncarnation);
        if (member == nullptr || member->State != MembershipState::Active) {
            result.Disposition = MeshV1LivenessReceiveDisposition::UnknownAuthenticatedSender;
            return result;
        }
        const auto session = _sessions.Find(header.Sender, header.SenderIncarnation);
        if (!session || _sessions.Identifier(session).Value != header.Session.Value ||
            !_sessions.CanAcceptInbound(session, MeshSecurityTrafficPurpose::Hop, header.Sequence)) {
            result.Disposition = MeshV1LivenessReceiveDisposition::ReplayRejected;
            return result;
        }
        std::array<std::uint8_t, MeshV1LivenessFrameCodec::TokenBytes> plaintext{};
        if (!_provider.Open(
                _sessions.ProviderSession(session), MeshSecurityTrafficPurpose::Hop, header.Sequence,
                view.AuthenticatedHeader, view.AuthenticatedHeaderBytes,
                view.Ciphertext, view.CiphertextBytes, view.Tag, plaintext.data())) {
            result.Disposition = MeshV1LivenessReceiveDisposition::AuthenticationFailed;
            return result;
        }
        if (!_sessions.CommitAuthenticatedInbound(session, MeshSecurityTrafficPurpose::Hop, header.Sequence)) {
            result.Disposition = MeshV1LivenessReceiveDisposition::ReplayRejected;
            return result;
        }
        result.Token = MeshV1LivenessFrameCodec::DecodeToken(plaintext.data());
        if (result.Token == 0U) {
            result.Disposition = MeshV1LivenessReceiveDisposition::AuthenticationFailed;
            return result;
        }

        switch (header.Type) {
            case MeshV1LivenessMessageType::Probe:
                result.Disposition = Send(
                    MeshV1LivenessMessageType::Response,
                    header.Sender, header.SenderIncarnation, result.Token)
                    ? MeshV1LivenessReceiveDisposition::ProbeAuthenticated
                    : MeshV1LivenessReceiveDisposition::ResponseSendFailed;
                break;
            case MeshV1LivenessMessageType::Response:
                result.Disposition = MeshV1LivenessReceiveDisposition::ResponseAuthenticated;
                break;
            case MeshV1LivenessMessageType::GracefulDisconnect:
                result.Disposition = MeshV1LivenessReceiveDisposition::GracefulDisconnectAuthenticated;
                break;
        }
        return result;
    }
};

/// <summary>Default policy keeping authenticated direct neighbours inside the liveness floor without application traffic.</summary>
class DefaultMeshLivenessProbePolicy final : public IMeshLivenessProbePolicy {
    std::uint64_t _probeIntervalMilliseconds{2000U};

public:
    explicit constexpr DefaultMeshLivenessProbePolicy(
        std::uint64_t probeIntervalMilliseconds = 2000U
    ) noexcept : _probeIntervalMilliseconds(probeIntervalMilliseconds) {}

    bool ShouldProbe(const LivenessProbeAssessment& assessment) const noexcept override {
        if (assessment.Membership != MembershipState::Active || _probeIntervalMilliseconds == 0U) return false;
        return !assessment.HasAuthenticatedEvidence ||
               assessment.EvidenceAgeMilliseconds >= _probeIntervalMilliseconds;
    }
};

static_assert(MeshV1LivenessFrameCodec::PacketBytes == 138U);

} // namespace ESPressio::Mesh
