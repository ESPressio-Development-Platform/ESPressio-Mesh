#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeferredLogicalTransferObserverBridge.hpp>

namespace ESPressio::Mesh {

/**
 * ESPressio Memory Audit
 * Members:
 * - Slot (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - Generation (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ForwardingRadioCorrelationHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr bool IsValid() const noexcept { return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const ForwardingRadioCorrelationHandle& other) const noexcept { return Slot == other.Slot && Generation == other.Generation; }
    constexpr bool operator!=(const ForwardingRadioCorrelationHandle& other) const noexcept { return !(*this == other); }
};

/**
 * ESPressio Memory Audit
 * Members:
 * - Terminal (Radio::LogicalTransferTerminalEvidence): 32 bytes [0 bytes dynamic allocation]
 * Total Memory: 32 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ForwardingRadioTerminalObservation final { Radio::LogicalTransferTerminalEvidence Terminal{}; };

/// <summary>Explicit-capacity correlation between one Mesh forwarding attempt and deferred Radio terminal evidence.</summary>
/// <remarks>
/// Capacity is reserved before Radio submission, so local correlation exhaustion is known before any physical fragment
/// can be accepted. Bind() is called immediately after an Accepted Send returns a DeferredTransfer and before yielding the
/// serialized Radio execution domain. This object owns no payload, route, timer, retry, HopLimit or Mesh acceptance state.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _records (std::array<Record, Capacity>): Capacity * (44 bytes) [0 bytes dynamic allocation]
 * Total Memory: 4 bytes known/aligned storage + Capacity * (44 bytes) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<std::size_t Capacity>
class ForwardingRadioTerminalCorrelation final : public Radio::ILogicalTransferTerminalObserver {
    static_assert(Capacity > 0U, "Forwarding Radio terminal correlation capacity must be explicit and non-zero.");
    static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max(), "Capacity must fit the local correlation handle.");

/**
 * ESPressio Memory Audit
 * Members:
 * - Used (bool): 1 bytes [0 bytes dynamic allocation]
 * - Bound (bool): 1 bytes [0 bytes dynamic allocation]
 * - TerminalAvailable (bool): 1 bytes [0 bytes dynamic allocation]
 * - Generation (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - Deferred (Radio::DeferredLogicalTransferHandle): 4 bytes [0 bytes dynamic allocation]
 * - Terminal (Radio::LogicalTransferTerminalEvidence): 32 bytes [0 bytes dynamic allocation]
 * Total Memory: 44 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct Record final {
        bool Used{false};
        bool Bound{false};
        bool TerminalAvailable{false};
        std::uint16_t Generation{0};
        Radio::DeferredLogicalTransferHandle Deferred{};
        Radio::LogicalTransferTerminalEvidence Terminal{};
    };
    std::array<Record, Capacity> _records{};

    static void AdvanceGeneration(Record& record) noexcept { ++record.Generation; if (record.Generation == 0U) ++record.Generation; }
    static void ClearPayload(Record& record) noexcept {
        record.Used = false; record.Bound = false; record.TerminalAvailable = false; record.Deferred = {}; record.Terminal = {};
    }
    Record* Resolve(ForwardingRadioCorrelationHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }

public:
    ForwardingRadioCorrelationHandle Reserve() noexcept {
        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& record = _records[slot];
            if (record.Used) continue;
            AdvanceGeneration(record); ClearPayload(record); record.Used = true;
            return {static_cast<std::uint16_t>(slot), record.Generation};
        }
        return {};
    }

    bool Bind(ForwardingRadioCorrelationHandle handle, Radio::DeferredLogicalTransferHandle deferred) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr || record->Bound || !deferred) return false;
        for (const auto& candidate : _records) {
            if (candidate.Used && candidate.Bound && candidate.Deferred == deferred) return false;
        }
        record->Deferred = deferred; record->Bound = true; return true;
    }

    bool TryTake(ForwardingRadioCorrelationHandle handle, ForwardingRadioTerminalObservation& observation) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr || !record->TerminalAvailable) return false;
        observation.Terminal = record->Terminal; ClearPayload(*record); return true;
    }

    bool Release(ForwardingRadioCorrelationHandle handle) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr) return false;
        ClearPayload(*record); return true;
    }

    bool Contains(ForwardingRadioCorrelationHandle handle) const noexcept {
        if (!handle || handle.Slot >= Capacity) return false;
        const auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation;
    }
    std::size_t Size() const noexcept { std::size_t count = 0U; for (const auto& record : _records) if (record.Used) ++count; return count; }

    /// <summary>Releases every retained local Radio-terminal correlation during controlled Mesh shutdown/reset.</summary>
    /// <remarks>
    /// Record generations are preserved so handles issued before reset cannot resolve after slot reuse. No Radio
    /// terminal evidence is created: the owning Radio transport must independently abandon its provider work.
    /// </remarks>
    void Clear() noexcept {
        for (auto& record : _records) ClearPayload(record);
    }

    void OnLogicalTransferTerminal(const Radio::LogicalTransferTerminalEvidence& terminal) override {
        if (!terminal.Transfer || !terminal.Evidence.IsTerminal()) return;
        for (auto& record : _records) {
            if (!record.Used || !record.Bound || !(record.Deferred == terminal.Transfer)) continue;
            if (!record.TerminalAvailable) { record.Terminal = terminal; record.TerminalAvailable = true; }
            return;
        }
    }
};

} // namespace ESPressio::Mesh
