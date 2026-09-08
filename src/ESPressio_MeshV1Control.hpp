#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class MeshV1ControlMessageType : std::uint8_t {
    Invalid = 0U,
    NextHopAcceptance = 1U,
    DestinationDeliveryAcknowledgement = 2U
};

/**
 * ESPressio Memory Audit
 * Members:
 * - Source (System::DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - SourceIncarnation (MembershipIncarnation): 16 bytes [0 bytes dynamic allocation]
 * - MessageId (MeshMessageId): 8 bytes [0 bytes dynamic allocation]
 * - AbsoluteDeadlineMilliseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 48 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct MeshV1AcknowledgedDelivery final {
    System::DeviceIdentifier Source{};
    MembershipIncarnation SourceIncarnation{};
    MeshMessageId MessageId{0U};
    std::uint64_t AbsoluteDeadlineMilliseconds{0U};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Source) && static_cast<bool>(SourceIncarnation) && MessageId != 0U &&
               AbsoluteDeadlineMilliseconds != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/**
 * ESPressio Memory Audit
 * Members:
 * - Recipient (System::DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - RecipientIncarnation (MembershipIncarnation): 16 bytes [0 bytes dynamic allocation]
 * - Acknowledged (MeshV1AcknowledgedDelivery): 48 bytes [0 bytes dynamic allocation]
 * Total Memory: 80 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct MeshV1NextHopAcceptanceIntent final {
    System::DeviceIdentifier Recipient{};
    MembershipIncarnation RecipientIncarnation{};
    MeshV1AcknowledgedDelivery Acknowledged{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Recipient) && static_cast<bool>(RecipientIncarnation) &&
               static_cast<bool>(Acknowledged);
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Canonical fixed Mesh Control acknowledgement payload.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class MeshV1ControlCodec final {
    inline static constexpr std::array<std::uint8_t, 4> Magic{{0x45U, 0x53U, 0x4DU, 0x43U}};
public:
    static constexpr std::uint8_t Version = 1U;
    static constexpr std::size_t HeaderBytes = 8U;
    static constexpr std::size_t AcknowledgedDeliveryBytes = 48U;
    static constexpr std::size_t PacketBytes = HeaderBytes + AcknowledgedDeliveryBytes;

    static bool Encode(
        MeshV1ControlMessageType type,
        const MeshV1AcknowledgedDelivery& acknowledged,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if ((type != MeshV1ControlMessageType::NextHopAcceptance &&
             type != MeshV1ControlMessageType::DestinationDeliveryAcknowledgement) ||
            !acknowledged || output == nullptr || outputBytes != PacketBytes) return false;
        std::memcpy(output, Magic.data(), Magic.size());
        output[4] = Version;
        output[5] = static_cast<std::uint8_t>(type);
        output[6] = 0U;
        output[7] = static_cast<std::uint8_t>(AcknowledgedDeliveryBytes);
        auto* cursor = output + HeaderBytes;
        std::memcpy(cursor, acknowledged.Source.Bytes().data(), acknowledged.Source.Bytes().size());
        cursor += acknowledged.Source.Bytes().size();
        std::memcpy(cursor, acknowledged.SourceIncarnation.Bytes().data(),
                    acknowledged.SourceIncarnation.Bytes().size());
        cursor += acknowledged.SourceIncarnation.Bytes().size();
        for (std::size_t index = 0; index < 8U; ++index) {
            cursor[index] = static_cast<std::uint8_t>(acknowledged.MessageId >> ((7U - index) * 8U));
        }
        cursor += 8U;
        for (std::size_t index = 0; index < 8U; ++index) {
            cursor[index] = static_cast<std::uint8_t>(
                acknowledged.AbsoluteDeadlineMilliseconds >> ((7U - index) * 8U));
        }
        return true;
    }

    static bool Decode(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1ControlMessageType& type,
        MeshV1AcknowledgedDelivery& acknowledged
    ) noexcept {
        type = {};
        acknowledged = {};
        if (input == nullptr || inputBytes != PacketBytes ||
            std::memcmp(input, Magic.data(), Magic.size()) != 0 || input[4] != Version ||
            input[6] != 0U || input[7] != AcknowledgedDeliveryBytes ||
            (input[5] != static_cast<std::uint8_t>(MeshV1ControlMessageType::NextHopAcceptance) &&
             input[5] != static_cast<std::uint8_t>(MeshV1ControlMessageType::DestinationDeliveryAcknowledgement))) {
            return false;
        }
        type = static_cast<MeshV1ControlMessageType>(input[5]);
        const auto* cursor = input + HeaderBytes;
        System::DeviceIdentifier::Storage source{};
        MembershipIncarnation::Storage incarnation{};
        std::memcpy(source.data(), cursor, source.size());
        cursor += source.size();
        std::memcpy(incarnation.data(), cursor, incarnation.size());
        cursor += incarnation.size();
        MeshMessageId messageId = 0U;
        for (std::size_t index = 0; index < 8U; ++index) messageId = (messageId << 8U) | cursor[index];
        cursor += 8U;
        std::uint64_t deadline = 0U;
        for (std::size_t index = 0; index < 8U; ++index) deadline = (deadline << 8U) | cursor[index];
        acknowledged = {
            System::DeviceIdentifier{source}, MembershipIncarnation{incarnation}, messageId, deadline};
        if (!acknowledged) {
            type = {};
            acknowledged = {};
            return false;
        }
        return true;
    }
};

static_assert(MeshV1ControlCodec::PacketBytes == 56U);

} // namespace ESPressio::Mesh
