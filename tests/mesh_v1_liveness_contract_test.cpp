#include <type_traits>

#include "ESPressio_MeshV1LivenessCoordinator.hpp"

using namespace ESPressio::Mesh;

static_assert(MeshV1LivenessFrameCodec::PacketBytes == 138U);
static_assert(std::is_base_of_v<ILivenessProbeInitiator, MeshV1LivenessCoordinator<>>);
static_assert(IsMeshRelayServiceClass(MeshRelayServiceClass::Infrastructure));
static_assert(IsMeshRelayServiceClass(MeshRelayServiceClass::BestEffort));

int main() {
    FixedControlWorkLifetimePolicy lifetimes{1000, 250, 500, 750, 1500, 2000};
    return lifetimes.IsValid() ? 0 : 1;
}
