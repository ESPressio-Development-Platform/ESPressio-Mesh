#include <cassert>

#include <ESPressio_ForwardingAttemptLifecycle.hpp>
#include <ESPressio_ForwardingRadioTerminalCorrelation.hpp>

using namespace ESPressio::Mesh;
namespace Radio = ESPressio::Radio;

int main() {
    ForwardingRadioTerminalCorrelation<2> correlation;

    const Radio::DeferredLogicalTransferHandle deferredA{0, 7};
    const Radio::DeferredLogicalTransferHandle deferredB{1, 9};
    const auto a = correlation.Reserve();
    const auto b = correlation.Reserve();
    assert(a && b && a != b);
    assert(correlation.Size() == 2U);
    assert(!correlation.Reserve());
    assert(correlation.Bind(a, deferredA));
    assert(correlation.Bind(b, deferredB));
    assert(!correlation.Bind(a, {2, 1}));

    Radio::LogicalTransferTerminalEvidence unrelated;
    unrelated.Transfer = {5, 5};
    unrelated.Evidence = Radio::RadioDirectLinkEvidence::Failed();
    correlation.OnLogicalTransferTerminal(unrelated);

    ForwardingRadioTerminalObservation observation;
    assert(!correlation.TryTake(a, observation));

    Radio::LogicalTransferTerminalEvidence completed;
    completed.Transfer = deferredA;
    completed.Evidence = Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged();
    correlation.OnLogicalTransferTerminal(completed);
    assert(correlation.TryTake(a, observation));
    assert(observation.Terminal.Transfer == deferredA);
    assert(observation.Terminal.Evidence.TransmissionCompleted());
    assert(observation.Terminal.Evidence.PeerAcknowledged());
    assert(!correlation.Contains(a));

    DefaultRouteAttemptPolicy routePolicy;
    DefaultRetryPolicy retryPolicy;
    RouteAttemptCoordinator attempts(routePolicy, retryPolicy);
    assert(attempts.BeginDistinctRouteAttempt(10, 100));
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(observation.Terminal, attempts, 20, 100) == ForwardingAttemptAction::AwaitingNextHopAcceptance);

    Radio::LogicalTransferTerminalEvidence failed;
    failed.Transfer = deferredB;
    failed.Evidence = Radio::RadioDirectLinkEvidence::Failed();
    correlation.OnLogicalTransferTerminal(failed);
    assert(correlation.TryTake(b, observation));
    assert(observation.Terminal.Evidence.TransmissionFailed());
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(observation.Terminal, attempts, 20, 100) == ForwardingAttemptAction::RetryCurrentRoute);

    const auto old = correlation.Reserve();
    assert(old); assert(correlation.Bind(old, {3, 11})); assert(correlation.Release(old));
    const auto replacement = correlation.Reserve();
    assert(replacement && replacement.Slot == old.Slot && replacement.Generation != old.Generation);
    assert(correlation.Bind(replacement, {4, 12}));

    Radio::LogicalTransferTerminalEvidence stale;
    stale.Transfer = {3, 11}; stale.Evidence = Radio::RadioDirectLinkEvidence::Failed();
    correlation.OnLogicalTransferTerminal(stale);
    assert(!correlation.TryTake(replacement, observation));

    Radio::LogicalTransferTerminalEvidence replacementTerminal;
    replacementTerminal.Transfer = {4, 12};
    replacementTerminal.Evidence = Radio::RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement();
    correlation.OnLogicalTransferTerminal(replacementTerminal);
    assert(correlation.TryTake(replacement, observation));
    assert(observation.Terminal.Evidence.TransmissionCompleted());

    const auto duplicateA = correlation.Reserve();
    const auto duplicateB = correlation.Reserve();
    assert(duplicateA && duplicateB);
    assert(correlation.Bind(duplicateA, {8, 8}));
    assert(!correlation.Bind(duplicateB, {8, 8}));
    assert(correlation.Release(duplicateA));
    assert(correlation.Release(duplicateB));
    return 0;
}
