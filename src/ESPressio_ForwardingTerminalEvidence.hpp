#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeferredLogicalTransferObserverBridge.hpp>

#include "ESPressio_ForwardingSubmissionCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Technology-independent terminal direct-link fact for one submitted Mesh next-hop transfer.</summary>
enum class ForwardingTerminalEvidenceKind : std::uint8_t {
    TransmissionCompleted,
    PeerAcknowledged,
    TransmissionFailed
};

/// <summary>Generation-safe local correlation handle for one forwarding attempt awaiting deferred Radio evidence.</summary>
struct ForwardingTerminalHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const ForwardingTerminalHandle& other) const noexcept {
        return Slot == other.Slot && Generation == other.Generation;
    }
};

/// <summary>Mesh-qualified terminal direct-link evidence for one exact authenticated next-hop incarnation.</summary>
struct ForwardingTerminalEvidence final {
    ForwardingTerminalHandle Handle{};
    System::DeviceIdentifier NextHop{};
    MembershipIncarnation NextHopIncarnation{};
    Radio::DeferredLogicalTransferHandle RadioTransfer{};
    ForwardingTerminalEvidenceKind Kind{ForwardingTerminalEvidenceKind::TransmissionFailed};
    Radio::RadioDirectLinkEvidence RadioEvidence{};
};

/// <summary>Consumes terminal direct-link evidence after Radio logical-transfer aggregation.</summary>
class IForwardingTerminalEvidenceObserver {
public:
    virtual ~IForwardingTerminalEvidenceObserver() = default;
    virtual void OnForwardingTerminalEvidence(const ForwardingTerminalEvidence& evidence) = 0;
};

/// <summary>Immediate classification of Radio evidence carried by a forwarding submission result.</summary>
enum class ForwardingSubmissionEvidenceState : std::uint8_t {
    NotAccepted,
    AwaitingDeferredEvidence,
    TransmissionCompleted,
    PeerAcknowledged,
    Unobservable
};

/// <summary>
/// Classifies only the direct-link evidence attached to an immediate forwarding submission.
/// </summary>
/// <remarks>
/// TransmissionCompleted and PeerAcknowledged are link facts only. Neither means Mesh end-to-end delivery and neither
/// commits RemainingHopLimit. AwaitingDeferredEvidence means RadioTransport retained bounded correlation for a later
/// terminal logical-transfer observation. Unobservable means submission was accepted but the provider cannot establish
/// stronger terminal evidence for that logical transfer.
/// </remarks>
inline ForwardingSubmissionEvidenceState ClassifyForwardingSubmissionEvidence(
    const ForwardingSubmissionResult& submission
) noexcept {
    if (submission.Disposition != ForwardingSubmissionDisposition::Accepted) {
        return ForwardingSubmissionEvidenceState::NotAccepted;
    }
    if (submission.RadioResult.LinkResult.Evidence.PeerAcknowledged()) {
        return ForwardingSubmissionEvidenceState::PeerAcknowledged;
    }
    if (submission.RadioResult.LinkResult.Evidence.TransmissionCompleted()) {
        return ForwardingSubmissionEvidenceState::TransmissionCompleted;
    }
    if (submission.RadioResult.DeferredTransfer) {
        return ForwardingSubmissionEvidenceState::AwaitingDeferredEvidence;
    }
    return ForwardingSubmissionEvidenceState::Unobservable;
}

/// <summary>
/// Bounded bridge from RadioTransport deferred logical-transfer terminal evidence to exact authenticated Mesh next-hop
/// context.
/// </summary>
/// <remarks>
/// Capacity is an explicit composition choice rather than another hidden global transmission bound. Track() is called
/// after ForwardingSubmissionCoordinator::Submit() returns Accepted with a valid DeferredTransfer. The bridge tolerates a
/// terminal Radio callback racing immediately after Send() return by retaining unmatched terminal evidence in the same
/// bounded table until Track() supplies the authenticated next-hop context.
///
/// This component deliberately does not map link evidence to RouteAttemptOutcome::Delivered and never mutates HopLimit.
/// A higher forwarding/delivery policy decides whether physical completion, peer acknowledgement, a Mesh delivery ACK,
/// or some stronger condition constitutes a successful Mesh forwarding transition.
/// </remarks>
template<std::size_t Capacity>
class ForwardingTerminalEvidenceCoordinator final : public Radio::ILogicalTransferTerminalObserver {
    static_assert(Capacity > 0U, "Forwarding terminal evidence capacity must be explicit and non-zero.");
    static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max(), "Capacity must fit ForwardingTerminalHandle.");

    struct Slot final {
        bool Used{false};
        bool HasMeshContext{false};
        bool HasTerminalEvidence{false};
        std::uint16_t Generation{0};
        Radio::DeferredLogicalTransferHandle RadioTransfer{};
        System::DeviceIdentifier NextHop{};
        MembershipIncarnation NextHopIncarnation{};
        Radio::RadioDirectLinkEvidence TerminalEvidence{};
    };

    std::array<Slot, Capacity> _slots{};
    IForwardingTerminalEvidenceObserver* _observer{nullptr};

    static void AdvanceGeneration(Slot& slot) noexcept {
        ++slot.Generation;
        if (slot.Generation == 0U) ++slot.Generation;
    }

    static ForwardingTerminalEvidenceKind ClassifyTerminal(
        const Radio::RadioDirectLinkEvidence& evidence
    ) noexcept {
        if (evidence.TransmissionFailed()) return ForwardingTerminalEvidenceKind::TransmissionFailed;
        if (evidence.PeerAcknowledged()) return ForwardingTerminalEvidenceKind::PeerAcknowledged;
        return ForwardingTerminalEvidenceKind::TransmissionCompleted;
    }

    Slot* FindRadioTransfer(Radio::DeferredLogicalTransferHandle transfer) noexcept {
        if (!transfer) return nullptr;
        for (auto& slot : _slots) {
            if (slot.Used && slot.RadioTransfer == transfer) return &slot;
        }
        return nullptr;
    }

    void PublishAndRelease(std::size_t index) noexcept {
        auto& slot = _slots[index];
        if (!slot.Used || !slot.HasMeshContext || !slot.HasTerminalEvidence || !slot.TerminalEvidence.IsTerminal()) return;
        const ForwardingTerminalEvidence evidence{
            ForwardingTerminalHandle{static_cast<std::uint16_t>(index), slot.Generation},
            slot.NextHop,
            slot.NextHopIncarnation,
            slot.RadioTransfer,
            ClassifyTerminal(slot.TerminalEvidence),
            slot.TerminalEvidence
        };
        slot.Used = false;
        slot.HasMeshContext = false;
        slot.HasTerminalEvidence = false;
        slot.RadioTransfer = {};
        slot.NextHop = {};
        slot.NextHopIncarnation = {};
        slot.TerminalEvidence = {};
        if (_observer != nullptr) _observer->OnForwardingTerminalEvidence(evidence);
    }

public:
    explicit ForwardingTerminalEvidenceCoordinator(IForwardingTerminalEvidenceObserver* observer = nullptr) noexcept
        : _observer(observer) {}

    void SetObserver(IForwardingTerminalEvidenceObserver* observer) noexcept { _observer = observer; }

    /// <summary>
    /// Correlates one accepted deferred Radio logical transfer with its exact authenticated Mesh next-hop incarnation.
    /// </summary>
    ForwardingTerminalHandle Track(const ForwardingSubmissionResult& submission) noexcept {
        if (submission.Disposition != ForwardingSubmissionDisposition::Accepted ||
            !submission.RadioResult.DeferredTransfer || !submission.NextHop || !submission.NextHopIncarnation) {
            return {};
        }

        Slot* slot = FindRadioTransfer(submission.RadioResult.DeferredTransfer);
        std::size_t index = 0U;
        if (slot == nullptr) {
            for (; index < Capacity; ++index) {
                if (!_slots[index].Used) {
                    slot = &_slots[index];
                    AdvanceGeneration(*slot);
                    slot->Used = true;
                    slot->RadioTransfer = submission.RadioResult.DeferredTransfer;
                    break;
                }
            }
            if (slot == nullptr) return {};
        } else {
            index = static_cast<std::size_t>(slot - _slots.data());
        }

        slot->HasMeshContext = true;
        slot->NextHop = submission.NextHop;
        slot->NextHopIncarnation = submission.NextHopIncarnation;
        const ForwardingTerminalHandle handle{static_cast<std::uint16_t>(index), slot->Generation};
        if (slot->HasTerminalEvidence) PublishAndRelease(index);
        return handle;
    }

    /// <summary>Releases one still-pending Mesh correlation when its owning attempt is abandoned.</summary>
    bool Release(ForwardingTerminalHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return false;
        auto& slot = _slots[handle.Slot];
        if (!slot.Used || slot.Generation != handle.Generation) return false;
        slot.Used = false;
        slot.HasMeshContext = false;
        slot.HasTerminalEvidence = false;
        slot.RadioTransfer = {};
        slot.NextHop = {};
        slot.NextHopIncarnation = {};
        slot.TerminalEvidence = {};
        return true;
    }

    std::size_t Size() const noexcept {
        std::size_t count = 0U;
        for (const auto& slot : _slots) if (slot.Used) ++count;
        return count;
    }

    /// <summary>
    /// Consumes one terminal aggregate emitted by RadioTransport. Unknown transfers are retained only if bounded capacity
    /// is available so an immediately-racing Track() call may attach authenticated Mesh context.
    /// </summary>
    void OnLogicalTransferTerminal(const Radio::LogicalTransferTerminalEvidence& terminal) override {
        if (!terminal.Transfer || !terminal.Evidence.IsTerminal()) return;

        Slot* slot = FindRadioTransfer(terminal.Transfer);
        std::size_t index = 0U;
        if (slot == nullptr) {
            for (; index < Capacity; ++index) {
                if (!_slots[index].Used) {
                    slot = &_slots[index];
                    AdvanceGeneration(*slot);
                    slot->Used = true;
                    slot->RadioTransfer = terminal.Transfer;
                    break;
                }
            }
            if (slot == nullptr) return;
        } else {
            index = static_cast<std::size_t>(slot - _slots.data());
        }

        slot->HasTerminalEvidence = true;
        slot->TerminalEvidence = terminal.Evidence;
        if (slot->HasMeshContext) PublishAndRelease(index);
    }
};

} // namespace ESPressio::Mesh
