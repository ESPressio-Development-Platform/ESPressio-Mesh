#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <ESPressio_PrimitiveFamilyRegistry.hpp>
#include <ESPressio_PrimitiveTypes.hpp>

#include "ESPressio_MeshV1Security.hpp"

namespace ESPressio::Mesh {

struct MeshV1BroadcastOriginHeader final {
    MeshIdentifier Mesh{};
    System::DeviceIdentifier Source{};
    MembershipIncarnation SourceIncarnation{};
    MeshMessageId MessageId{0U};
    std::uint64_t AbsoluteDeadlineMilliseconds{0U};
    Primitive::PrimitiveFamilyId PrimitiveFamily{Primitive::FamilyIds::Invalid};
    Primitive::PrimitiveProtocolVersion PrimitiveVersion{0U};
    std::uint16_t PayloadBytes{0U};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Source) &&
               static_cast<bool>(SourceIncarnation) && MessageId != 0U &&
               AbsoluteDeadlineMilliseconds != 0U &&
               Primitive::FamilyIds::IsUsable(PrimitiveFamily) &&
               PrimitiveFamily != Primitive::FamilyIds::MeshControl && PayloadBytes != 0U;
    }
};

struct MeshV1BroadcastHopHeader final {
    MeshIdentifier Mesh{};
    MeshSecuritySessionIdentifier Session{};
    std::uint64_t Sequence{0U};
    System::DeviceIdentifier Sender{};
    MembershipIncarnation SenderIncarnation{};
    System::DeviceIdentifier NextHop{};
    MembershipIncarnation NextHopIncarnation{};
    System::DeviceIdentifier Source{};
    MembershipIncarnation SourceIncarnation{};
    MeshMessageId MessageId{0U};
    RemainingHopLimit HopLimit{0U};
    std::uint16_t InnerFrameBytes{0U};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Session) && Sequence != 0U &&
               static_cast<bool>(Sender) && static_cast<bool>(SenderIncarnation) &&
               static_cast<bool>(NextHop) && static_cast<bool>(NextHopIncarnation) &&
               static_cast<bool>(Source) && static_cast<bool>(SourceIncarnation) &&
               MessageId != 0U && HopLimit != 0U && InnerFrameBytes != 0U;
    }
};

struct MeshV1BroadcastOriginView final {
    const std::uint8_t* SignedBytes{nullptr};
    std::size_t SignedByteCount{0U};
    const std::uint8_t* Payload{nullptr};
    std::size_t PayloadByteCount{0U};
    MeshIdentitySignature Signature{};
};

struct MeshV1BroadcastHopView final {
    const std::uint8_t* AuthenticatedHeader{nullptr};
    std::size_t AuthenticatedHeaderBytes{0U};
    const std::uint8_t* Ciphertext{nullptr};
    std::size_t CiphertextBytes{0U};
    MeshAuthenticationTag Tag{};
};

/// <summary>Canonical signed-origin and pairwise Hop-protected Mesh v1 Broadcast framing.</summary>
/// <remarks>
/// The immutable origin frame is signed by its claimed DeviceIdentifier and is never rewritten by relays. It provides
/// origin authentication and integrity, not secrecy from participating Mesh members. Every transition additionally
/// encrypts/authenticates that complete origin frame for one direct neighbour using the existing Hop session purpose.
/// Broadcast has no destination identity, end-to-end session, acknowledgement or shared delivery-success claim.
/// </remarks>
class MeshV1BroadcastFrameCodec final {
    static constexpr std::array<std::uint8_t, 4> Magic{{0x45U, 0x53U, 0x4DU, 0x31U}};
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
    static bool EncodeCommon(
        MeshV1SecurityMessageType type,
        std::size_t bodyBytes,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (output == nullptr || outputBytes < CommonHeaderBytes ||
            bodyBytes > std::numeric_limits<std::uint16_t>::max()) return false;
        std::memcpy(output, Magic.data(), Magic.size());
        output[4] = Version;
        output[5] = static_cast<std::uint8_t>(type);
        WriteU16(output + 6U, MeshV1SecuritySuite::Identifier);
        WriteU16(output + 8U, static_cast<std::uint16_t>(bodyBytes));
        return true;
    }
    static bool DecodeCommon(
        MeshV1SecurityMessageType type,
        const std::uint8_t* input,
        std::size_t inputBytes,
        std::size_t minimumBodyBytes
    ) noexcept {
        return input != nullptr && inputBytes >= CommonHeaderBytes + minimumBodyBytes &&
               inputBytes - CommonHeaderBytes <= std::numeric_limits<std::uint16_t>::max() &&
               std::memcmp(input, Magic.data(), Magic.size()) == 0 && input[4] == Version &&
               input[5] == static_cast<std::uint8_t>(type) &&
               ReadU16(input + 6U) == MeshV1SecuritySuite::Identifier &&
               ReadU16(input + 8U) == inputBytes - CommonHeaderBytes;
    }

public:
    static constexpr std::size_t CommonHeaderBytes = 10U;
    static constexpr std::size_t OriginFixedBodyBytes = 70U;
    static constexpr std::size_t OriginAuthenticatedHeaderBytes = CommonHeaderBytes + OriginFixedBodyBytes;
    static constexpr std::size_t HopFixedBodyBytes = 147U;
    static constexpr std::size_t HopAuthenticatedHeaderBytes = CommonHeaderBytes + HopFixedBodyBytes;

    static constexpr std::size_t OriginPacketBytes(std::size_t payloadBytes) noexcept {
        const auto body = OriginFixedBodyBytes + payloadBytes + MeshV1SecuritySuite::IdentitySignatureBytes;
        return payloadBytes != 0U && body <= std::numeric_limits<std::uint16_t>::max()
            ? CommonHeaderBytes + body : 0U;
    }
    static constexpr std::size_t HopPacketBytes(std::size_t innerFrameBytes) noexcept {
        const auto body = HopFixedBodyBytes + innerFrameBytes + MeshV1SecuritySuite::AuthenticationTagBytes;
        return innerFrameBytes != 0U && innerFrameBytes <= std::numeric_limits<std::uint16_t>::max() &&
               body <= std::numeric_limits<std::uint16_t>::max() ? CommonHeaderBytes + body : 0U;
    }

    static bool EncodeOriginAuthenticatedHeader(
        const MeshV1BroadcastOriginHeader& header,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        const auto packetBytes = OriginPacketBytes(header.PayloadBytes);
        if (!header.IsValid() || packetBytes == 0U || outputBytes < OriginAuthenticatedHeaderBytes ||
            !EncodeCommon(MeshV1SecurityMessageType::BroadcastOriginFrame,
                          packetBytes - CommonHeaderBytes, output, outputBytes)) return false;
        auto* cursor = output + CommonHeaderBytes;
        Copy(cursor, header.Mesh.Bytes().data(), header.Mesh.Bytes().size());
        Copy(cursor, header.Source.Bytes().data(), header.Source.Bytes().size());
        Copy(cursor, header.SourceIncarnation.Bytes().data(), header.SourceIncarnation.Bytes().size());
        WriteU64(cursor, header.MessageId); cursor += 8U;
        WriteU64(cursor, header.AbsoluteDeadlineMilliseconds); cursor += 8U;
        WriteU16(cursor, header.PrimitiveFamily); cursor += 2U;
        WriteU16(cursor, header.PrimitiveVersion); cursor += 2U;
        WriteU16(cursor, header.PayloadBytes);
        return true;
    }

    static bool DecodeOrigin(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1BroadcastOriginHeader& header,
        MeshV1BroadcastOriginView& view
    ) noexcept {
        header = {};
        view = {};
        if (!DecodeCommon(MeshV1SecurityMessageType::BroadcastOriginFrame, input, inputBytes,
                          OriginFixedBodyBytes + MeshV1SecuritySuite::IdentitySignatureBytes)) return false;
        const auto* cursor = input + CommonHeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage source{};
        MembershipIncarnation::Storage sourceIncarnation{};
        Read(cursor, mesh.data(), mesh.size()); header.Mesh = MeshIdentifier{mesh};
        Read(cursor, source.data(), source.size()); header.Source = System::DeviceIdentifier{source};
        Read(cursor, sourceIncarnation.data(), sourceIncarnation.size());
        header.SourceIncarnation = MembershipIncarnation{sourceIncarnation};
        header.MessageId = ReadU64(cursor); cursor += 8U;
        header.AbsoluteDeadlineMilliseconds = ReadU64(cursor); cursor += 8U;
        header.PrimitiveFamily = ReadU16(cursor); cursor += 2U;
        header.PrimitiveVersion = ReadU16(cursor); cursor += 2U;
        header.PayloadBytes = ReadU16(cursor); cursor += 2U;
        if (!header.IsValid() || OriginPacketBytes(header.PayloadBytes) != inputBytes) return false;
        view.SignedBytes = input;
        view.SignedByteCount = OriginAuthenticatedHeaderBytes + header.PayloadBytes;
        view.Payload = cursor;
        view.PayloadByteCount = header.PayloadBytes;
        std::memcpy(view.Signature.Value.data(), cursor + header.PayloadBytes, view.Signature.Value.size());
        return static_cast<bool>(view.Signature);
    }

    static bool EncodeHopAuthenticatedHeader(
        const MeshV1BroadcastHopHeader& header,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        const auto packetBytes = HopPacketBytes(header.InnerFrameBytes);
        if (!header.IsValid() || packetBytes == 0U || outputBytes < HopAuthenticatedHeaderBytes ||
            !EncodeCommon(MeshV1SecurityMessageType::BroadcastHopFrame,
                          packetBytes - CommonHeaderBytes, output, outputBytes)) return false;
        auto* cursor = output + CommonHeaderBytes;
        Copy(cursor, header.Mesh.Bytes().data(), header.Mesh.Bytes().size());
        Copy(cursor, header.Session.Value.data(), header.Session.Value.size());
        WriteU64(cursor, header.Sequence); cursor += 8U;
        Copy(cursor, header.Sender.Bytes().data(), header.Sender.Bytes().size());
        Copy(cursor, header.SenderIncarnation.Bytes().data(), header.SenderIncarnation.Bytes().size());
        Copy(cursor, header.NextHop.Bytes().data(), header.NextHop.Bytes().size());
        Copy(cursor, header.NextHopIncarnation.Bytes().data(), header.NextHopIncarnation.Bytes().size());
        Copy(cursor, header.Source.Bytes().data(), header.Source.Bytes().size());
        Copy(cursor, header.SourceIncarnation.Bytes().data(), header.SourceIncarnation.Bytes().size());
        WriteU64(cursor, header.MessageId); cursor += 8U;
        *cursor++ = header.HopLimit;
        WriteU16(cursor, header.InnerFrameBytes);
        return true;
    }

    static bool DecodeHop(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1BroadcastHopHeader& header,
        MeshV1BroadcastHopView& view
    ) noexcept {
        header = {};
        view = {};
        if (!DecodeCommon(MeshV1SecurityMessageType::BroadcastHopFrame, input, inputBytes,
                          HopFixedBodyBytes + MeshV1SecuritySuite::AuthenticationTagBytes)) return false;
        const auto ciphertextBytes = inputBytes - HopAuthenticatedHeaderBytes -
                                     MeshV1SecuritySuite::AuthenticationTagBytes;
        if (ciphertextBytes == 0U || ciphertextBytes > std::numeric_limits<std::uint16_t>::max()) return false;
        const auto* cursor = input + CommonHeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage sender{};
        MembershipIncarnation::Storage senderIncarnation{};
        System::DeviceIdentifier::Storage nextHop{};
        MembershipIncarnation::Storage nextHopIncarnation{};
        System::DeviceIdentifier::Storage source{};
        MembershipIncarnation::Storage sourceIncarnation{};
        Read(cursor, mesh.data(), mesh.size()); header.Mesh = MeshIdentifier{mesh};
        Read(cursor, header.Session.Value.data(), header.Session.Value.size());
        header.Sequence = ReadU64(cursor); cursor += 8U;
        Read(cursor, sender.data(), sender.size()); header.Sender = System::DeviceIdentifier{sender};
        Read(cursor, senderIncarnation.data(), senderIncarnation.size());
        header.SenderIncarnation = MembershipIncarnation{senderIncarnation};
        Read(cursor, nextHop.data(), nextHop.size()); header.NextHop = System::DeviceIdentifier{nextHop};
        Read(cursor, nextHopIncarnation.data(), nextHopIncarnation.size());
        header.NextHopIncarnation = MembershipIncarnation{nextHopIncarnation};
        Read(cursor, source.data(), source.size()); header.Source = System::DeviceIdentifier{source};
        Read(cursor, sourceIncarnation.data(), sourceIncarnation.size());
        header.SourceIncarnation = MembershipIncarnation{sourceIncarnation};
        header.MessageId = ReadU64(cursor); cursor += 8U;
        header.HopLimit = *cursor++;
        header.InnerFrameBytes = ReadU16(cursor); cursor += 2U;
        if (!header.IsValid() || header.InnerFrameBytes != ciphertextBytes) return false;
        view.AuthenticatedHeader = input;
        view.AuthenticatedHeaderBytes = HopAuthenticatedHeaderBytes;
        view.Ciphertext = cursor;
        view.CiphertextBytes = ciphertextBytes;
        std::memcpy(view.Tag.Value.data(), cursor + ciphertextBytes, view.Tag.Value.size());
        return true;
    }
};

static_assert(MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes == 80U);
static_assert(MeshV1BroadcastFrameCodec::HopAuthenticatedHeaderBytes == 157U);

} // namespace ESPressio::Mesh
