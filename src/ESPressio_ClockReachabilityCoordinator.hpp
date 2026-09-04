#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_ClockCoordination.hpp"
#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of converging retained clock-election state with authenticated member reachability.</summary>
enum class ClockReachabilityDisposition : std::uint8_t {
    Retained,
    Removed,
    MembershipUnavailable,
    Invalid
};

/// <summary>
/// Narrow lifecycle boundary that prevents an authenticated but currently Unreachable member from remaining a local
/// clock-election candidate.
/// </summary>
/// <remarks>
/// Reachability is not authentication and this type does not alter membership, choose a root/parent, discipline Timing,
/// schedule probes, or define any wire representation. Reachable and Suspect members retain their informational clock
/// advertisements; only the exact current Unreachable incarnation is removed. A later authenticated observation may be
/// retained again after liveness has restored that member to Reachable.
/// </remarks>
template<
    typename TQuality,
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t ClockCapacity = Limits::MaxMeshNodes
>
class ClockReachabilityCoordinator final {
    AuthenticatedMembershipTable<MembershipCapacity>& _membership;
    ClockCoordinationTable<TQuality, ClockCapacity>& _clock;

public:
    ClockReachabilityCoordinator(
        AuthenticatedMembershipTable<MembershipCapacity>& membership,
        ClockCoordinationTable<TQuality, ClockCapacity>& clock
    ) noexcept
        : _membership(membership), _clock(clock) {}

    /// <summary>
    /// Applies the exact authenticated member's current reachability to retained informational clock state.
    /// </summary>
    ClockReachabilityDisposition Converge(
        const System::DeviceIdentifier& device,
        const MembershipIncarnation& incarnation
    ) noexcept {
        if (!device || !incarnation) return ClockReachabilityDisposition::Invalid;
        const auto* member = _membership.FindExact(device, incarnation);
        if (member == nullptr) return ClockReachabilityDisposition::MembershipUnavailable;
        if (member->Reachability != ReachabilityState::Unreachable) {
            return ClockReachabilityDisposition::Retained;
        }
        return _clock.Remove(device, incarnation)
            ? ClockReachabilityDisposition::Removed
            : ClockReachabilityDisposition::Retained;
    }
};

} // namespace ESPressio::Mesh
