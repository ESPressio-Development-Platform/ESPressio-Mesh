#pragma once

#include <cstddef>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of admitting one clock advertisement through authenticated membership authority.</summary>
enum class ClockObservationDisposition : std::uint8_t {
    Observed,
    MembershipUnavailable,
    MembershipNotActive,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Narrow lifecycle boundary between authenticated Mesh membership and informational clock coordination state.
/// </summary>
/// <remarks>
/// This coordinator does not authenticate advertisements, choose roots/parents, discipline a clock, schedule exchanges,
/// or define a wire representation. It merely prevents unauthenticated, stale-incarnation, or non-Active membership
/// claims from entering the clock-election table and provides exact-incarnation cleanup when membership is retired.
/// The caller remains responsible for establishing message authenticity before invoking ObserveAuthenticated().
/// </remarks>
template<
    typename TQuality,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t ClockCapacity = Limits::MaxMeshNodes
>
class ClockMembershipCoordinator final {
    AuthenticatedMembershipTable<MembershipCapacity>& _membership;
    ClockCoordinationTable<TQuality, ClockCapacity>& _clock;

public:
    ClockMembershipCoordinator(
        AuthenticatedMembershipTable<MembershipCapacity>& membership,
        ClockCoordinationTable<TQuality, ClockCapacity>& clock
    ) noexcept
        : _membership(membership), _clock(clock) {}

    /// <summary>
    /// Retains an already-authenticated clock advertisement only for the exact currently Active membership incarnation.
    /// </summary>
    ClockObservationDisposition ObserveAuthenticated(
        const ClockCoordinationAdvertisement<TQuality>& advertisement
    ) noexcept {
        if (!advertisement.IsStructurallyValid()) return ClockObservationDisposition::Invalid;

        const auto* member = _membership.FindExact(advertisement.Sender, advertisement.SenderIncarnation);
        if (member == nullptr) return ClockObservationDisposition::MembershipUnavailable;
        if (member->State != MembershipState::Active) return ClockObservationDisposition::MembershipNotActive;

        return _clock.Observe(advertisement)
            ? ClockObservationDisposition::Observed
            : ClockObservationDisposition::ResourceUnavailable;
    }

    /// <summary>
    /// Removes clock-election state for exactly the retired membership incarnation.
    /// </summary>
    /// <remarks>
    /// This operation intentionally does not remove authenticated membership itself. Membership retirement remains owned
    /// by MembershipRetentionCoordinator/MembershipLifecycleCoordinator; call this as part of the same serialized Mesh
    /// lifecycle transition after the retirement decision has been made.
    /// </remarks>
    bool ForgetRetiredMembership(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        return _clock.Remove(device, incarnation);
    }

    /// <summary>Clears informational clock state during controlled Mesh shutdown/reset.</summary>
    void Clear() noexcept { _clock.Clear(); }
};

} // namespace ESPressio::Mesh
