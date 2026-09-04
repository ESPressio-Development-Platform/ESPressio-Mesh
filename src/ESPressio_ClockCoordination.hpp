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
enum class ClockQualityComparison : std::uint8_t { Better, Equivalent, Worse };

template<typename TQuality>
struct ClockCoordinationAdvertisement final {
    System::DeviceIdentifier Sender{};
    MembershipIncarnation SenderIncarnation{};
    System::DeviceIdentifier AdvertisedRoot{};
    ClockStratum SenderStratum{InvalidClockStratum};
    TQuality RootQuality{};
    std::uint64_t ObservedAtMilliseconds{0};

    constexpr bool IsStructurallyValid() const noexcept {
        return static_cast<bool>(Sender) && static_cast<bool>(SenderIncarnation) &&
               static_cast<bool>(AdvertisedRoot) && SenderStratum != InvalidClockStratum &&
               ObservedAtMilliseconds != 0U;
    }
};

template<typename TQuality>
class IClockQualityPolicy {
public:
    virtual ~IClockQualityPolicy() = default;
    virtual ClockQualityComparison Compare(const TQuality& candidate, const TQuality& incumbent) const noexcept = 0;
};

template<typename TQuality>
class IClockEligibilityPolicy {
public:
    virtual ~IClockEligibilityPolicy() = default;
    virtual bool IsEligible(const ClockCoordinationAdvertisement<TQuality>& advertisement) const noexcept = 0;
};

/// <summary>
/// Independent policy deciding whether an otherwise eligible advertisement is usable as this node's immediate
/// synchronization parent.
/// </summary>
/// <remarks>
/// Root eligibility and parent usability are intentionally distinct. A globally preferable root may be several Mesh
/// hops away, while precision synchronization is performed only through a currently usable direct authenticated parent.
/// </remarks>
template<typename TQuality>
class IClockParentUsabilityPolicy {
public:
    virtual ~IClockParentUsabilityPolicy() = default;
    virtual bool IsUsableParent(const ClockCoordinationAdvertisement<TQuality>& advertisement) const noexcept = 0;
};

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

struct ClockCoordinationSelection final {
    System::DeviceIdentifier Root{};
    System::DeviceIdentifier Parent{};
    MembershipIncarnation ParentIncarnation{};
    ClockStratum LocalStratum{InvalidClockStratum};

    constexpr bool HasRoot() const noexcept { return static_cast<bool>(Root); }
    constexpr bool HasParent() const noexcept { return static_cast<bool>(Parent); }
};

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

    bool Observe(const ClockCoordinationAdvertisement<TQuality>& advertisement) noexcept {
        if (!advertisement.IsStructurallyValid()) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Advertisement.Sender != advertisement.Sender) continue;
            if (slot.Advertisement.SenderIncarnation == advertisement.SenderIncarnation &&
                advertisement.ObservedAtMilliseconds < slot.Advertisement.ObservedAtMilliseconds) return false;
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

    bool Remove(const System::DeviceIdentifier& sender, const MembershipIncarnation& incarnation) noexcept {
        if (!sender || !incarnation) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Advertisement.Sender != sender ||
                slot.Advertisement.SenderIncarnation != incarnation) continue;
            slot = {};
            --_size;
            return true;
        }
        return false;
    }

    void Clear() noexcept { _slots = {}; _size = 0U; }

    /// <summary>
    /// Recomputes the best root and loop-safe immediately usable parent from current observations plus local root
    /// candidacy.
    /// </summary>
    /// <remarks>
    /// `eligibility` controls participation in root election. `parentUsability` is applied only while selecting the
    /// immediate synchronization parent, so a non-direct elected root remains valid when a direct neighbour advertises
    /// that same root. This preserves multi-hop root coordination without performing precision exchange through Mesh
    /// forwarding queues.
    /// </remarks>
    ClockCoordinationSelection Select(
        const ClockCoordinationAdvertisement<TQuality>& local,
        const IClockQualityPolicy<TQuality>& quality,
        const IClockEligibilityPolicy<TQuality>& eligibility,
        const IClockParentUsabilityPolicy<TQuality>& parentUsability,
        const IClockRootElectionPolicy<TQuality>& rootElection,
        const IClockParentSelectionPolicy<TQuality>& parentSelection
    ) const noexcept {
        ClockCoordinationSelection result{};
        if (!local.IsStructurallyValid() || local.Sender != local.AdvertisedRoot || local.SenderStratum != ClockRootStratum)
            return result;

        const ClockCoordinationAdvertisement<TQuality>* rootSource = eligibility.IsEligible(local) ? &local : nullptr;
        for (const auto& slot : _slots) {
            if (!slot.Occupied || !eligibility.IsEligible(slot.Advertisement)) continue;
            if (rootSource == nullptr || rootElection.PreferRoot(slot.Advertisement, *rootSource, quality))
                rootSource = &slot.Advertisement;
        }
        if (rootSource == nullptr) return result;

        result.Root = rootSource->AdvertisedRoot;
        if (result.Root == local.Sender) {
            result.LocalStratum = ClockRootStratum;
            return result;
        }

        const ClockCoordinationAdvertisement<TQuality>* parent = nullptr;
        for (const auto& slot : _slots) {
            const auto& candidate = slot.Advertisement;
            if (!slot.Occupied || !eligibility.IsEligible(candidate) || !parentUsability.IsUsableParent(candidate)) continue;
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
