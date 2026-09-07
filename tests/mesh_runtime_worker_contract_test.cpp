#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <ESPressio_MeshRuntimeWorker.hpp>

using namespace ESPressio;

static_assert(
    std::is_base_of<
        Threads::PrecisionThread<
            Units::NanoSeconds<std::uint64_t>,
            Threads::PrecisionThreadTraits<Units::NanoSeconds<std::uint64_t>>
        >,
        Mesh::MeshRuntimeWorker
    >::value,
    "Mesh runtime must retain PrecisionThread monotonic scheduling ownership"
);

static_assert(
    std::is_base_of<Mesh::IMeshRuntimeWorkSignal, Mesh::MeshRuntimeWorker>::value,
    "Mesh runtime must remain directly signalable by bounded ingress producers"
);

static_assert(
    std::is_same<
        Mesh::MeshRuntimeWorker::IngressHandler,
        std::function<Mesh::MeshRuntimeServiceResult(std::size_t)>
    >::value,
    "Mesh ingress handler must expose a bounded quantum contract"
);

static_assert(
    std::is_same<
        Mesh::MeshRuntimeWorker::MaintenanceHandler,
        std::function<void(std::uint64_t)>
    >::value,
    "Mesh maintenance handler must receive local monotonic milliseconds"
);

int main() {
    const Mesh::MeshRuntimeWorkerConfiguration defaults{};
    if (defaults.MaintenancePeriodMilliseconds != 10U) return 1;
    if (defaults.IngressQuantum != 8U) return 2;
    if (defaults.Priority != 3U) return 3;
    if (defaults.Core != -1) return 4;
    if (defaults.StackSize != 8192U) return 5;

    const Mesh::MeshRuntimeServiceResult drained{4U, false};
    const Mesh::MeshRuntimeServiceResult continuing{8U, true};
    if (drained.WorkItemsProcessed != 4U || drained.WorkRemaining) return 6;
    if (continuing.WorkItemsProcessed != 8U || !continuing.WorkRemaining) return 7;
    return 0;
}
