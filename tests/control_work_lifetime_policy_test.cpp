#include <cassert>
#include <cstdint>
#include <limits>

#include "ESPressio_ControlWorkLifetimePolicy.hpp"

using namespace ESPressio::Mesh;

int main() {
    FixedControlWorkLifetimePolicy policy{1000, 250, 2000};
    assert(policy.IsValid());
    assert(policy.LifetimeMilliseconds(MeshTrafficClass::InfrastructureResponse) == 1000);
    assert(policy.LifetimeMilliseconds(MeshTrafficClass::ClockControl) == 250);
    assert(policy.LifetimeMilliseconds(MeshTrafficClass::GeneralControl) == 2000);
    assert(policy.LifetimeMilliseconds(MeshTrafficClass::Application) == 0);

    std::uint64_t deadline = 0;
    assert(TryControlWorkDeadline(policy, MeshTrafficClass::InfrastructureResponse, 5000, deadline));
    assert(deadline == 6000);
    assert(TryControlWorkDeadline(policy, MeshTrafficClass::ClockControl, 5000, deadline));
    assert(deadline == 5250);
    assert(TryControlWorkDeadline(policy, MeshTrafficClass::GeneralControl, 5000, deadline));
    assert(deadline == 7000);

    // Application deliveries use their own immutable deadline rather than control-work lifetime policy.
    assert(!TryControlWorkDeadline(policy, MeshTrafficClass::Application, 5000, deadline));
    assert(deadline == 0);

    FixedControlWorkLifetimePolicy invalid{1000, 0, 2000};
    assert(!invalid.IsValid());
    assert(!TryControlWorkDeadline(invalid, MeshTrafficClass::ClockControl, 5000, deadline));

    // Absolute deadline computation saturates instead of wrapping monotonic time.
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    assert(TryControlWorkDeadline(policy, MeshTrafficClass::GeneralControl, maximum - 10, deadline));
    assert(deadline == maximum);

    return 0;
}
