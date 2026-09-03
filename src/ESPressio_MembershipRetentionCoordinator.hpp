#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_MembershipTombstoneTable.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of retiring one full authenticated membership record into compact local history.</summary>
enum class MembershipRetirementResult : std::uint8_t {
    Retired,
    MembershipNotFound,
    Invalid
};

/// <summary>
/// Coordinates bounded removal of full authenticated membership state with compact tombstone retention.
/// </summary>
/// <remarks>
/// This component deliberately does not decide *when* a member should be retired. Authoritative Leave,
/// authenticated incarnation supersession and policy-decided local forgetting are distinct higher-level
/// decisions. Once one of those decisions is made, this coordinator guarantees that compact history is
/// recorded before the full record (including delivery dedup state) is released.
///
/// Tombstone exhaustion never prevents retirement: MembershipTombstoneTable deterministically evicts
/// older lower-protection history according to the frozen policy, so retained history remains independent
/// from active-member capacity and cannot weaken later authentication requirements.
/// </remarks>
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t TombstoneCapacity = Limits::MaxMembershipTombstones
>
class MembershipRetentionCoordinator final {
public:
    using MembershipTable = AuthenticatedMembershipTable<MembershipCapacity>;
    using TombstoneTable = MembershipTombstoneTable<TombstoneCapacity>;

private:
    MembershipTable& _memberships;
    TombstoneTable& _tombstones;

    MembershipRetirementResult Retire(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        MembershipTombstoneDisposition disposition,
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds
    ) noexcept {
        if (!device || !incarnation || retentionMilliseconds == 0U) {
            return MembershipRetirementResult::Invalid;
        }
        if (_memberships.FindExact(device, incarnation) == nullptr) {
            return MembershipRetirementResult::MembershipNotFound;
        }
        if (!_tombstones.Record(
                device,
                incarnation,
                disposition,
                nowMilliseconds,
                retentionMilliseconds)) {
            return MembershipRetirementResult::Invalid;
        }
        return _memberships.Remove(device, incarnation)
            ? MembershipRetirementResult::Retired
            : MembershipRetirementResult::MembershipNotFound;
    }

public:
    MembershipRetentionCoordinator(
        MembershipTable& memberships,
        TombstoneTable& tombstones
    ) noexcept :
        _memberships(memberships),
        _tombstones(tombstones) {
    }

    /// <summary>Retires a membership after an authenticated authoritative Leave.</summary>
    MembershipRetirementResult RecordAuthoritativeLeave(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        return Retire(
            device,
            incarnation,
            MembershipTombstoneDisposition::AuthoritativeLeave,
            nowMilliseconds,
            retentionMilliseconds
        );
    }

    /// <summary>Retires an old incarnation after authenticated supersession has been established externally.</summary>
    MembershipRetirementResult RecordSupersededIncarnation(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        return Retire(
            device,
            incarnation,
            MembershipTombstoneDisposition::SupersededIncarnation,
            nowMilliseconds,
            retentionMilliseconds
        );
    }

    /// <summary>Retires a full unreachable record after the injected retention policy has decided to forget it locally.</summary>
    MembershipRetirementResult RecordLocallyForgotten(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation,
        std::uint64_t nowMilliseconds,
        std::uint64_t retentionMilliseconds =
            Limits::MembershipTombstoneRetentionMilliseconds
    ) noexcept {
        return Retire(
            device,
            incarnation,
            MembershipTombstoneDisposition::LocallyForgotten,
            nowMilliseconds,
            retentionMilliseconds
        );
    }
};

} // namespace ESPressio::Mesh
