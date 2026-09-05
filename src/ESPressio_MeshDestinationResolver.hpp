#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

struct FrozenMeshRecipient final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};

    constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(Device) && static_cast<bool>(Incarnation);
    }
};

enum class MeshDestinationResolutionDisposition : std::uint8_t {
    Resolved,
    NoRecipients,
    ResourceUnavailable,
    Invalid
};

/// <summary>Fixed immutable-by-convention recipient snapshot produced by Group or CapabilitySelector resolution.</summary>
template<std::size_t Capacity = Limits::MaxRecipientsPerTransmission>
class FrozenMeshRecipientSet final {
    static_assert(Capacity > 0U, "Frozen recipient capacity must be non-zero.");
    std::array<FrozenMeshRecipient, Capacity> _recipients{};
    std::size_t _size{0U};

    static constexpr bool Less(const FrozenMeshRecipient& left, const FrozenMeshRecipient& right) noexcept {
        return left.Device < right.Device ||
               (!(right.Device < left.Device) && left.Incarnation < right.Incarnation);
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }
    constexpr const FrozenMeshRecipient* At(std::size_t index) const noexcept {
        return index < _size ? &_recipients[index] : nullptr;
    }
    void Clear() noexcept {
        _recipients = {};
        _size = 0U;
    }
    bool Insert(const FrozenMeshRecipient& recipient) noexcept {
        if (!recipient || _size >= Capacity) return false;
        for (std::size_t index = 0U; index < _size; ++index) {
            if (_recipients[index].Device == recipient.Device) return false;
        }
        std::size_t insertion = _size;
        while (insertion > 0U && Less(recipient, _recipients[insertion - 1U])) {
            _recipients[insertion] = _recipients[insertion - 1U];
            --insertion;
        }
        _recipients[insertion] = recipient;
        ++_size;
        return true;
    }
};

/// <summary>Resolves authenticated active remote-member profiles into one frozen bounded Node recipient set.</summary>
/// <remarks>
/// Group and CapabilitySelector identities never become a shared delivery-success claim. Resolution snapshots exact
/// DeviceIdentifier + MembershipIncarnation pairs, after which the application aggregate issues independent MessageIds
/// and outcomes. Reachability is deliberately not a membership filter: an Active member remains a recipient while route
/// planning independently determines whether and when it can be reached before the immutable deadline.
/// </remarks>
template<std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission>
class MeshDestinationResolver final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;

    template<typename TPredicate>
    MeshDestinationResolutionDisposition Resolve(
        TPredicate&& predicate,
        FrozenMeshRecipientSet<RecipientCapacity>& recipients
    ) const noexcept {
        recipients.Clear();
        bool overflow = false;
        _memberships.ForEachAuthenticated([&](const AuthenticatedMembershipRecord& member) noexcept {
            if (overflow || member.State != MembershipState::Active || !member.Profile ||
                !predicate(member.Profile)) return;
            if (!recipients.Insert({member.Device, member.Incarnation})) overflow = true;
        });
        if (overflow) {
            recipients.Clear();
            return MeshDestinationResolutionDisposition::ResourceUnavailable;
        }
        return recipients.Empty()
            ? MeshDestinationResolutionDisposition::NoRecipients
            : MeshDestinationResolutionDisposition::Resolved;
    }

public:
    explicit MeshDestinationResolver(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships
    ) noexcept : _memberships(memberships) {}

    MeshDestinationResolutionDisposition ResolveGroup(
        const GroupIdentifier& group,
        FrozenMeshRecipientSet<RecipientCapacity>& recipients
    ) const noexcept {
        if (!group) {
            recipients.Clear();
            return MeshDestinationResolutionDisposition::Invalid;
        }
        return Resolve([&](const MeshNodeProfile& profile) noexcept { return profile.HasGroup(group); }, recipients);
    }

    MeshDestinationResolutionDisposition ResolveCapabilitySelector(
        CapabilityMask requiredCapabilities,
        FrozenMeshRecipientSet<RecipientCapacity>& recipients
    ) const noexcept {
        if (requiredCapabilities == 0U) {
            recipients.Clear();
            return MeshDestinationResolutionDisposition::Invalid;
        }
        return Resolve(
            [&](const MeshNodeProfile& profile) noexcept {
                return profile.SupportsAll(requiredCapabilities);
            },
            recipients);
    }
};

} // namespace ESPressio::Mesh
