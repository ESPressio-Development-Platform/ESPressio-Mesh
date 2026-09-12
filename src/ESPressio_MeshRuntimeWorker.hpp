#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <ESPressio_Thread.hpp>

namespace ESPressio::Mesh {

/// <summary>Result of one bounded serialized Mesh runtime service quantum.</summary>
/// <remarks>
/// NextDeadlineNanoseconds is an absolute local monotonic deadline; zero means no time-driven work is currently retained.
/// WorkRemaining requests an immediate continuation through the common Thread work latch. No polling interval exists.
/// </remarks>
struct MeshRuntimeServiceResult final {
    std::size_t WorkItemsProcessed{0U};
    bool WorkRemaining{false};
    std::uint64_t NextDeadlineNanoseconds{0U};
};

/// <summary>Receives a non-blocking signal that serialized Mesh runtime work is available.</summary>
class IMeshRuntimeWorkSignal {
public:
    virtual ~IMeshRuntimeWorkSignal()=default;
    virtual void OnMeshRuntimeWorkAvailable() noexcept=0;
};

/// <summary>Fixed owner/member-style hot-path hook; never a std::function or heap-backed callback graph.</summary>
struct MeshRuntimeWorkerService final {
    void* Owner{nullptr};
    MeshRuntimeServiceResult (*Service)(void*,std::size_t,std::uint64_t) noexcept{nullptr};

    constexpr bool IsValid() const noexcept { return Owner!=nullptr&&Service!=nullptr; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    MeshRuntimeServiceResult Run(std::size_t quantum,std::uint64_t now) const noexcept {
        return IsValid()?Service(Owner,quantum,now):MeshRuntimeServiceResult{};
    }
};

/// <summary>Execution configuration for the one serialized Mesh runtime Thread.</summary>
struct MeshRuntimeWorkerConfiguration final {
    std::size_t WorkQuantum{8U};
    unsigned int Priority{3U};
    int Core{-1};
    std::uint32_t StackSize{8192U};
    const char* Name{"espressioMesh"};
};

/// <summary>Cumulative bounded-worker diagnostics.</summary>
struct MeshRuntimeWorkerStatistics final {
    std::uint64_t WorkSignals{0U};
    std::uint64_t ServicePasses{0U};
    std::uint64_t ContinuationRequests{0U};
    std::uint64_t WorkItemsProcessed{0U};
    std::uint64_t TotalServiceDurationNanoseconds{0U};
    std::uint64_t MaximumServiceDurationNanoseconds{0U};
    std::uint64_t PublishedDeadlines{0U};
};

/// <summary>
/// One concrete Mesh-owned serialized runtime executor built directly on the generic Threads::Thread root.
/// </summary>
/// <remarks>
/// The worker owns no queue and no reusable capability flavor. A composition supplies one fixed noexcept service thunk
/// which drains at most WorkQuantum logical items, performs due lifecycle work and returns its next real monotonic deadline.
/// Async producers publish their domain state before OnMeshRuntimeWorkAvailable(), which requests one application quantum
/// through Thread's common coalescing wake. Immediate continuation is explicit; otherwise the root waits until the exact
/// returned deadline or another wake. There is no PrecisionThread, std::function, exception-based Mesh control flow or
/// fixed-period ingress/maintenance fallback.
/// </remarks>
class MeshRuntimeWorker final : public Threads::Thread, public IMeshRuntimeWorkSignal {
    MeshRuntimeWorkerConfiguration _configuration{};
    MeshRuntimeWorkerService _service{};
    std::atomic<std::uint64_t> _nextDeadlineNanoseconds{0U};
    std::atomic<std::uint64_t> _workSignals{0U};
    std::atomic<std::uint64_t> _servicePasses{0U};
    std::atomic<std::uint64_t> _continuationRequests{0U};
    std::atomic<std::uint64_t> _workItemsProcessed{0U};
    std::atomic<std::uint64_t> _totalServiceDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumServiceDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _publishedDeadlines{0U};

    static Threads::ThreadConfiguration ToThreadConfiguration(const MeshRuntimeWorkerConfiguration& configuration) noexcept {
        Threads::ThreadConfiguration result{};
        result.Execution.Name=configuration.Name;
        result.Execution.StackSize=configuration.StackSize;
        result.Execution.Priority=configuration.Priority;
        result.Execution.Core=configuration.Core;
        result.ApplicationStackFloorBytes=Threads::Thread::RootFrameworkStackFloorBytes;
        return result;
    }

    static void UpdateMaximum(std::atomic<std::uint64_t>& target,std::uint64_t value) noexcept {
        auto current=target.load(std::memory_order_relaxed);
        while(value>current&&!target.compare_exchange_weak(
            current,value,std::memory_order_relaxed,std::memory_order_relaxed)) {}
    }

protected:
    Threads::ThreadWorkDisposition OnLoop() override {
        if(!_service||_configuration.WorkQuantum==0U) {
            _nextDeadlineNanoseconds.store(0U,std::memory_order_release);
            return Threads::ThreadWorkDisposition::IdleReady;
        }
        const auto started=Now();
        const auto result=_service.Run(_configuration.WorkQuantum,started);
        const auto completed=Now();
        _servicePasses.fetch_add(1U,std::memory_order_relaxed);
        _workItemsProcessed.fetch_add(result.WorkItemsProcessed,std::memory_order_relaxed);
        if(completed>=started) {
            const auto duration=completed-started;
            _totalServiceDurationNanoseconds.fetch_add(duration,std::memory_order_relaxed);
            UpdateMaximum(_maximumServiceDurationNanoseconds,duration);
        }
        _nextDeadlineNanoseconds.store(result.NextDeadlineNanoseconds,std::memory_order_release);
        if(result.NextDeadlineNanoseconds!=0U) _publishedDeadlines.fetch_add(1U,std::memory_order_relaxed);
        if(result.WorkRemaining) {
            _continuationRequests.fetch_add(1U,std::memory_order_relaxed);
            return Threads::ThreadWorkDisposition::ImmediateWorkRemaining;
        }
        return Threads::ThreadWorkDisposition::IdleReady;
    }

    Threads::ThreadHostResult Host(Threads::ThreadHostOperation operation,Threads::ThreadCycleContext& context) override {
        Threads::ThreadHostResult result{};
        if(operation==Threads::ThreadHostOperation::Inspect) {
            const auto deadline=_nextDeadlineNanoseconds.load(std::memory_order_acquire);
            result.Ready.ApplicationEligible=true;
            if(deadline!=0U) {
                result.Ready.Deadline={true,deadline};
                result.Ready.ApplicationDeadlineDue=deadline<=context.Now;
            }
        } else if(operation==Threads::ThreadHostOperation::Resources) {
            result.Resources.ResidentBytes=sizeof(MeshRuntimeWorker);
            result.Resources.ResidentAlignment=alignof(MeshRuntimeWorker);
            result.Resources.FrameworkStackFloorBytes=Threads::Thread::RootFrameworkStackFloorBytes;
            result.Resources.ExecutionContexts=1U;
            result.Resources.CommonWorkSignals=1U;
            result.NeedsMonotonicTime=true;
        } else if(operation==Threads::ThreadHostOperation::Quiesce) {
            _nextDeadlineNanoseconds.store(0U,std::memory_order_release);
        }
        return result;
    }

public:
    explicit MeshRuntimeWorker(MeshRuntimeWorkerService service,MeshRuntimeWorkerConfiguration configuration={}) noexcept
        :Threads::Thread(ToThreadConfiguration(configuration)),_configuration(configuration),_service(service) {}

    MeshRuntimeWorker(const MeshRuntimeWorker&)=delete;
    MeshRuntimeWorker& operator=(const MeshRuntimeWorker&)=delete;

    const MeshRuntimeWorkerConfiguration& Configuration() const noexcept { return _configuration; }
    bool IsConfigured() const noexcept { return static_cast<bool>(_service)&&_configuration.WorkQuantum!=0U&&
        _configuration.StackSize!=0U&&_configuration.Name!=nullptr; }

    void OnMeshRuntimeWorkAvailable() noexcept override {
        _workSignals.fetch_add(1U,std::memory_order_relaxed);
        RequestLoop();
    }

    std::uint64_t NextServiceDeadlineNanoseconds() const noexcept {
        return _nextDeadlineNanoseconds.load(std::memory_order_acquire);
    }

    MeshRuntimeWorkerStatistics GetStatistics() const noexcept {
        return {
            _workSignals.load(std::memory_order_relaxed),
            _servicePasses.load(std::memory_order_relaxed),
            _continuationRequests.load(std::memory_order_relaxed),
            _workItemsProcessed.load(std::memory_order_relaxed),
            _totalServiceDurationNanoseconds.load(std::memory_order_relaxed),
            _maximumServiceDurationNanoseconds.load(std::memory_order_relaxed),
            _publishedDeadlines.load(std::memory_order_relaxed)
        };
    }
};

} // namespace ESPressio::Mesh
