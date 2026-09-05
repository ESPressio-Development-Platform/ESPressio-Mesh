#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeferredLogicalTransferObserverBridge.hpp>

namespace ESPressio::Mesh {

/// <summary>Generation-safe sender-local handle for one forwarding attempt awaiting deferred Radio terminal evidence.</summary>
struct ForwardingRadioCorrelationHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const ForwardingRadioCorrelationHandle& other) const noexcept {
        return Slot == other.Slot && Generation == other.Generation;
    }
    constexpr bool operator!=(const ForwardingRadioCorrelationHandle& other) const noexcept { return !(*this == other); }
};

struct ForwardingRadioTerminalObservation final {
    Radio::LogicalTransferTerminalEvidence Terminal{};
};

/// <summary>
/// Explicit-capacity bridge correlating RadioTransport deferred logical-transfer terminal observations back to the
/// Mesh forwarding attempt which submitted them.
/// </summary>
/// <remarks>
/// Capacity is reserved before Radio submission, so local Mesh correlation exhaustion is discovered before any physical
/// fragment can be accepted. After an Accepted Send returns a valid DeferredTransfer, Bind() installs that Radio-owned
/// handle before the serialized Radio execution domain is yielded. A submission with synchronous terminal evidence or no
/// promised deferred evidence releases the reservation immediately.
///
/// The table owns correlation only. It does not own payloads, routes, timers, retries, HopLimit or Mesh acceptance state.
/// Capacity is deliberately a composition choice rather than a new universal Mesh limit. Radio completion/peer ACK remains
/// link evidence only and must still pass through ForwardingAttemptLifecycle before authenticated next-hop acceptance.
/// Mutation and callbacks are serialized by the owning composition; this type deliberately introduces no mutex or task.
/// </remarks>
template<std::size_t Capacity>
class ForwardingRadioTerminalCorrelation final : public Radio::ILogicalTransferTerminalObserver {
    static_assert(Capacity > 0U, "Forwarding Radio terminal correlation capacity must be explicit and non-zero.");
    static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max(), "Capacity must fit the local correlation handle.");

    struct Record final {
        bool Used{false};
        bool Bound{false};
        bool TerminalAvailable{false};
        std::uint16_t Generation{0};
        Radio::DeferredLogicalTransferHandle Deferred{};
        Radio::LogicalTransferTerminalEvidence Terminal{};
    };

    std::array<Record, Capacity> _records{};

    static void AdvanceGeneration(Record& record) noexcept {
        ++record.Generation;
        if (record.Generation == 0U) ++record.Generation;
    }

    Record* Resolve(ForwardingRadioCorrelationHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }

    static void ClearPayload(Record& record) noexcept {
        record.Used = false;
        record.Bound = false;
        record.TerminalAvailable = false;
        record.Deferred = {};
        record.Terminal = {};
    }

public:
    /// <summary>Reserves one bounded Mesh-side correlation slot before forwarding is submitted to Radio.</summary>
    ForwardingRadioCorrelationHandle Reserve() noexcept {
        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& record = _records[slot];
            if (record.Used) continue;
            AdvanceGeneration(record);
            ClearPayload(record);
            record.Used = true;
            return {static_cast<std::uint16_t>(slot), record.Generation};
        }
        return {};
    }

    /// <summary>Binds a pre-reserved slot to the deferred logical-transfer handle returned by RadioTransport.</summary>
    bool Bind(
        ForwardingRadioCorrelationHandle handle,
        Radio::DeferredLogicalTransferHandle deferred
    ) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr || record->Bound || !deferred) return false;
        for (const auto& candidate : _records) {
            if (candidate.Used && candidate.Bound && candidate.Deferred == deferred) return false;
        }
        record->Deferred = deferred;
        record->Bound = true;
        return true;
    }

    bool TryTake(
        ForwardingRadioCorrelationHandle handle,
        ForwardingRadioTerminalObservation& observation
    ) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr || !record->TerminalAvailable) return false;
        observation.Terminal = record->Terminal;
        ClearPayload(*record);
        return true;
    }

    /// <summary>Releases reservation/correlation after synchronous evidence, cancellation, expiry or superseded attempt.</summary>
    bool Release(ForwardingRadioCorrelationHandle handle) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr) return false;
        ClearPayload(*record);
        return true;
    }

    bool Contains(ForwardingRadioCorrelationHandle handle) const noexcept {
        if (!handle || handle.Slot >= Capacity) return false;
        const auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation;
    }

    std::size_t Size() const noexcept {
        std::size_t count = 0U;
        for (const auto& record : _records) if (record.Used) ++count;
        return count;
    }

    void OnLogicalTransferTerminal(const Radio::LogicalTransferTerminalEvidence& terminal) override {
        if (!terminal.Transfer || !terminal.Evidence.IsTerminal()) return;
        for (auto& record : _records) {
            if (!record.Used || !record.Bound || record.Deferred != terminal.Transfer) continue;
            if (!record.TerminalAvailable) {
                record.Terminal = terminal;
                record.TerminalAvailable = true;
            }
            return;
        }
    }
};

} // namespace ESPressio::Mesh
