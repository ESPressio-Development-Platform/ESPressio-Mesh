#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_Route.hpp"

namespace ESPressio::Mesh {

/// <summary>Immediate result of submitting one validated next-hop transfer to Radio.</summary>
enum class ForwardingSubmissionDisposition : std::uint8_t {
    Accepted,
    DeadlineExpired,
    HopLimitExhausted,
    MembershipUnavailable,
    PeerUnavailable,
    ResourceUnavailable,
    RetryableFailure,
    PermanentFailure,
    Invalid
};

struct ForwardingSubmissionResult final {
    ForwardingSubmissionDisposition Disposition{ForwardingSubmissionDisposition::Invalid};
    Radio::RadioTransportSendResult RadioResult{};

    constexpr explicit operator bool() const noexcept {
        return Disposition == ForwardingSubmissionDisposition::Accepted;
    }
};

/// <summary>
/// Resolves one already-validated local route's next hop to an exact authenticated neighbour incarnation and current
/// RadioPeerHandle, then submits immutable bytes through RadioTransport.
/// </summary>
/// <remarks>
/// `Accepted` means Radio accepted/submitted the direct-link logical transfer; it does not mean the Mesh hop or final
/// delivery completed and therefore does not consume RemainingHopLimit. Successful forwarding transition commitment is
/// intentionally separate (`CommitSuccessfulForwardingTransition`) and must be driven by later proven Mesh-hop evidence.
/// </remarks>
template<std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t HopCapacity = Limits::MaxRouteHops>
class ForwardingSubmissionCoordinator final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    const AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;
    Radio::RadioTransport& _transport;

    static ForwardingSubmissionDisposition MapFailure(const Radio::RadioTransportSendResult& result) noexcept {
        using TS = Radio::RadioTransportSendStatus;
        using LS = Radio::RadioSendStatus;
        switch (result.Status) {
            case TS::Accepted: return ForwardingSubmissionDisposition::Accepted;
            case TS::InvalidPeer: return ForwardingSubmissionDisposition::PeerUnavailable;
            case TS::NotStarted:
            case TS::InterfaceNotRegistered:
            case TS::RadioRejected:
                break;
            case TS::InvalidPayload:
            case TS::InvalidDestination:
            case TS::MessageTooLarge:
                return ForwardingSubmissionDisposition::PermanentFailure;
        }
        switch (result.LinkResult.Status) {
            case LS::Busy:
            case LS::NoMemory:
                return ForwardingSubmissionDisposition::ResourceUnavailable;
            case LS::NotStarted:
            case LS::NativeFailure:
                return ForwardingSubmissionDisposition::RetryableFailure;
            case LS::InvalidAddress:
            case LS::PayloadTooLarge:
            case LS::Unsupported:
                return ForwardingSubmissionDisposition::PermanentFailure;
            case LS::Accepted:
                return ForwardingSubmissionDisposition::RetryableFailure;
        }
        return ForwardingSubmissionDisposition::RetryableFailure;
    }

public:
    ForwardingSubmissionCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        const AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings,
        Radio::RadioTransport& transport
    ) noexcept : _memberships(memberships), _bindings(bindings), _transport(transport) {}

    ForwardingSubmissionResult Submit(
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) {
        if (!localDevice || route.Source() != localDevice || route.HopCount() == 0U ||
            (payload == nullptr && payloadSize != 0U)) {
            return {ForwardingSubmissionDisposition::Invalid, {}};
        }
        if (absoluteDeadlineMilliseconds == 0U || nowMilliseconds >= absoluteDeadlineMilliseconds) {
            return {ForwardingSubmissionDisposition::DeadlineExpired, {}};
        }
        if (remainingHopLimit == 0U) return {ForwardingSubmissionDisposition::HopLimitExhausted, {}};

        const auto* nextHop = route.NextHop();
        if (nextHop == nullptr || nextHop->Advertiser != localDevice) {
            return {ForwardingSubmissionDisposition::Invalid, {}};
        }

        const auto* membership = _memberships.FindDevice(nextHop->Neighbour);
        if (membership == nullptr || !membership->IsValid() || membership->Reachability == ReachabilityState::Unreachable) {
            return {ForwardingSubmissionDisposition::MembershipUnavailable, {}};
        }

        const auto* binding = _bindings.ResolveNextHop(*nextHop, localDevice, membership->Incarnation);
        if (binding == nullptr) return {ForwardingSubmissionDisposition::PeerUnavailable, {}};

        const auto radioResult = _transport.Send(binding->Peer, payload, payloadSize);
        return {MapFailure(radioResult), radioResult};
    }
};

} // namespace ESPressio::Mesh
