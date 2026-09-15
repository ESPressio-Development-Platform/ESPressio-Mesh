#include <cassert>
#include <cstdint>
#include <limits>

#include "ESPressio_ControlWorkLifetimePolicy.hpp"

using namespace ESPressio::Mesh;

int main() {
    FixedControlWorkLifetimePolicy policy{1000, 250, 500, 750, 1500, 2000};
    assert(policy.IsValid());
    assert(policy.LifetimeMilliseconds(MeshRelayServiceClass::Infrastructure) == 1000);
    assert(policy.LifetimeMilliseconds(MeshRelayServiceClass::Clock) == 250);
    assert(policy.LifetimeMilliseconds(MeshRelayServiceClass::Critical) == 500);
    assert(policy.LifetimeMilliseconds(MeshRelayServiceClass::Responsive) == 750);
    assert(policy.LifetimeMilliseconds(MeshRelayServiceClass::Convergent) == 1500);
    assert(policy.LifetimeMilliseconds(MeshRelayServiceClass::BestEffort) == 2000);

    std::uint64_t deadline = 0;
    assert(TryControlWorkDeadline(policy, MeshRelayServiceClass::Infrastructure, 5000, deadline));
    assert(deadline == 6000);
    assert(TryControlWorkDeadline(policy, MeshRelayServiceClass::Clock, 5000, deadline));
    assert(deadline == 5250);
    assert(TryControlWorkDeadline(policy, MeshRelayServiceClass::BestEffort, 5000, deadline));
    assert(deadline == 7000);

    std::uint64_t deadlineNanoseconds = 0;
    assert(TryControlWorkDeadlineNanoseconds(
        policy, MeshRelayServiceClass::Responsive, 5000, deadlineNanoseconds));
    assert(deadlineNanoseconds == 5750ULL * MeshNanosecondsPerMillisecond);

    FixedControlWorkLifetimePolicy invalid{1000, 0, 500, 750, 1500, 2000};
    assert(!invalid.IsValid());
    assert(!TryControlWorkDeadline(invalid, MeshRelayServiceClass::Clock, 5000, deadline));

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    assert(TryControlWorkDeadline(policy, MeshRelayServiceClass::BestEffort, maximum - 10, deadline));
    assert(deadline == maximum);
    assert(TryControlWorkDeadlineNanoseconds(
        policy, MeshRelayServiceClass::BestEffort, maximum - 10, deadlineNanoseconds));
    assert(deadlineNanoseconds == maximum);

    assert(!TryControlWorkDeadline(policy, MeshRelayServiceClass::Infrastructure, 0, deadline));
    assert(deadline == 0);

    return 0;
}
