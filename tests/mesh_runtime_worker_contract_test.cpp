#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <ESPressio_MeshRuntimeWorker.hpp>

using namespace ESPressio;

static_assert(
    std::is_base_of<Threads::Thread, Mesh::MeshRuntimeWorker>::value,
    "Mesh runtime must use exactly one generic Thread root for serialized execution"
);

static_assert(
    std::is_base_of<Mesh::IMeshRuntimeWorkSignal, Mesh::MeshRuntimeWorker>::value,
    "Mesh runtime must remain directly signalable by bounded ingress producers"
);

using MeshServiceThunk = Mesh::MeshRuntimeServiceResult (*)(void*, std::size_t, std::uint64_t) noexcept;
static_assert(
    std::is_same<decltype(Mesh::MeshRuntimeWorkerService::Service), MeshServiceThunk>::value,
    "Mesh runtime service must remain a fixed noexcept owner/thunk contract"
);

static_assert(
    std::is_same<decltype(Mesh::MeshRuntimeWorkerService::Owner), void*>::value,
    "Mesh runtime service must retain fixed owner identity without heap-backed callback state"
);

int main() {
    const Mesh::MeshRuntimeWorkerConfiguration defaults{};
    if (defaults.WorkQuantum != 8U) return 1;
    if (defaults.Priority != 3U) return 2;
    if (defaults.Core != -1) return 3;
    if (defaults.StackSize != 8192U) return 4;
    if (defaults.Name == nullptr) return 5;

    const Mesh::MeshRuntimeServiceResult drained{4U, false, 0U};
    const Mesh::MeshRuntimeServiceResult continuing{8U, true, 2500000U};
    if (drained.WorkItemsProcessed != 4U || drained.WorkRemaining || drained.NextDeadlineNanoseconds != 0U) return 6;
    if (continuing.WorkItemsProcessed != 8U || !continuing.WorkRemaining ||
        continuing.NextDeadlineNanoseconds != 2500000U) return 7;
    return 0;
}
