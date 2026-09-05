#include <cassert>

#include <ESPressio_ForwardingAttemptLifecycle.hpp>
#include <ESPressio_ForwardingRadioTerminalCorrelation.hpp>

using namespace ESPressio::Mesh;

int main() {
    ForwardingRadioTerminalCorrelation<2> correlation;

    const Radio::DeferredLogicalTransferHandle deferredA{0, 7};
    const Radio::DeferredLogicalTransferHandle deferredB{1, 9};
    const auto a = correlation.Register(deferredA);
    const auto b = correlation.Register(deferredB);
    assert(a && b && a != b);
    assert(correlation.Size() == 2U);
    assert(!correlation.Register({2, 1}));
    assert(!correlation.Register(deferredA));

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
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(
        observation.Terminal, attempts, 20, 100
    ) == ForwardingAttemptAction::AwaitingNextHopAcceptance);

    Radio::LogicalTransferTerminalEvidence failed;
    failed.Transfer = deferredB;
    failed.Evidence = Radio::RadioDirectLinkEvidence::Failed();
    correlation.OnLogicalTransferTerminal(failed);
    assert(correlation.TryTake(b, observation));
    assert(observation.Terminal.Evidence.TransmissionFailed());
    assert(ForwardingAttemptLifecycle::AfterRadioTerminalEvidence(
        observation.Terminal, attempts, 20, 100
    ) == ForwardingAttemptAction::RetryCurrentRoute);

    const auto old = correlation.Register({3, 11});
    assert(old);
    assert(correlation.Release(old));
    const auto replacement = correlation.Register({4, 12});
    assert(replacement);
    assert(replacement.Slot == old.Slot);
    assert(replacement.Generation != old.Generation);

    Radio::LogicalTransferTerminalEvidence stale;
    stale.Transfer = {3, 11};
    stale.Evidence = Radio::RadioDirectLinkEvidence::Failed();
    correlation.OnLogicalTransferTerminal(stale);
    assert(!correlation.TryTake(replacement, observation));

    Radio::LogicalTransferTerminalEvidence replacementTerminal;
    replacementTerminal.Transfer = {4, 12};
    replacementTerminal.Evidence = Radio::RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement();
    correlation.OnLogicalTransferTerminal(replacementTerminal);
    assert(correlation.TryTake(replacement, observation));
    assert(observation.Terminal.Evidence.TransmissionCompleted());

    return 0;
}
