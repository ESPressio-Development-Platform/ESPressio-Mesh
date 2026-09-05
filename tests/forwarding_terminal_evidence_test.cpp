#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_ForwardingTerminalEvidence.hpp>

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return System::DeviceIdentifier{bytes};
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return Mesh::MembershipIncarnation{bytes};
}

struct Observer final : Mesh::IForwardingTerminalEvidenceObserver {
    std::size_t Count{0};
    Mesh::ForwardingTerminalEvidence Last{};

    void OnForwardingTerminalEvidence(const Mesh::ForwardingTerminalEvidence& evidence) override {
        ++Count;
        Last = evidence;
    }
};

Mesh::ForwardingSubmissionResult DeferredSubmission(
    Radio::DeferredLogicalTransferHandle transfer,
    const System::DeviceIdentifier& nextHop,
    const Mesh::MembershipIncarnation& incarnation
) {
    Mesh::ForwardingSubmissionResult result;
    result.Disposition = Mesh::ForwardingSubmissionDisposition::Accepted;
    result.RadioResult.Status = Radio::RadioTransportSendStatus::Accepted;
    result.RadioResult.LinkResult = Radio::RadioSendResult::Accepted();
    result.RadioResult.DeferredTransfer = transfer;
    result.NextHop = nextHop;
    result.NextHopIncarnation = incarnation;
    return result;
}
}

int main() {
    const auto node = Device(4);
    const auto incarnation = Incarnation(7);

    // Immediate evidence classification never overstates Mesh delivery.
    auto immediate = DeferredSubmission({1, 1}, node, incarnation);
    assert(Mesh::ClassifyForwardingSubmissionEvidence(immediate) ==
           Mesh::ForwardingSubmissionEvidenceState::AwaitingDeferredEvidence);
    immediate.RadioResult.DeferredTransfer = {};
    assert(Mesh::ClassifyForwardingSubmissionEvidence(immediate) ==
           Mesh::ForwardingSubmissionEvidenceState::Unobservable);
    immediate.RadioResult.LinkResult = Radio::RadioSendResult::Accepted(
        Radio::RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement()
    );
    assert(Mesh::ClassifyForwardingSubmissionEvidence(immediate) ==
           Mesh::ForwardingSubmissionEvidenceState::TransmissionCompleted);
    immediate.RadioResult.LinkResult = Radio::RadioSendResult::Accepted(
        Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged()
    );
    assert(Mesh::ClassifyForwardingSubmissionEvidence(immediate) ==
           Mesh::ForwardingSubmissionEvidenceState::PeerAcknowledged);
    immediate.Disposition = Mesh::ForwardingSubmissionDisposition::RetryableFailure;
    assert(Mesh::ClassifyForwardingSubmissionEvidence(immediate) ==
           Mesh::ForwardingSubmissionEvidenceState::NotAccepted);

    Observer observer;
    Mesh::ForwardingTerminalEvidenceCoordinator<2> coordinator{&observer};

    // Normal order: Mesh records authenticated context, then deferred Radio terminal evidence arrives.
    const Radio::DeferredLogicalTransferHandle firstTransfer{2, 3};
    const auto firstSubmission = DeferredSubmission(firstTransfer, node, incarnation);
    const auto firstHandle = coordinator.Track(firstSubmission);
    assert(firstHandle);
    assert(coordinator.Size() == 1U);

    Radio::LogicalTransferTerminalEvidence firstTerminal;
    firstTerminal.Transfer = firstTransfer;
    firstTerminal.Evidence = Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged();
    coordinator.OnLogicalTransferTerminal(firstTerminal);
    assert(observer.Count == 1U);
    assert(observer.Last.Handle == firstHandle);
    assert(observer.Last.NextHop == node);
    assert(observer.Last.NextHopIncarnation == incarnation);
    assert(observer.Last.RadioTransfer == firstTransfer);
    assert(observer.Last.Kind == Mesh::ForwardingTerminalEvidenceKind::PeerAcknowledged);
    assert(coordinator.Size() == 0U);

    // Race order: terminal evidence can arrive immediately after Send returns, before Track attaches Mesh context.
    const Radio::DeferredLogicalTransferHandle racedTransfer{5, 9};
    Radio::LogicalTransferTerminalEvidence racedTerminal;
    racedTerminal.Transfer = racedTransfer;
    racedTerminal.Evidence = Radio::RadioDirectLinkEvidence::Failed();
    coordinator.OnLogicalTransferTerminal(racedTerminal);
    assert(observer.Count == 1U);
    assert(coordinator.Size() == 1U);

    const auto racedSubmission = DeferredSubmission(racedTransfer, Device(8), Incarnation(8));
    const auto racedHandle = coordinator.Track(racedSubmission);
    assert(racedHandle);
    assert(observer.Count == 2U);
    assert(observer.Last.Handle == racedHandle);
    assert(observer.Last.NextHop == Device(8));
    assert(observer.Last.NextHopIncarnation == Incarnation(8));
    assert(observer.Last.Kind == Mesh::ForwardingTerminalEvidenceKind::TransmissionFailed);
    assert(coordinator.Size() == 0U);

    // Explicit release invalidates only the exact generation-safe local correlation.
    const auto pending = coordinator.Track(DeferredSubmission({10, 1}, node, incarnation));
    assert(pending);
    assert(coordinator.Release(pending));
    assert(!coordinator.Release(pending));
    assert(coordinator.Size() == 0U);

    // Capacity is hard and explicit; no hidden allocation or eviction occurs.
    const auto a = coordinator.Track(DeferredSubmission({11, 1}, node, incarnation));
    const auto b = coordinator.Track(DeferredSubmission({12, 1}, Device(5), Incarnation(5)));
    const auto c = coordinator.Track(DeferredSubmission({13, 1}, Device(6), Incarnation(6)));
    assert(a && b);
    assert(!c);
    assert(coordinator.Size() == 2U);

    return 0;
}
