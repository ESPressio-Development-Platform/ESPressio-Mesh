#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>
#include <ESPressio_RadioTypes.hpp>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Generation-safe local handle for one pending neighbour candidate slot.</summary>
struct NeighbourCandidateHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const NeighbourCandidateHandle& other) const noexcept {
        return Slot == other.Slot && Generation == other.Generation;
    }
    constexpr bool operator!=(const NeighbourCandidateHandle& other) const noexcept {
        return !(*this == other);
    }
};

/// <summary>
/// Unauthenticated identity claims associated with one discovered neighbour candidate.
/// </summary>
/// <remarks>
/// These fields are hints only. They must never be treated as authenticated DeviceIdentifier or
/// MembershipIncarnation authority and cannot affect authenticated membership, liveness or deduplication.
/// </remarks>
struct UntrustedMembershipClaim final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
};

/// <summary>One bounded pre-authentication neighbour candidate.</summary>
struct PendingNeighbourCandidate final {
    NeighbourCandidateHandle Handle{};
    RadioIdentifier Radio{0};
    Radio::RadioPeerHandle Peer{};
    UntrustedMembershipClaim Claim{};
    MembershipState State{MembershipState::Discovered};
    std::uint64_t FirstObservedMilliseconds{0};
    std::uint64_t LastObservedMilliseconds{0};
};

/// <summary>Result of admitting one candidate into bounded pre-authentication storage.</summary>
enum class PendingCandidateInsertResult : std::uint8_t {
    Inserted,
    Refreshed,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed-capacity storage for Discovered/Authenticating neighbours before authenticated membership exists.
/// </summary>
/// <remarks>
/// Each candidate is bound to the Mesh-local RadioIdentifier plus a generation-safe Radio-owned peer handle.
/// Neither that link binding nor the claimed DeviceIdentifier/MembershipIncarnation grants identity authority;
/// authentication/admission must succeed before promotion into AuthenticatedMembershipTable.
/// </remarks>
template<std::size_t Capacity = Limits::MaxPendingNeighbourCandidates>
class PendingNeighbourCandidateTable final {
    static_assert(Capacity > 0, "Pending neighbour capacity must be non-zero.");
    static_assert(Capacity < std::numeric_limits<std::uint16_t>::max(),
                  "Candidate slot index must fit the generation-safe public handle.");

    struct Slot final {
        PendingNeighbourCandidate Candidate{};
        std::uint16_t Generation{0};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

    static std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        ++current;
        if (current == 0U) ++current;
        return current;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }

    PendingNeighbourCandidate* Resolve(NeighbourCandidateHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return nullptr;
        return &slot.Candidate;
    }

    const PendingNeighbourCandidate* Resolve(NeighbourCandidateHandle handle) const noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        const auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return nullptr;
        return &slot.Candidate;
    }

    /// <summary>
    /// Inserts or refreshes an untrusted identity claim observed through one valid Radio/peer binding.
    /// </summary>
    PendingCandidateInsertResult Observe(
        RadioIdentifier radio,
        Radio::RadioPeerHandle peer,
        const UntrustedMembershipClaim& claim,
        std::uint64_t nowMilliseconds,
        NeighbourCandidateHandle& handle
    ) noexcept {
        if (radio == 0U || radio == 0xFFU || !peer ||
            !claim.Device || !claim.Incarnation || nowMilliseconds == 0U) {
            handle = {};
            return PendingCandidateInsertResult::Invalid;
        }

        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            if (slot.Candidate.Radio == radio &&
                slot.Candidate.Peer == peer &&
                slot.Candidate.Claim.Device == claim.Device &&
                slot.Candidate.Claim.Incarnation == claim.Incarnation) {
                if (nowMilliseconds >= slot.Candidate.LastObservedMilliseconds) {
                    slot.Candidate.LastObservedMilliseconds = nowMilliseconds;
                }
                handle = slot.Candidate.Handle;
                return PendingCandidateInsertResult::Refreshed;
            }
        }

        for (std::size_t index = 0; index < Capacity; ++index) {
            auto& slot = _slots[index];
            if (slot.Occupied) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Occupied = true;
            slot.Candidate = PendingNeighbourCandidate{
                NeighbourCandidateHandle{static_cast<std::uint16_t>(index), slot.Generation},
                radio,
                peer,
                claim,
                MembershipState::Discovered,
                nowMilliseconds,
                nowMilliseconds
            };
            ++_size;
            handle = slot.Candidate.Handle;
            return PendingCandidateInsertResult::Inserted;
        }

        handle = {};
        return PendingCandidateInsertResult::ResourceUnavailable;
    }

    /// <summary>Moves a pending candidate between the only two pre-authentication lifecycle states.</summary>
    bool SetState(NeighbourCandidateHandle handle, MembershipState state) noexcept {
        if (state != MembershipState::Discovered && state != MembershipState::Authenticating) return false;
        auto* candidate = Resolve(handle);
        if (candidate == nullptr) return false;
        candidate->State = state;
        return true;
    }

    /// <summary>Releases one exact generation-safe candidate slot.</summary>
    bool Remove(NeighbourCandidateHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return false;
        auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return false;
        slot.Candidate = {};
        slot.Occupied = false;
        --_size;
        return true;
    }
};

/// <summary>Result of claiming one bounded inbound authentication execution slot.</summary>
enum class InboundAuthenticationReservationResult : std::uint8_t {
    Reserved,
    AlreadyInProgress,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed-capacity execution reservations for expensive inbound authentication work.
/// </summary>
/// <remarks>
/// Reservation is keyed by a generation-safe pending-candidate handle. This table owns no security
/// semantics and stores no authenticated identity: successful authentication must be promoted separately
/// into AuthenticatedMembershipTable only after security and admission checks complete.
/// </remarks>
template<std::size_t Capacity = Limits::MaxActiveInboundAuthentications>
class InboundAuthenticationReservationTable final {
    static_assert(Capacity > 0, "Inbound authentication capacity must be non-zero.");

    struct Slot final {
        NeighbourCandidateHandle Candidate{};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }

    InboundAuthenticationReservationResult TryReserve(
        NeighbourCandidateHandle candidate
    ) noexcept {
        if (!candidate) return InboundAuthenticationReservationResult::Invalid;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Candidate == candidate) {
                return InboundAuthenticationReservationResult::AlreadyInProgress;
            }
        }
        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Candidate = candidate;
            slot.Occupied = true;
            ++_size;
            return InboundAuthenticationReservationResult::Reserved;
        }
        return InboundAuthenticationReservationResult::ResourceUnavailable;
    }

    bool Release(NeighbourCandidateHandle candidate) noexcept {
        if (!candidate) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Candidate != candidate) continue;
            slot = {};
            --_size;
            return true;
        }
        return false;
    }

    bool Contains(NeighbourCandidateHandle candidate) const noexcept {
        if (!candidate) return false;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Candidate == candidate) return true;
        }
        return false;
    }
};

} // namespace ESPressio::Mesh
