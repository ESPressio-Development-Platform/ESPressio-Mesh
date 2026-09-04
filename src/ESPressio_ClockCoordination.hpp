#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Local clock-hierarchy stratum used by Mesh clock coordination.</summary>
/// <remarks>This is local orchestration state only; this header defines no Mesh wire representation.</remarks>
using ClockStratum = std::uint16_t;

static constexpr ClockStratum ClockRootStratum = 0U;
static constexpr ClockStratum InvalidClockStratum = std::numeric_limits<ClockStratum>::max();

/// <summary>Relative quality comparison returned by an injected clock-quality policy.</summary>
enum class ClockQualityComparison : std::uint8_t {
    Better,
    Equivalent,
    Worse
};

/// <summary>
/// One authenticated informational clock advertisement retained by the local Mesh coordinator.
/// </summary>
/// <remarks>
/// `TQuality` is intentionally supplied by composition: Mesh does not impose a universal accuracy/uncertainty scalar.
/// Advertisement storage does not authenticate the sender and does not define wire encoding. Callers may insert an
/// observation only after authenticating the exact DeviceIdentifier + MembershipIncarnation that supplied it.
/// </remarks>
template<typename TQuality>
struct ClockCoordinationAdvertisement final {
    System::DeviceIdentifier Sender{};
    MembershipIncarnation SenderIncarnation{};
    System::DeviceIdentifier AdvertisedRoot{};
    ClockStratum SenderStratum{InvalidClockStratum};
    TQuality RootQuality{};
    std::uint64_t ObservedAtMilliseconds{0};

    constexpr bool IsStructurallyValid() const noexcept {
        return static_cast<bool>(Sender) &&
               static_cast<bool>(SenderIncarnation) &&
               static_cast<bool>(AdvertisedRoot) &&
               SenderStratum != InvalidClockStratum &&
               ObservedAtMilliseconds != 0U;
    }
};

/// <summary>Injected comparison policy for the composition-defined clock-quality observation.</summary>
template<typename TQuality>
class IClockQualityPolicy {
public:
    virtual ~IClockQualityPolicy() = default;
    virtual ClockQualityComparison Compare(const TQuality& candidate, const TQuality& incumbent) const noexcept = 0;
};

/// <summary>Injected eligibility policy deciding whether one authenticated clock advertisement may participate.</summary>
template<typename TQuality>
class IClockEligibilityPolicy {
public:
    virtual ~IClockEligibilityPolicy() = default;
    virtual bool IsEligible(const ClockCoordinationAdvertisement<TQuality>& advertisement) const noexcept = 0;
};

/// <summary>Injected root-election policy.</summary>
template<typename TQuality>
class IClockRootElectionPolicy {
public:
    virtual ~IClockRootElectionPolicy() = default;
    virtual bool PreferRoot(
        const ClockCoordinationAdvertisement<TQuality>& candidate,
        const ClockCoordinationAdvertisement<TQuality>& incumbent,
        const IClockQualityPolicy<TQuality>& quality
    ) const noexcept = 0;
};

/// <summary>Injected parent-selection policy among candidates already proven loop-safe for the elected root.</summary>
template<typename TQuality>
class IClockParentSelectionPolicy {
public:
    virtual ~IClockParentSelectionPolicy() = default;
    virtual bool PreferParent(
        const ClockCoordinationAdvertisement<TQuality>& candidate,
        const ClockCoordinationAdvertisement<TQuality>& incumbent,
        const IClockQualityPolicy<TQuality>& quality
    ) const noexcept = 0;
};

/// <summary>
/// Default root policy: better root quality wins; exact ties use the advertised root DeviceIdentifier deterministically.
/// </summary>
template<typename TQuality>
class DefaultClockRootElectionPolicy final : public IClockRootElectionPolicy<TQuality> {
public:
    bool PreferRoot(
        const ClockCoordinationAdvertisement<TQuality>& candidate,
        const ClockCoordinationAdvertisement<TQuality>& incumbent,
        const IClockQualityPolicy<TQuality>& quality
    ) const noexcept override {
        const auto comparison = quality.Compare(candidate.RootQuality, incumbent.RootQuality);
        if (comparison == ClockQualityComparison::Better) return true;
        if (comparison == ClockQualityComparison::Worse) return false;
        return candidate.AdvertisedRoot < incumbent.AdvertisedRoot;
    }
};

/// <summary>
/// Default parent policy: prefer a lower stratum, then better advertised root quality, then DeviceIdentifier.
/// </summary>
template<typename TQuality>
class DefaultClockParentSelectionPolicy final : public IClockParentSelectionPolicy<TQuality> {
public:
    bool PreferParent(
        const ClockCoordinationAdvertisement<TQuality>& candidate,
        const ClockCoordinationAdvertisement<TQuality>& incumbent,
        const IClockQualityPolicy<TQuality>& quality
    ) const noexcept override {
        if (candidate.SenderStratum < incumbent.SenderStratum) return true;
        if (candidate.SenderStratum > incumbent.SenderStratum) return false;
        const auto comparison = quality.Compare(candidate.RootQuality, incumbent.RootQuality);
        if (comparison == ClockQualityComparison::Better) return true;
        if (comparison == ClockQualityComparison::Worse) return false;
        return candidate.Sender < incumbent.Sender;
    }
};

/// <summary>Result of one local bounded clock election pass.</summary>
struct ClockCoordinationSelection final {
    System::DeviceIdentifier Root{};
    System::DeviceIdentifier Parent{};
    MembershipIncarnation ParentIncarnation{};
    ClockStratum LocalStratum{InvalidClockStratum};

    constexpr bool HasRoot() const noexcept { return static_cast<bool>(Root); }
    constexpr bool HasParent() const noexcept { return static_cast<bool>(Parent); }
};

/// <summary>
/// Fixed-capacity store and election coordinator for authenticated informational clock advertisements.
/// </summary>
/// <remarks>
/// This type owns no timer, task, Radio exchange, Timing discipline or wire encoding. It only retains authenticated
/// observations and recomputes local root/parent choice from injected policies. Loop prevention is structural and cannot
/// be delegated away: a parent must advertise the elected root and must have a strictly better (numerically lower)
/// stratum than the local node. A root never has a parent.
/// </remarks>
template<typename TQuality, std::size_t Capacity = Limits::MaxMeshNodes>
class ClockCoordinationTable final {
    static_assert(Capacity > 0U, "Clock coordination capacity must be non-zero.");

    struct Slot final {
        ClockCoordinationAdvertisement<TQuality> Advertisement{};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0U};

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }

    /// <summary>
    /// Retains the newest authenticated observation from one exact sender incarnation.
    /// </summary>
    /// <remarks>
    /// A newly authenticated incarnation for the same DeviceIdentifier replaces the old informational clock observation.
    /// Monotonic-time regression within the same incarnation is rejected.
    /// </remarks>
    bool Observe(const ClockCoordinationAdvertisement<TQuality>& advertisement) noexcept {
        if (!advertisement.IsStructurallyValid()) return false;

        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Advertisement.Sender != advertisement.Sender) continue;
            if (slot.Advertisement.SenderIncarnation == advertisement.SenderIncarnation &&
                advertisement.ObservedAtMilliseconds < slot.Advertisement.ObservedAtMilliseconds) {
                return false;
            }
            slot.Advertisement = advertisement;
            return true;
        }

        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Advertisement = advertisement;
            slot.Occupied = true;
            ++_size;
            return true;
        }
        return false;
    }

    /// <summary>Removes informational clock state for one exact authenticated sender incarnation.</summary>
    bool Remove(
        const System::DeviceIdentifier& sender,
        const MembershipIncarnation& incarnation
    ) noexcept {
        if (!sender || !incarnation) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied ||
                slot.Advertisement.Sender != sender ||
                slot.Advertisement.SenderIncarnation != incarnation) continue;
            slot = {};
            --_size;
            return true;
        }
        return false;
    }

    /// <summary>Clears all retained informational clock observations.</summary>
    void Clear() noexcept {
        _slots = {};
        _size = 0U;
    }

    /// <summary>
    /// Recomputes the best root and loop-safe parent from current observations plus the local node's own root candidacy.
    /// </summary>
    ClockCoordinationSelection Select(
        const ClockCoordinationAdvertisement<TQuality>& local,
        const IClockQualityPolicy<TQuality>& quality,
        const IClockEligibilityPolicy<TQuality>& eligibility,
        const IClockRootElectionPolicy<TQuality>& rootElection,
        const IClockParentSelectionPolicy<TQuality>& parentSelection
    ) const noexcept {
        ClockCoordinationSelection result{};
        if (!local.IsStructurallyValid() || local.Sender != local.AdvertisedRoot || local.SenderStratum != ClockRootStratum) {
            return result;
        }

        const ClockCoordinationAdvertisement<TQuality>* rootSource = eligibility.IsEligible(local) ? &local : nullptr;
        for (const auto& slot : _slots) {
            if (!slot.Occupied || !eligibility.IsEligible(slot.Advertisement)) continue;
            if (rootSource == nullptr || rootElection.PreferRoot(slot.Advertisement, *rootSource, quality)) {
                rootSource = &slot.Advertisement;
            }
        }
        if (rootSource == nullptr) return result;

        result.Root = rootSource->AdvertisedRoot;
        if (result.Root == local.Sender) {
            result.LocalStratum = ClockRootStratum;
            return result;
        }

        const ClockCoordinationAdvertisement<TQuality>* parent = nullptr;
        // No current parent exists yet, so any usable upstream stratum is strictly better than InvalidClockStratum.
        for (const auto& slot : _slots) {
            const auto& candidate = slot.Advertisement;
            if (!slot.Occupied || !eligibility.IsEligible(candidate)) continue;
            if (candidate.AdvertisedRoot != result.Root || candidate.Sender == local.Sender) continue;
            if (candidate.SenderStratum == InvalidClockStratum || candidate.SenderStratum >= InvalidClockStratum - 1U) continue;
            if (parent == nullptr || parentSelection.PreferParent(candidate, *parent, quality)) parent = &candidate;
        }
        if (parent == nullptr) return result;

        result.Parent = parent->Sender;
        result.ParentIncarnation = parent->SenderIncarnation;
        result.LocalStratum = static_cast<ClockStratum>(parent->SenderStratum + 1U);
        return result;
    }
};

} // namespace ESPressio::Mesh
