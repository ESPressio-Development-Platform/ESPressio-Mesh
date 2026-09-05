#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioTransport.hpp>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_Route.hpp"

namespace ESPressio::Mesh {

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

enum class ForwardingDirectLinkEvidence : std::uint8_t {
    None,
    SubmissionAccepted,
    TransmissionCompleted,
    PeerAcknowledged
};

struct ForwardingSubmissionResult final {
    ForwardingSubmissionDisposition Disposition{ForwardingSubmissionDisposition::Invalid};
    Radio::RadioTransportSendResult RadioResult{};
    System::DeviceIdentifier NextHop{};
    MembershipIncarnation NextHopIncarnation{};

    constexpr explicit operator bool() const noexcept {
        return Disposition == ForwardingSubmissionDisposition::Accepted;
    }

    constexpr ForwardingDirectLinkEvidence DirectLinkEvidence() const noexcept {
        if (Disposition != ForwardingSubmissionDisposition::Accepted) return ForwardingDirectLinkEvidence::None;
        if (RadioResult.LinkResult.Evidence.PeerAcknowledged()) return ForwardingDirectLinkEvidence::PeerAcknowledged;
        if (RadioResult.LinkResult.Evidence.TransmissionCompleted()) return ForwardingDirectLinkEvidence::TransmissionCompleted;
        return ForwardingDirectLinkEvidence::SubmissionAccepted;
    }
};

/// <summary>
/// Resolves one already-validated local route's next hop to an exact authenticated neighbour incarnation and current
/// RadioPeerHandle, then submits immutable bytes through RadioTransport.
/// </summary>
/// <remarks>
/// Accepted means Radio accepted every fragment of the direct-link logical transfer. Stronger synchronous Radio evidence
/// remains link evidence only and never consumes RemainingHopLimit. Explicit RadioTransport deferred-correlation pressure
/// maps to ResourceUnavailable so route-attempt policy can apply bounded backpressure/retry rather than treating local
/// bookkeeping exhaustion as a permanent route failure. On Accepted, NextHop and NextHopIncarnation are the exact
/// authenticated membership identity used to resolve the executable peer binding; callers must use those values rather
/// than reconstructing acceptance authority from route or discovery state.
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
            case TS::ResourceUnavailable: return ForwardingSubmissionDisposition::ResourceUnavailable;
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
        const auto disposition = MapFailure(radioResult);
        if (disposition != ForwardingSubmissionDisposition::Accepted) return {disposition, radioResult};
        return {disposition, radioResult, nextHop->Neighbour, membership->Incarnation};
    }
};

} // namespace ESPressio::Mesh
