#pragma once

#include <cstddef>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of admitting one clock advertisement through authenticated membership authority.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class ClockObservationDisposition : std::uint8_t {
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
/**
 * ESPressio Memory Audit
 * Members:
 * - _membership (AuthenticatedMembershipTable<MembershipCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * - _clock (ClockCoordinationTable<TQuality, ClockCapacity>&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
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
