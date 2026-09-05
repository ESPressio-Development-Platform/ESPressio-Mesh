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
};

/// <summary>Result of consuming terminal Radio evidence correlated to one Mesh forwarding attempt.</summary>
struct ForwardingRadioTerminalObservation final {
    Radio::LogicalTransferTerminalEvidence Terminal{};
};

/// <summary>
/// Explicit-capacity bridge correlating RadioTransport deferred logical-transfer terminal observations back to the
/// Mesh forwarding attempt which submitted them.
/// </summary>
/// <remarks>
/// The table owns correlation only. It does not own payloads, routes, timers, retries, HopLimit or Mesh acceptance state.
/// Capacity is deliberately a composition choice rather than a new universal Mesh limit. The caller stores the returned
/// generation-safe handle in its already-bounded active forwarding state and later consumes terminal evidence through
/// TryTake(). Radio completion/peer acknowledgement remains link evidence only; the caller must pass it through
/// ForwardingAttemptLifecycle and still await authenticated next-hop Mesh acceptance after successful Radio completion.
///
/// Register() is used only for a valid RadioTransport DeferredTransfer returned after Send. Radio's serialized execution
/// contract guarantees that a promised deferred provider callback cannot be published before the corresponding Send has
/// returned, allowing Mesh to install this correlation before yielding that execution domain. Mutation and callbacks must
/// therefore remain serialized by the owning composition; this type deliberately introduces no mutex or task.
/// </remarks>
template<std::size_t Capacity>
class ForwardingRadioTerminalCorrelation final : public Radio::ILogicalTransferTerminalObserver {
    static_assert(Capacity > 0U, "Forwarding Radio terminal correlation capacity must be explicit and non-zero.");
    static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max(), "Capacity must fit the local correlation handle.");

    struct Record final {
        bool Used{false};
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

public:
    /// <summary>Reserves correlation for one accepted logical transfer whose terminal Radio evidence is deferred.</summary>
    ForwardingRadioCorrelationHandle Register(Radio::DeferredLogicalTransferHandle deferred) noexcept {
        if (!deferred) return {};
        for (const auto& record : _records) {
            if (record.Used && record.Deferred == deferred) return {};
        }
        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& record = _records[slot];
            if (record.Used) continue;
            AdvanceGeneration(record);
            record.Used = true;
            record.TerminalAvailable = false;
            record.Deferred = deferred;
            record.Terminal = {};
            return {static_cast<std::uint16_t>(slot), record.Generation};
        }
        return {};
    }

    /// <summary>Consumes terminal evidence when available and releases the correlation slot.</summary>
    bool TryTake(
        ForwardingRadioCorrelationHandle handle,
        ForwardingRadioTerminalObservation& observation
    ) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr || !record->TerminalAvailable) return false;
        observation.Terminal = record->Terminal;
        record->Used = false;
        record->TerminalAvailable = false;
        record->Deferred = {};
        record->Terminal = {};
        return true;
    }

    /// <summary>Releases pending correlation after cancellation, deadline expiry or superseded route attempt.</summary>
    bool Release(ForwardingRadioCorrelationHandle handle) noexcept {
        auto* record = Resolve(handle);
        if (record == nullptr) return false;
        record->Used = false;
        record->TerminalAvailable = false;
        record->Deferred = {};
        record->Terminal = {};
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

    /// <summary>Captures one RadioTransport terminal observation only when a matching live forwarding correlation exists.</summary>
    void OnLogicalTransferTerminal(const Radio::LogicalTransferTerminalEvidence& terminal) override {
        if (!terminal.Transfer || !terminal.Evidence.IsTerminal()) return;
        for (auto& record : _records) {
            if (!record.Used || record.Deferred != terminal.Transfer) continue;
            if (!record.TerminalAvailable) {
                record.Terminal = terminal;
                record.TerminalAvailable = true;
            }
            return;
        }
    }
};

} // namespace ESPressio::Mesh
