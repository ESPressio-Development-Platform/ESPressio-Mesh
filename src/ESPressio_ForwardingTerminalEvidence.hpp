#pragma once

#include <cstdint>

#include <ESPressio_DeferredLogicalTransferObserverBridge.hpp>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_DirectPeerBindings.hpp"
#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Qualified terminal direct-link evidence available to Mesh after authenticated peer revalidation.</summary>
enum class ForwardingTerminalDisposition : std::uint8_t {
    TransmissionCompleted,
    PeerAcknowledged,
    TransmissionFailed
};

/// <summary>Authenticated context attached to one current deferred Radio terminal observation.</summary>
struct ForwardingTerminalEvidence final {
    System::DeviceIdentifier Neighbour{};
    MembershipIncarnation Incarnation{};
    RadioIdentifier LocalRadio{0};
    Radio::RadioPeerHandle Peer{};
    Radio::DeferredLogicalTransferHandle Transfer{};
    Radio::RadioTransferId RadioTransferId{0};
    ForwardingTerminalDisposition Disposition{ForwardingTerminalDisposition::TransmissionFailed};

    constexpr bool IsPeerAcknowledged() const noexcept {
        return Disposition == ForwardingTerminalDisposition::PeerAcknowledged;
    }
};

/// <summary>Consumes authenticated terminal direct-link evidence without defining Mesh delivery semantics.</summary>
/// <remarks>
/// This callback is intentionally weaker than a Mesh forwarding-success callback. Transmission completion without a
/// peer acknowledgement is surfaced distinctly and does not itself consume RemainingHopLimit. A higher Mesh delivery
/// service may combine this evidence with its own delivery acknowledgement/security contract.
/// </remarks>
class IForwardingTerminalEvidenceObserver {
public:
    virtual ~IForwardingTerminalEvidenceObserver() = default;
    virtual void OnForwardingTerminalEvidence(const ForwardingTerminalEvidence& evidence) = 0;
};

/// <summary>
/// Revalidates RadioTransport deferred terminal evidence against current authenticated membership and executable peer
/// bindings before exposing it to Mesh forwarding orchestration.
/// </summary>
/// <remarks>
/// Late evidence for an invalidated/reused peer handle, removed member, superseded incarnation, or unreachable current
/// binding is ignored. This coordinator does not retry, commit hop budget, infer end-to-end delivery, or alter membership.
/// </remarks>
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t BindingCapacity = Limits::MaxTopologyLinks
>
class ForwardingTerminalEvidenceCoordinator final : public Radio::ILogicalTransferTerminalObserver {
    const AuthenticatedMembershipTable<MembershipCapacity>& _memberships;
    const AuthenticatedDirectPeerBindingTable<BindingCapacity>& _bindings;
    IForwardingTerminalEvidenceObserver& _observer;

public:
    ForwardingTerminalEvidenceCoordinator(
        const AuthenticatedMembershipTable<MembershipCapacity>& memberships,
        const AuthenticatedDirectPeerBindingTable<BindingCapacity>& bindings,
        IForwardingTerminalEvidenceObserver& observer
    ) noexcept : _memberships(memberships), _bindings(bindings), _observer(observer) {}

    void OnLogicalTransferTerminal(const Radio::LogicalTransferTerminalEvidence& terminal) override {
        if (!terminal.Transfer || !terminal.Descriptor.IsValid() || !terminal.Descriptor.Peer ||
            !terminal.Evidence.IsTerminal()) return;

        const auto* binding = _bindings.ResolvePeer(terminal.Descriptor.Peer);
        if (binding == nullptr) return;

        const auto* membership = _memberships.FindExact(binding->Neighbour, binding->Incarnation);
        if (membership == nullptr || !membership->IsValid() ||
            membership->Reachability == ReachabilityState::Unreachable) return;

        ForwardingTerminalDisposition disposition = ForwardingTerminalDisposition::TransmissionFailed;
        if (!terminal.Evidence.TransmissionFailed()) {
            disposition = terminal.Evidence.PeerAcknowledged()
                ? ForwardingTerminalDisposition::PeerAcknowledged
                : ForwardingTerminalDisposition::TransmissionCompleted;
        }

        _observer.OnForwardingTerminalEvidence({
            binding->Neighbour,
            binding->Incarnation,
            binding->LocalRadio,
            binding->Peer,
            terminal.Transfer,
            terminal.Descriptor.TransferId,
            disposition
        });
    }
};

} // namespace ESPressio::Mesh
