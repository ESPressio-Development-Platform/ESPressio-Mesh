#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>
#include <ESPressio_PrimitiveFamilyRegistry.hpp>
#include <ESPressio_PrimitiveTypes.hpp>

#include "ESPressio_ApplicationPayload.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Wire-neutral identity of the conceptual primitive family carried by an application transmission.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Family (Primitive::PrimitiveFamilyId): 2 bytes [0 bytes dynamic allocation]
 * - Version (Primitive::PrimitiveProtocolVersion): 2 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ApplicationPrimitiveDescriptor final {
    Primitive::PrimitiveFamilyId Family{Primitive::FamilyIds::Invalid};
    Primitive::PrimitiveProtocolVersion Version{0};

    constexpr bool IsValid() const noexcept {
        return Primitive::FamilyIds::IsUsable(Family) && Family != Primitive::FamilyIds::MeshControl;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Generation-safe sender-local handle to one accepted application transmission aggregate.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Slot (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - Generation (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ApplicationTransmissionHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr bool IsValid() const noexcept { return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const ApplicationTransmissionHandle& other) const noexcept { return Slot == other.Slot && Generation == other.Generation; }
};

/// <summary>One recipient frozen into a sender-local selective application transmission.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Device (System::DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - Incarnation (MembershipIncarnation): 16 bytes [0 bytes dynamic allocation]
 * - MessageId (MeshMessageId): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 40 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ApplicationTransmissionRecipient final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MeshMessageId MessageId{0};
    constexpr bool IsValid() const noexcept { return static_cast<bool>(Device) && static_cast<bool>(Incarnation) && MessageId != 0U; }
};

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class ApplicationRecipientOutcome : std::uint8_t { Pending, Delivered, PermanentFailure, DeadlineExpired };
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class ApplicationTransmissionBeginResult : std::uint8_t { Begun, ResourceUnavailable, DeadlineExpired, DuplicateRecipient, DuplicateMessageId, Invalid };
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class ApplicationTransmissionUpdateResult : std::uint8_t { Updated, AlreadyTerminal, UnknownRecipient, UnknownTransmission, Invalid };

/// <summary>Fixed-capacity sender-local ownership of accepted application transmission aggregates and frozen recipients.</summary>
/// <remarks>
/// Every recipient owns an independent MeshMessageId/outcome while all recipients share one immutable deadline and one
/// immutable logical payload reference. Payload bytes are never duplicated per recipient. Borrowed/source backing remains
/// caller-owned for the aggregate lifetime; the table owns no variable-capacity payload storage, routes, Radio state,
/// acknowledgement tracker or selector identity. Broadcast is not represented here.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - _records (std::array<Record, TransmissionCapacity>): TransmissionCapacity * (40 bytes known/aligned storage + RecipientCapacity * (44 bytes)) [0 bytes dynamic allocation]
 * Total Memory: TransmissionCapacity * (40 bytes known/aligned storage + RecipientCapacity * (44 bytes)) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
         std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission>
class ApplicationTransmissionTable final {
    static_assert(TransmissionCapacity > 0U, "Application transmission capacity must be non-zero.");
    static_assert(RecipientCapacity > 0U, "Application recipient capacity must be non-zero.");
    static_assert(TransmissionCapacity <= std::numeric_limits<std::uint16_t>::max(), "Transmission capacity must fit handle slot.");

/**
 * ESPressio Memory Audit
 * Members:
 * - Recipient (ApplicationTransmissionRecipient): 40 bytes [0 bytes dynamic allocation]
 * - Outcome (ApplicationRecipientOutcome): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 44 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct RecipientRecord final { ApplicationTransmissionRecipient Recipient{}; ApplicationRecipientOutcome Outcome{ApplicationRecipientOutcome::Pending}; };
/**
 * ESPressio Memory Audit
 * Members:
 * - Used (bool): 1 bytes [0 bytes dynamic allocation]
 * - Generation (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - RecipientCount (std::uint8_t): 1 bytes [0 bytes dynamic allocation]
 * - TerminalCount (std::uint8_t): 1 bytes [0 bytes dynamic allocation]
 * - AbsoluteDeadlineMilliseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - Primitive (ApplicationPrimitiveDescriptor): 4 bytes [0 bytes dynamic allocation]
 * - Payload (ApplicationPayload): 20 bytes [0 bytes dynamic allocation]
 * - Recipients (std::array<RecipientRecord, RecipientCapacity>): RecipientCapacity * (44 bytes) [0 bytes dynamic allocation]
 * Total Memory: 40 bytes known/aligned storage + RecipientCapacity * (44 bytes) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
struct Record final {
        bool Used{false};
        std::uint16_t Generation{0};
        std::uint8_t RecipientCount{0};
        std::uint8_t TerminalCount{0};
        std::uint64_t AbsoluteDeadlineMilliseconds{0};
        ApplicationPrimitiveDescriptor Primitive{};
        ApplicationPayload Payload{};
        std::array<RecipientRecord, RecipientCapacity> Recipients{};
        void ClearPayload() noexcept {
            Used = false; RecipientCount = 0U; TerminalCount = 0U; AbsoluteDeadlineMilliseconds = 0U;
            Primitive = {}; Payload = {};
            for (auto& recipient : Recipients) recipient = {};
        }
    };
    std::array<Record, TransmissionCapacity> _records{};

    static void AdvanceGeneration(Record& record) noexcept { ++record.Generation; if (record.Generation == 0U) ++record.Generation; }
    Record* Resolve(ApplicationTransmissionHandle handle) noexcept {
        if (!handle || handle.Slot >= TransmissionCapacity) return nullptr;
        auto& record = _records[handle.Slot]; return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }
    const Record* Resolve(ApplicationTransmissionHandle handle) const noexcept {
        if (!handle || handle.Slot >= TransmissionCapacity) return nullptr;
        const auto& record = _records[handle.Slot]; return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }

    template<typename TExpiredRecipientCallback>
    static bool ExpireRecord(
        Record& record,
        std::uint64_t nowMilliseconds,
        TExpiredRecipientCallback&& onExpiredRecipient
    ) noexcept {
        if (!record.Used || nowMilliseconds < record.AbsoluteDeadlineMilliseconds || record.TerminalCount == record.RecipientCount) return false;

        std::array<MeshMessageId, RecipientCapacity> newlyExpired{};
        std::size_t newlyExpiredCount = 0U;
        for (std::size_t index = 0; index < record.RecipientCount; ++index) {
            auto& recipient = record.Recipients[index];
            if (recipient.Outcome != ApplicationRecipientOutcome::Pending) continue;
            recipient.Outcome = ApplicationRecipientOutcome::DeadlineExpired;
            ++record.TerminalCount;
            newlyExpired[newlyExpiredCount++] = recipient.Recipient.MessageId;
        }

        for (std::size_t index = 0; index < newlyExpiredCount; ++index) {
            onExpiredRecipient(newlyExpired[index]);
        }
        return newlyExpiredCount != 0U;
    }

    static bool ExpireRecord(Record& record, std::uint64_t nowMilliseconds) noexcept {
        return ExpireRecord(record, nowMilliseconds, [](MeshMessageId) noexcept {});
    }

public:
    ApplicationTransmissionBeginResult Begin(const ApplicationTransmissionRecipient* recipients, std::size_t recipientCount,
        ApplicationPrimitiveDescriptor primitive, const ApplicationPayload& payload,
        std::uint64_t nowMilliseconds, std::uint64_t absoluteDeadlineMilliseconds,
        ApplicationTransmissionHandle& handle) noexcept {
        handle = {};
        if (recipients == nullptr || recipientCount == 0U || recipientCount > RecipientCapacity || !primitive || !payload ||
            absoluteDeadlineMilliseconds == 0U) return ApplicationTransmissionBeginResult::Invalid;
        if (nowMilliseconds >= absoluteDeadlineMilliseconds) return ApplicationTransmissionBeginResult::DeadlineExpired;
        for (std::size_t i = 0; i < recipientCount; ++i) {
            if (!recipients[i].IsValid()) return ApplicationTransmissionBeginResult::Invalid;
            for (std::size_t j = 0; j < i; ++j) {
                if (recipients[i].Device == recipients[j].Device) return ApplicationTransmissionBeginResult::DuplicateRecipient;
                if (recipients[i].MessageId == recipients[j].MessageId) return ApplicationTransmissionBeginResult::DuplicateMessageId;
            }
        }
        for (std::size_t slot = 0; slot < TransmissionCapacity; ++slot) {
            auto& record = _records[slot]; if (record.Used) continue;
            AdvanceGeneration(record); record.ClearPayload(); record.Used = true;
            record.RecipientCount = static_cast<std::uint8_t>(recipientCount);
            record.AbsoluteDeadlineMilliseconds = absoluteDeadlineMilliseconds;
            record.Primitive = primitive; record.Payload = payload;
            for (std::size_t index = 0; index < recipientCount; ++index) record.Recipients[index].Recipient = recipients[index];
            handle = {static_cast<std::uint16_t>(slot), record.Generation}; return ApplicationTransmissionBeginResult::Begun;
        }
        return ApplicationTransmissionBeginResult::ResourceUnavailable;
    }

    ApplicationTransmissionUpdateResult SetOutcome(ApplicationTransmissionHandle handle, MeshMessageId messageId, ApplicationRecipientOutcome outcome) noexcept {
        if (messageId == 0U || outcome == ApplicationRecipientOutcome::Pending) return ApplicationTransmissionUpdateResult::Invalid;
        auto* record = Resolve(handle); if (record == nullptr) return ApplicationTransmissionUpdateResult::UnknownTransmission;
        for (std::size_t index = 0; index < record->RecipientCount; ++index) {
            auto& recipient = record->Recipients[index]; if (recipient.Recipient.MessageId != messageId) continue;
            if (recipient.Outcome != ApplicationRecipientOutcome::Pending) return ApplicationTransmissionUpdateResult::AlreadyTerminal;
            recipient.Outcome = outcome; ++record->TerminalCount; return ApplicationTransmissionUpdateResult::Updated;
        }
        return ApplicationTransmissionUpdateResult::UnknownRecipient;
    }

    bool Expire(ApplicationTransmissionHandle handle, std::uint64_t nowMilliseconds) noexcept {
        auto* record = Resolve(handle);
        return record != nullptr && ExpireRecord(*record, nowMilliseconds);
    }

    template<typename TExpiredRecipientCallback>
    bool ExpireWithRecipients(
        ApplicationTransmissionHandle handle,
        std::uint64_t nowMilliseconds,
        TExpiredRecipientCallback&& onExpiredRecipient
    ) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr) return false;
        return ExpireRecord(*record, nowMilliseconds, [&](MeshMessageId messageId) noexcept {
            onExpiredRecipient(messageId);
        });
    }

    template<typename TExpiredCallback>
    std::size_t ExpireDue(std::uint64_t nowMilliseconds, TExpiredCallback&& onExpired) noexcept {
        std::size_t expired = 0U;
        for (std::size_t slot = 0; slot < TransmissionCapacity; ++slot) {
            auto& record = _records[slot];
            if (!ExpireRecord(record, nowMilliseconds)) continue;
            ++expired;
            onExpired(ApplicationTransmissionHandle{static_cast<std::uint16_t>(slot), record.Generation});
        }
        return expired;
    }

    template<typename TExpiredRecipientCallback, typename TExpiredAggregateCallback>
    std::size_t ExpireDueWithRecipients(
        std::uint64_t nowMilliseconds,
        TExpiredRecipientCallback&& onExpiredRecipient,
        TExpiredAggregateCallback&& onExpiredAggregate
    ) noexcept {
        std::size_t expired = 0U;
        for (std::size_t slot = 0; slot < TransmissionCapacity; ++slot) {
            auto& record = _records[slot];
            const ApplicationTransmissionHandle handle{static_cast<std::uint16_t>(slot), record.Generation};
            if (!ExpireRecord(record, nowMilliseconds, [&](MeshMessageId messageId) noexcept {
                    onExpiredRecipient(handle, messageId);
                })) continue;
            ++expired;
            onExpiredAggregate(handle);
        }
        return expired;
    }

    /// <summary>
    /// Enumerates every retained recipient synchronously using only the table's fixed aggregate/recipient capacities.
    /// </summary>
    /// <remarks>
    /// This read-only local introspection exists for composition-owned cleanup/inspection and performs at most
    /// TransmissionCapacity × RecipientCapacity visits. The visitor must not structurally mutate this table while the
    /// enumeration is in progress. No additional identity/capacity registry is created.
    /// </remarks>
    template<typename TVisitor>
    void ForEachRecipient(TVisitor&& visitor) const noexcept {
        for (std::size_t slot = 0; slot < TransmissionCapacity; ++slot) {
            const auto& record = _records[slot];
            if (!record.Used) continue;
            const ApplicationTransmissionHandle handle{static_cast<std::uint16_t>(slot), record.Generation};
            for (std::size_t index = 0; index < record.RecipientCount; ++index) {
                const auto& recipient = record.Recipients[index];
                visitor(handle, recipient.Recipient, recipient.Outcome);
            }
        }
    }

    constexpr std::size_t Capacity() const noexcept { return TransmissionCapacity; }
    std::size_t Size() const noexcept { std::size_t count = 0U; for (const auto& record : _records) if (record.Used) ++count; return count; }
    bool Contains(ApplicationTransmissionHandle handle) const noexcept { return Resolve(handle) != nullptr; }
    bool IsTerminal(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record != nullptr && record->TerminalCount == record->RecipientCount; }
    std::uint64_t AbsoluteDeadlineMilliseconds(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? 0U : record->AbsoluteDeadlineMilliseconds; }
    std::size_t RecipientCount(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? 0U : record->RecipientCount; }
    const ApplicationPrimitiveDescriptor* PrimitiveDescriptor(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? nullptr : &record->Primitive; }
    const ApplicationPayload* Payload(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? nullptr : &record->Payload; }

    bool TryGetRecipient(ApplicationTransmissionHandle handle, std::size_t index, ApplicationTransmissionRecipient& recipient, ApplicationRecipientOutcome& outcome) const noexcept {
        const auto* record = Resolve(handle); if (record == nullptr || index >= record->RecipientCount) return false;
        recipient = record->Recipients[index].Recipient; outcome = record->Recipients[index].Outcome; return true;
    }
    bool Release(ApplicationTransmissionHandle handle) noexcept { auto* record = Resolve(handle); if (record == nullptr) return false; record->ClearPayload(); return true; }
    void Clear() noexcept { for (auto& record : _records) record.ClearPayload(); }
};

} // namespace ESPressio::Mesh
