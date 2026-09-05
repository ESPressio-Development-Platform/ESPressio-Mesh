#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_ApplicationPayload.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Generation-safe sender-local handle to one accepted application transmission aggregate.</summary>
struct ApplicationTransmissionHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr bool IsValid() const noexcept { return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const ApplicationTransmissionHandle& other) const noexcept { return Slot == other.Slot && Generation == other.Generation; }
};

/// <summary>One recipient frozen into a sender-local selective application transmission.</summary>
struct ApplicationTransmissionRecipient final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MeshMessageId MessageId{0};
    constexpr bool IsValid() const noexcept { return static_cast<bool>(Device) && static_cast<bool>(Incarnation) && MessageId != 0U; }
};

enum class ApplicationRecipientOutcome : std::uint8_t { Pending, Delivered, PermanentFailure, DeadlineExpired };
enum class ApplicationTransmissionBeginResult : std::uint8_t { Begun, ResourceUnavailable, DeadlineExpired, DuplicateRecipient, DuplicateMessageId, Invalid };
enum class ApplicationTransmissionUpdateResult : std::uint8_t { Updated, AlreadyTerminal, UnknownRecipient, UnknownTransmission, Invalid };

/// <summary>Fixed-capacity sender-local ownership of accepted application transmission aggregates and frozen recipients.</summary>
/// <remarks>
/// Every recipient owns an independent MeshMessageId/outcome while all recipients share one immutable deadline and one
/// immutable logical payload reference. Payload bytes are never duplicated per recipient. Borrowed/source backing remains
/// caller-owned for the aggregate lifetime; the table owns no variable-capacity payload storage, routes, Radio state,
/// acknowledgement tracker or selector identity. Broadcast is not represented here.
/// </remarks>
template<std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
         std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission>
class ApplicationTransmissionTable final {
    static_assert(TransmissionCapacity > 0U, "Application transmission capacity must be non-zero.");
    static_assert(RecipientCapacity > 0U, "Application recipient capacity must be non-zero.");
    static_assert(TransmissionCapacity <= std::numeric_limits<std::uint16_t>::max(), "Transmission capacity must fit handle slot.");

    struct RecipientRecord final { ApplicationTransmissionRecipient Recipient{}; ApplicationRecipientOutcome Outcome{ApplicationRecipientOutcome::Pending}; };
    struct Record final {
        bool Used{false};
        std::uint16_t Generation{0};
        std::uint8_t RecipientCount{0};
        std::uint8_t TerminalCount{0};
        std::uint64_t AbsoluteDeadlineMilliseconds{0};
        ApplicationPayload Payload{};
        std::array<RecipientRecord, RecipientCapacity> Recipients{};
        void ClearPayload() noexcept {
            Used = false; RecipientCount = 0U; TerminalCount = 0U; AbsoluteDeadlineMilliseconds = 0U; Payload = {};
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

    static bool ExpireRecord(Record& record, std::uint64_t nowMilliseconds) noexcept {
        if (!record.Used || nowMilliseconds < record.AbsoluteDeadlineMilliseconds || record.TerminalCount == record.RecipientCount) return false;
        for (std::size_t index = 0; index < record.RecipientCount; ++index) {
            auto& recipient = record.Recipients[index];
            if (recipient.Outcome != ApplicationRecipientOutcome::Pending) continue;
            recipient.Outcome = ApplicationRecipientOutcome::DeadlineExpired;
            ++record.TerminalCount;
        }
        return true;
    }

public:
    ApplicationTransmissionBeginResult Begin(const ApplicationTransmissionRecipient* recipients, std::size_t recipientCount,
        const ApplicationPayload& payload, std::uint64_t nowMilliseconds, std::uint64_t absoluteDeadlineMilliseconds,
        ApplicationTransmissionHandle& handle) noexcept {
        handle = {};
        if (recipients == nullptr || recipientCount == 0U || recipientCount > RecipientCapacity || !payload ||
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
            record.AbsoluteDeadlineMilliseconds = absoluteDeadlineMilliseconds; record.Payload = payload;
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

    /// <summary>Expires every non-terminal aggregate whose immutable absolute deadline has elapsed.</summary>
    /// <remarks>
    /// The callback is invoked once for each aggregate newly made terminal by this sweep. Records remain retained for
    /// outcome inspection until explicitly released; no payload backing or handle is silently discarded here.
    /// </remarks>
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

    constexpr std::size_t Capacity() const noexcept { return TransmissionCapacity; }
    std::size_t Size() const noexcept { std::size_t count = 0U; for (const auto& record : _records) if (record.Used) ++count; return count; }
    bool Contains(ApplicationTransmissionHandle handle) const noexcept { return Resolve(handle) != nullptr; }
    bool IsTerminal(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record != nullptr && record->TerminalCount == record->RecipientCount; }
    std::uint64_t AbsoluteDeadlineMilliseconds(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? 0U : record->AbsoluteDeadlineMilliseconds; }
    std::size_t RecipientCount(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? 0U : record->RecipientCount; }
    const ApplicationPayload* Payload(ApplicationTransmissionHandle handle) const noexcept { const auto* record = Resolve(handle); return record == nullptr ? nullptr : &record->Payload; }

    bool TryGetRecipient(ApplicationTransmissionHandle handle, std::size_t index, ApplicationTransmissionRecipient& recipient, ApplicationRecipientOutcome& outcome) const noexcept {
        const auto* record = Resolve(handle); if (record == nullptr || index >= record->RecipientCount) return false;
        recipient = record->Recipients[index].Recipient; outcome = record->Recipients[index].Outcome; return true;
    }
    bool Release(ApplicationTransmissionHandle handle) noexcept { auto* record = Resolve(handle); if (record == nullptr) return false; record->ClearPayload(); return true; }
    void Clear() noexcept { for (auto& record : _records) record.ClearPayload(); }
};

} // namespace ESPressio::Mesh
