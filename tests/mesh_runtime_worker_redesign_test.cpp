#include <cassert>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshRuntimeWorker.hpp"

using namespace ESPressio;

namespace {
struct Owner final {
    std::size_t Calls{0};
    static Mesh::MeshRuntimeServiceResult Service(void* context,std::size_t quantum,std::uint64_t now) noexcept {
        auto& owner=*static_cast<Owner*>(context);
        ++owner.Calls;
        return {quantum,false,now+5'000'000ULL};
    }
};
}

int main(){
    Owner owner;
    Mesh::MeshRuntimeWorkerConfiguration config{};
    config.WorkQuantum=4;
    config.Priority=3;
    config.Core=-1;
    config.StackSize=8192;
    config.Name="meshRuntimeTest";
    Mesh::MeshRuntimeWorker worker({&owner,&Owner::Service},config);
    assert(worker.IsConfigured());
    assert(worker.Configuration().WorkQuantum==4);
    assert(worker.NextServiceDeadlineNanoseconds()==0);

    // Producer notification is safe before initialization: it records demand and the common Thread wake is simply absent.
    worker.OnMeshRuntimeWorkAvailable();
    const auto stats=worker.GetStatistics();
    assert(stats.WorkSignals==1);
    assert(stats.ServicePasses==0);

    const auto resources=worker.GetResourceProfile();
    assert(resources.ResidentBytes==sizeof(Mesh::MeshRuntimeWorker));
    assert(resources.ResidentAlignment==alignof(Mesh::MeshRuntimeWorker));
    assert(resources.ExecutionContexts==1);
    assert(resources.CommonWorkSignals==1);
    assert(resources.FrameworkStackFloorBytes>=Threads::Thread::RootFrameworkStackFloorBytes);
    assert(resources.ConfiguredStackBytes==8192);
    return 0;
}
