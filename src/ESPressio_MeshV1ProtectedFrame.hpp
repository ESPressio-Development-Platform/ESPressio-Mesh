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

struct MeshV1EndToEndFrameHeader final {
    MeshIdentifier Mesh{};
    MeshSecuritySessionIdentifier Session{};
    std::uint64_t Sequence{0};
    System::DeviceIdentifier Source{};
    MembershipIncarnation SourceIncarnation{};
    System::DeviceIdentifier Destination{};
    MembershipIncarnation DestinationIncarnation{};
    MeshMessageId MessageId{0};
    Primitive::PrimitiveFamilyId PrimitiveFamily{Primitive::FamilyIds::Invalid};
    Primitive::PrimitiveProtocolVersion PrimitiveVersion{0};
    std::uint16_t PlaintextBytes{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Session) && Sequence != 0U &&
               static_cast<bool>(Source) && static_cast<bool>(SourceIncarnation) &&
               static_cast<bool>(Destination) && static_cast<bool>(DestinationIncarnation) &&
               MessageId != 0U && Primitive::FamilyIds::IsUsable(PrimitiveFamily);
    }
};

struct MeshV1HopFrameHeader final {
    MeshIdentifier Mesh{};
    MeshSecuritySessionIdentifier Session{};
    std::uint64_t Sequence{0};
    System::DeviceIdentifier Sender{};
    MembershipIncarnation SenderIncarnation{};
    System::DeviceIdentifier NextHop{};
    MembershipIncarnation NextHopIncarnation{};
    System::DeviceIdentifier Destination{};
    MembershipIncarnation DestinationIncarnation{};
    MeshMessageId MessageId{0};
    RemainingHopLimit HopLimit{0};
    std::uint16_t InnerFrameBytes{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Session) && Sequence != 0U &&
               static_cast<bool>(Sender) && static_cast<bool>(SenderIncarnation) &&
               static_cast<bool>(NextHop) && static_cast<bool>(NextHopIncarnation) &&
               static_cast<bool>(Destination) && static_cast<bool>(DestinationIncarnation) &&
               MessageId != 0U && HopLimit != 0U && InnerFrameBytes != 0U;
    }
};

struct MeshV1ProtectedFrameView final {
    const std::uint8_t* AuthenticatedHeader{nullptr};
    std::size_t AuthenticatedHeaderBytes{0};
    const std::uint8_t* Ciphertext{nullptr};
    std::size_t CiphertextBytes{0};
    MeshAuthenticationTag Tag{};
};

/// <summary>Canonical layered Mesh v1 protected-frame codec.</summary>
/// <remarks>
/// An EndToEnd frame authenticates and encrypts the immutable application/control payload and its canonical source,
/// destination, incarnation, MessageId and primitive identity. Each forwarding transition wraps that complete frame in
/// a Hop frame protected by the current direct-neighbour session. A relay opens only the Hop layer, decrements HopLimit
/// through the existing forwarding transition, chooses the next hop, and seals a replacement Hop layer; it cannot open
/// or alter the EndToEnd frame. The final destination requires all duplicated destination/MessageId values to match.
/// Radio receives the complete Hop frame as one opaque logical transfer and remains the sole fragmentation owner.
/// </remarks>
class MeshV1ProtectedFrameCodec final {
    static constexpr std::array<std::uint8_t, 4> Magic{{0x45U, 0x53U, 0x4DU, 0x31U}};
    static constexpr std::uint8_t Version = 1U;

    static void WriteU16(std::uint8_t* output, std::uint16_t value) noexcept {
        output[0] = static_cast<std::uint8_t>(value >> 8U);
        output[1] = static_cast<std::uint8_t>(value);
    }
    static void WriteU64(std::uint8_t* output, std::uint64_t value) noexcept {
        for (std::size_t index = 0; index < 8U; ++index) {
            output[index] = static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
        }
    }
    static std::uint16_t ReadU16(const std::uint8_t* input) noexcept {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[0]) << 8U) | input[1]);
    }
    static std::uint64_t ReadU64(const std::uint8_t* input) noexcept {
        std::uint64_t value = 0U;
        for (std::size_t index = 0; index < 8U; ++index) value = (value << 8U) | input[index];
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
        if (output == nullptr || outputBytes < CommonHeaderBytes || bodyBytes > std::numeric_limits<std::uint16_t>::max()) return false;
        std::memcpy(output, Magic.data(), Magic.size());
        output[4] = Version;
        output[5] = static_cast<std::uint8_t>(type);
        WriteU16(output + 6, MeshV1SecuritySuite::Identifier);
        WriteU16(output + 8, static_cast<std::uint16_t>(bodyBytes));
        return true;
    }
    static bool DecodeCommon(
        MeshV1SecurityMessageType type,
        const std::uint8_t* input,
        std::size_t inputBytes,
        std::size_t fixedBodyBytes,
        std::size_t& ciphertextBytes
    ) noexcept {
        ciphertextBytes = 0U;
        if (input == nullptr || inputBytes < CommonHeaderBytes + fixedBodyBytes + MeshV1SecuritySuite::AuthenticationTagBytes ||
            std::memcmp(input, Magic.data(), Magic.size()) != 0 || input[4] != Version ||
            input[5] != static_cast<std::uint8_t>(type) || ReadU16(input + 6) != MeshV1SecuritySuite::Identifier ||
            ReadU16(input + 8) != inputBytes - CommonHeaderBytes) return false;
        ciphertextBytes = inputBytes - CommonHeaderBytes - fixedBodyBytes - MeshV1SecuritySuite::AuthenticationTagBytes;
        return true;
    }

public:
    static constexpr std::size_t CommonHeaderBytes = 10U;
    static constexpr std::size_t EndToEndFixedBodyBytes = 118U;
    static constexpr std::size_t EndToEndAuthenticatedHeaderBytes = CommonHeaderBytes + EndToEndFixedBodyBytes;
    static constexpr std::size_t HopFixedBodyBytes = 147U;
    static constexpr std::size_t HopAuthenticatedHeaderBytes = CommonHeaderBytes + HopFixedBodyBytes;

    static constexpr std::size_t EndToEndPacketBytes(std::size_t plaintextBytes) noexcept {
        const auto body = EndToEndFixedBodyBytes + plaintextBytes + MeshV1SecuritySuite::AuthenticationTagBytes;
        return body <= std::numeric_limits<std::uint16_t>::max() ? CommonHeaderBytes + body : 0U;
    }
    static constexpr std::size_t HopPacketBytes(std::size_t innerFrameBytes) noexcept {
        const auto body = HopFixedBodyBytes + innerFrameBytes + MeshV1SecuritySuite::AuthenticationTagBytes;
        return innerFrameBytes != 0U && innerFrameBytes <= std::numeric_limits<std::uint16_t>::max() &&
               body <= std::numeric_limits<std::uint16_t>::max() ? CommonHeaderBytes + body : 0U;
    }

    static bool EncodeEndToEndAuthenticatedHeader(
        const MeshV1EndToEndFrameHeader& header,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        const auto packetBytes = EndToEndPacketBytes(header.PlaintextBytes);
        if (!header.IsValid() || packetBytes == 0U || outputBytes < EndToEndAuthenticatedHeaderBytes ||
            !EncodeCommon(MeshV1SecurityMessageType::EndToEndProtectedFrame,
                          packetBytes - CommonHeaderBytes, output, outputBytes)) return false;
        auto* cursor = output + CommonHeaderBytes;
        Copy(cursor, header.Mesh.Bytes().data(), header.Mesh.Bytes().size());
        Copy(cursor, header.Session.Value.data(), header.Session.Value.size());
        WriteU64(cursor, header.Sequence); cursor += 8U;
        Copy(cursor, header.Source.Bytes().data(), header.Source.Bytes().size());
        Copy(cursor, header.SourceIncarnation.Bytes().data(), header.SourceIncarnation.Bytes().size());
        Copy(cursor, header.Destination.Bytes().data(), header.Destination.Bytes().size());
        Copy(cursor, header.DestinationIncarnation.Bytes().data(), header.DestinationIncarnation.Bytes().size());
        WriteU64(cursor, header.MessageId); cursor += 8U;
        WriteU16(cursor, header.PrimitiveFamily); cursor += 2U;
        WriteU16(cursor, header.PrimitiveVersion); cursor += 2U;
        WriteU16(cursor, header.PlaintextBytes);
        return true;
    }

    static bool DecodeEndToEnd(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1EndToEndFrameHeader& header,
        MeshV1ProtectedFrameView& frame
    ) noexcept {
        header = {};
        frame = {};
        std::size_t ciphertextBytes = 0U;
        if (!DecodeCommon(MeshV1SecurityMessageType::EndToEndProtectedFrame, input, inputBytes,
                          EndToEndFixedBodyBytes, ciphertextBytes) ||
            ciphertextBytes > std::numeric_limits<std::uint16_t>::max()) return false;
        const auto* cursor = input + CommonHeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage source{};
        MembershipIncarnation::Storage sourceIncarnation{};
        System::DeviceIdentifier::Storage destination{};
        MembershipIncarnation::Storage destinationIncarnation{};
        Read(cursor, mesh.data(), mesh.size()); header.Mesh = MeshIdentifier{mesh};
        Read(cursor, header.Session.Value.data(), header.Session.Value.size());
        header.Sequence = ReadU64(cursor); cursor += 8U;
        Read(cursor, source.data(), source.size()); header.Source = System::DeviceIdentifier{source};
        Read(cursor, sourceIncarnation.data(), sourceIncarnation.size()); header.SourceIncarnation = MembershipIncarnation{sourceIncarnation};
        Read(cursor, destination.data(), destination.size()); header.Destination = System::DeviceIdentifier{destination};
        Read(cursor, destinationIncarnation.data(), destinationIncarnation.size()); header.DestinationIncarnation = MembershipIncarnation{destinationIncarnation};
        header.MessageId = ReadU64(cursor); cursor += 8U;
        header.PrimitiveFamily = ReadU16(cursor); cursor += 2U;
        header.PrimitiveVersion = ReadU16(cursor); cursor += 2U;
        header.PlaintextBytes = ReadU16(cursor); cursor += 2U;
        if (!header.IsValid() || header.PlaintextBytes != ciphertextBytes) return false;
        frame.AuthenticatedHeader = input;
        frame.AuthenticatedHeaderBytes = EndToEndAuthenticatedHeaderBytes;
        frame.Ciphertext = cursor;
        frame.CiphertextBytes = ciphertextBytes;
        std::memcpy(frame.Tag.Value.data(), cursor + ciphertextBytes, frame.Tag.Value.size());
        return true;
    }

    static bool EncodeHopAuthenticatedHeader(
        const MeshV1HopFrameHeader& header,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        const auto packetBytes = HopPacketBytes(header.InnerFrameBytes);
        if (!header.IsValid() || packetBytes == 0U || outputBytes < HopAuthenticatedHeaderBytes ||
            !EncodeCommon(MeshV1SecurityMessageType::HopProtectedFrame,
                          packetBytes - CommonHeaderBytes, output, outputBytes)) return false;
        auto* cursor = output + CommonHeaderBytes;
        Copy(cursor, header.Mesh.Bytes().data(), header.Mesh.Bytes().size());
        Copy(cursor, header.Session.Value.data(), header.Session.Value.size());
        WriteU64(cursor, header.Sequence); cursor += 8U;
        Copy(cursor, header.Sender.Bytes().data(), header.Sender.Bytes().size());
        Copy(cursor, header.SenderIncarnation.Bytes().data(), header.SenderIncarnation.Bytes().size());
        Copy(cursor, header.NextHop.Bytes().data(), header.NextHop.Bytes().size());
        Copy(cursor, header.NextHopIncarnation.Bytes().data(), header.NextHopIncarnation.Bytes().size());
        Copy(cursor, header.Destination.Bytes().data(), header.Destination.Bytes().size());
        Copy(cursor, header.DestinationIncarnation.Bytes().data(), header.DestinationIncarnation.Bytes().size());
        WriteU64(cursor, header.MessageId); cursor += 8U;
        *cursor++ = header.HopLimit;
        WriteU16(cursor, header.InnerFrameBytes);
        return true;
    }

    static bool DecodeHop(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1HopFrameHeader& header,
        MeshV1ProtectedFrameView& frame
    ) noexcept {
        header = {};
        frame = {};
        std::size_t ciphertextBytes = 0U;
        if (!DecodeCommon(MeshV1SecurityMessageType::HopProtectedFrame, input, inputBytes,
                          HopFixedBodyBytes, ciphertextBytes) ||
            ciphertextBytes == 0U || ciphertextBytes > std::numeric_limits<std::uint16_t>::max()) return false;
        const auto* cursor = input + CommonHeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage sender{};
        MembershipIncarnation::Storage senderIncarnation{};
        System::DeviceIdentifier::Storage nextHop{};
        MembershipIncarnation::Storage nextHopIncarnation{};
        System::DeviceIdentifier::Storage destination{};
        MembershipIncarnation::Storage destinationIncarnation{};
        Read(cursor, mesh.data(), mesh.size()); header.Mesh = MeshIdentifier{mesh};
        Read(cursor, header.Session.Value.data(), header.Session.Value.size());
        header.Sequence = ReadU64(cursor); cursor += 8U;
        Read(cursor, sender.data(), sender.size()); header.Sender = System::DeviceIdentifier{sender};
        Read(cursor, senderIncarnation.data(), senderIncarnation.size()); header.SenderIncarnation = MembershipIncarnation{senderIncarnation};
        Read(cursor, nextHop.data(), nextHop.size()); header.NextHop = System::DeviceIdentifier{nextHop};
        Read(cursor, nextHopIncarnation.data(), nextHopIncarnation.size()); header.NextHopIncarnation = MembershipIncarnation{nextHopIncarnation};
        Read(cursor, destination.data(), destination.size()); header.Destination = System::DeviceIdentifier{destination};
        Read(cursor, destinationIncarnation.data(), destinationIncarnation.size()); header.DestinationIncarnation = MembershipIncarnation{destinationIncarnation};
        header.MessageId = ReadU64(cursor); cursor += 8U;
        header.HopLimit = *cursor++;
        header.InnerFrameBytes = ReadU16(cursor); cursor += 2U;
        if (!header.IsValid() || header.InnerFrameBytes != ciphertextBytes) return false;
        frame.AuthenticatedHeader = input;
        frame.AuthenticatedHeaderBytes = HopAuthenticatedHeaderBytes;
        frame.Ciphertext = cursor;
        frame.CiphertextBytes = ciphertextBytes;
        std::memcpy(frame.Tag.Value.data(), cursor + ciphertextBytes, frame.Tag.Value.size());
        return true;
    }
};

static_assert(MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes == 128U);
static_assert(MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes == 157U);

} // namespace ESPressio::Mesh
