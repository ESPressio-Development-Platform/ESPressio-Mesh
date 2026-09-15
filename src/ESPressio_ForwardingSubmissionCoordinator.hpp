#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_MeshRadioSubmission.hpp"
#include "ESPressio_MeshRemainingResidence.hpp"
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

struct ForwardingSubmissionResult final {
    ForwardingSubmissionDisposition Disposition{ForwardingSubmissionDisposition::Invalid};
    MeshRadioSubmissionResult RadioResult{};
    Radio::RadioContentionDomainId RadioDomain{};
    System::DeviceIdentifier NextHop{};
    MembershipIncarnation NextHopIncarnation{};

    constexpr explicit operator bool() const noexcept {
        return Disposition==ForwardingSubmissionDisposition::Accepted;
    }
};

/// <summary>
/// Resolves one validated route to an exact authenticated neighbour and submits immutable bytes through the final
/// family-neutral managed-Radio seam.
/// </summary>
/// <remarks>
/// Accepted means only that Radio/R3 took bounded ownership of the logical transfer. It never implies direct-link
/// completion, peer acknowledgement or Mesh next-hop acceptance. The returned contention-domain plus transfer-id pair is
/// retained solely for later terminal Radio correlation. The neutral service class is supplied by the frozen upper
/// transport binding and is never inferred from Primitive family/type semantics inside Mesh.
/// </remarks>
template<std::size_t MembershipCapacity=Limits::MaxMeshNodes,
         std::size_t BindingCapacity=Limits::MaxTopologyLinks,
         std::size_t HopCapacity=Limits::MaxRouteHops>
class ForwardingSubmissionCoordinator final {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    const AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;
    MeshRadioSubmissionTarget _radio{};
    MeshRadioDomainResolver _domains{};

    static ForwardingSubmissionDisposition MapFailure(Radio::RadioSchedulerStatus status) noexcept {
        switch(status){
            case Radio::RadioSchedulerStatus::Success:return ForwardingSubmissionDisposition::Accepted;
            case Radio::RadioSchedulerStatus::Busy:
            case Radio::RadioSchedulerStatus::ResourceUnavailable:
                return ForwardingSubmissionDisposition::ResourceUnavailable;
            case Radio::RadioSchedulerStatus::ProviderUnavailable:
            case Radio::RadioSchedulerStatus::NotInitialized:
            case Radio::RadioSchedulerStatus::Frozen:
                return ForwardingSubmissionDisposition::RetryableFailure;
            case Radio::RadioSchedulerStatus::Expired:
                return ForwardingSubmissionDisposition::DeadlineExpired;
            case Radio::RadioSchedulerStatus::PayloadTooLarge:
            case Radio::RadioSchedulerStatus::InvalidConfiguration:
                return ForwardingSubmissionDisposition::PermanentFailure;
        }
        return ForwardingSubmissionDisposition::PermanentFailure;
    }

public:
    ForwardingSubmissionCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        const AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings,
        MeshRadioSubmissionTarget radio,
        MeshRadioDomainResolver domains
    ) noexcept : _memberships(memberships),_bindings(bindings),_radio(radio),_domains(domains) {}

    ForwardingSubmissionResult Submit(
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        MeshRelayServiceClass service,
        const std::uint8_t* payload,
        std::size_t payloadSize,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) noexcept {
        if(!localDevice||route.Source()!=localDevice||route.HopCount()==0U||
           !IsMeshRelayServiceClass(service)||(payload==nullptr&&payloadSize!=0U)||!_radio||!_domains)
            return {};
        if(absoluteDeadlineMilliseconds==0U||nowMilliseconds>=absoluteDeadlineMilliseconds)
            return {ForwardingSubmissionDisposition::DeadlineExpired};
        if(remainingHopLimit==0U) return {ForwardingSubmissionDisposition::HopLimitExhausted};
        if(absoluteDeadlineMilliseconds>std::numeric_limits<std::uint64_t>::max()/MeshNanosecondsPerMillisecond)
            return {ForwardingSubmissionDisposition::PermanentFailure};

        const auto* nextHop=route.NextHop();
        if(nextHop==nullptr||nextHop->Advertiser!=localDevice) return {};
        const auto* membership=_memberships.FindDevice(nextHop->Neighbour);
        if(membership==nullptr||!membership->IsValid()||membership->Reachability==ReachabilityState::Unreachable)
            return {ForwardingSubmissionDisposition::MembershipUnavailable};
        const auto* binding=_bindings.ResolveNextHop(*nextHop,localDevice,membership->Incarnation);
        if(binding==nullptr) return {ForwardingSubmissionDisposition::PeerUnavailable};

        const auto domain=_domains.ResolveLocalRadio(binding->LocalRadio);
        if(!domain) return {ForwardingSubmissionDisposition::PeerUnavailable};
        const auto radioResult=_radio.SubmitPeer(
            binding->Peer,service,absoluteDeadlineMilliseconds*MeshNanosecondsPerMillisecond,payload,payloadSize);
        const auto disposition=MapFailure(radioResult.Status);
        if(disposition!=ForwardingSubmissionDisposition::Accepted)
            return {disposition,radioResult,domain};
        return {disposition,radioResult,domain,nextHop->Neighbour,membership->Incarnation};
    }
};

} // namespace ESPressio::Mesh
