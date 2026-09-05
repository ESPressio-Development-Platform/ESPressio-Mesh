#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_DeduplicationWindow.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshNodeProfile.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>One bounded authenticated participation record retained by Mesh.</summary>
/// <remarks>
/// The record owns committed ordinary-delivery deduplication and the bounded accepted-delivery subset for exactly one
/// authenticated DeviceIdentifier + MembershipIncarnation namespace. The subset lets a duplicate of an accepted
/// delivery regenerate a lost positive ACK without treating other definitive dispositions as accepted. Discovery and
/// pre-authentication candidates do not belong in this table and therefore cannot reserve or advance either window.
/// </remarks>
struct AuthenticatedMembershipRecord final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MembershipState State{MembershipState::Unknown};
    ReachabilityState Reachability{ReachabilityState::Unknown};
    DeduplicationWindow<Limits::DeduplicationWindowBits> DeliveryDeduplication{};
    DeduplicationWindow<Limits::DeduplicationWindowBits> AcceptedDeliveryDeduplication{};
    DeduplicationWindow<Limits::DeduplicationWindowBits> BroadcastDeduplication{};
    MeshNodeProfile Profile{};

    /// <summary>Returns whether this record identifies an authenticated participation state.</summary>
    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Device) &&
               static_cast<bool>(Incarnation) &&
               (State == MembershipState::Validating ||
                State == MembershipState::Joining ||
                State == MembershipState::Active);
    }
};

/// <summary>Immediate result of inserting one authenticated membership record.</summary>
enum class AuthenticatedMembershipInsertResult : std::uint8_t {
    Inserted,
    Updated,
    ConflictingIncarnation,
    ResourceUnavailable,
    Invalid
};

enum class AuthenticatedProfileUpdateResult : std::uint8_t {
    Applied,
    Unchanged,
    StaleGeneration,
    ConflictingGeneration,
    ConflictingAlias,
    ConflictingCanonicalName,
    UnknownAuthenticatedMembership,
    Invalid
};

/// <summary>
/// Fixed-capacity authenticated membership/incarnation table.
/// </summary>
/// <remarks>
/// A DeviceIdentifier may have at most one retained authenticated incarnation in this table.
/// Replacement by a genuinely new incarnation is intentionally not implicit: the membership/
/// admission service must first resolve supersession, tombstoning and authentication, remove the
/// old record, then insert the new authenticated incarnation. This prevents an unauthenticated or
/// merely newer-looking claim from silently replacing current authority.
///
/// Mutation is intended for the serialized Mesh execution domain; this container owns no task or
/// mutex. Full unreachable-record retention and policy-driven expiry are higher membership-service
/// responsibilities and may retain these same records for the configured bounded interval.
/// </remarks>
template<std::size_t Capacity = Limits::MaxMeshNodes>
class AuthenticatedMembershipTable final {
    static_assert(Capacity > 0, "Authenticated membership capacity must be non-zero.");

    struct Slot final {
        AuthenticatedMembershipRecord Record{};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

    static constexpr bool IsAuthenticatedState(MembershipState state) noexcept {
        return state == MembershipState::Validating ||
               state == MembershipState::Joining ||
               state == MembershipState::Active;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>Non-mutating classification for a serialized authenticated upsert.</summary>
    AuthenticatedMembershipInsertResult PreflightAuthenticatedUpsert(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        if (!device || !incarnation) return AuthenticatedMembershipInsertResult::Invalid;
        for (const auto& slot : _slots) {
            if (!slot.Occupied || slot.Record.Device != device) continue;
            return slot.Record.Incarnation == incarnation
                ? AuthenticatedMembershipInsertResult::Updated
                : AuthenticatedMembershipInsertResult::ConflictingIncarnation;
        }
        return _size < Capacity
            ? AuthenticatedMembershipInsertResult::Inserted
            : AuthenticatedMembershipInsertResult::ResourceUnavailable;
    }

    /// <summary>Finds the authenticated record for an exact device/incarnation identity.</summary>
    AuthenticatedMembershipRecord* FindExact(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        if (!device || !incarnation) return nullptr;
        for (auto& slot : _slots) {
            if (slot.Occupied &&
                slot.Record.Device == device &&
                slot.Record.Incarnation == incarnation) {
                return &slot.Record;
            }
        }
        return nullptr;
    }

    const AuthenticatedMembershipRecord* FindExact(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        if (!device || !incarnation) return nullptr;
        for (const auto& slot : _slots) {
            if (slot.Occupied &&
                slot.Record.Device == device &&
                slot.Record.Incarnation == incarnation) {
                return &slot.Record;
            }
        }
        return nullptr;
    }

    /// <summary>Finds whichever authenticated incarnation is currently retained for one device.</summary>
    AuthenticatedMembershipRecord* FindDevice(
        const System::DeviceIdentifier& device
    ) noexcept {
        if (!device) return nullptr;
        for (auto& slot : _slots) {
            if (slot.Occupied && slot.Record.Device == device) return &slot.Record;
        }
        return nullptr;
    }

    const AuthenticatedMembershipRecord* FindDevice(
        const System::DeviceIdentifier& device
    ) const noexcept {
        if (!device) return nullptr;
        for (const auto& slot : _slots) {
            if (slot.Occupied && slot.Record.Device == device) return &slot.Record;
        }
        return nullptr;
    }

    /// <summary>
    /// Inserts or updates an already-authenticated participation without replacing another incarnation.
    /// </summary>
    AuthenticatedMembershipInsertResult UpsertAuthenticated(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        MembershipState state,
        ReachabilityState reachability = ReachabilityState::Unknown
    ) noexcept {
        if (!device || !incarnation || !IsAuthenticatedState(state)) {
            return AuthenticatedMembershipInsertResult::Invalid;
        }

        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Record.Device != device) continue;
            if (slot.Record.Incarnation != incarnation) {
                return AuthenticatedMembershipInsertResult::ConflictingIncarnation;
            }
            slot.Record.State = state;
            slot.Record.Reachability = reachability;
            return AuthenticatedMembershipInsertResult::Updated;
        }

        for (auto& slot : _slots) {
            if (slot.Occupied) continue;
            slot.Record.Device = device;
            slot.Record.Incarnation = incarnation;
            slot.Record.State = state;
            slot.Record.Reachability = reachability;
            slot.Record.DeliveryDeduplication.Reset();
            slot.Record.AcceptedDeliveryDeduplication.Reset();
            slot.Record.BroadcastDeduplication.Reset();
            slot.Record.Profile = {};
            slot.Occupied = true;
            ++_size;
            return AuthenticatedMembershipInsertResult::Inserted;
        }

        return AuthenticatedMembershipInsertResult::ResourceUnavailable;
    }

    /// <summary>Applies a verified profile only to its exact authenticated member and non-regressing generation.</summary>
    AuthenticatedProfileUpdateResult ApplyAuthenticatedProfile(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        const MeshNodeProfile& profile
    ) noexcept {
        if (!device || !incarnation || !profile) return AuthenticatedProfileUpdateResult::Invalid;
        auto* record = FindExact(device, incarnation);
        if (record == nullptr) return AuthenticatedProfileUpdateResult::UnknownAuthenticatedMembership;
        if (record->Profile) {
            if (profile.Generation() < record->Profile.Generation()) {
                return AuthenticatedProfileUpdateResult::StaleGeneration;
            }
            if (profile.Generation() == record->Profile.Generation()) {
                return profile == record->Profile
                    ? AuthenticatedProfileUpdateResult::Unchanged
                    : AuthenticatedProfileUpdateResult::ConflictingGeneration;
            }
        }
        for (const auto& slot : _slots) {
            if (!slot.Occupied || &slot.Record == record || !slot.Record.Profile) continue;
            if (slot.Record.Profile.Alias() == profile.Alias()) {
                return AuthenticatedProfileUpdateResult::ConflictingAlias;
            }
            if (slot.Record.Profile.Name() == profile.Name()) {
                return AuthenticatedProfileUpdateResult::ConflictingCanonicalName;
            }
        }
        record->Profile = profile;
        return AuthenticatedProfileUpdateResult::Applied;
    }

    /// <summary>Visits current authenticated records in fixed slot order inside the serialized Mesh domain.</summary>
    template<typename TVisitor>
    void ForEachAuthenticated(TVisitor&& visitor) const noexcept {
        for (const auto& slot : _slots) if (slot.Occupied) visitor(slot.Record);
    }

    /// <summary>Updates membership state only for the exact authenticated incarnation.</summary>
    bool SetMembershipState(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        MembershipState state
    ) noexcept {
        if (!IsAuthenticatedState(state)) return false;
        auto* record = FindExact(device, incarnation);
        if (record == nullptr) return false;
        record->State = state;
        return true;
    }

    /// <summary>Updates local reachability only for the exact authenticated incarnation.</summary>
    bool SetReachability(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        ReachabilityState reachability
    ) noexcept {
        auto* record = FindExact(device, incarnation);
        if (record == nullptr) return false;
        record->Reachability = reachability;
        return true;
    }

    /// <summary>
    /// Removes exactly one authenticated incarnation after higher membership logic has resolved its disposition.
    /// </summary>
    bool Remove(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        if (!device || !incarnation) return false;
        for (auto& slot : _slots) {
            if (!slot.Occupied ||
                slot.Record.Device != device ||
                slot.Record.Incarnation != incarnation) continue;
            slot = {};
            --_size;
            return true;
        }
        return false;
    }

    /// <summary>Clears all authenticated records during controlled Mesh shutdown/reset.</summary>
    void Clear() noexcept {
        _slots = {};
        _size = 0U;
    }
};

} // namespace ESPressio::Mesh
