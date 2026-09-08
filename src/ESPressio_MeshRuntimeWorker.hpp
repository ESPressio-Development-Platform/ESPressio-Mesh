#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
#include <ESPressio_SystemPlatformClock.hpp>
#include <ESPressio_Time.hpp>

namespace ESPressio::Mesh {

/// <summary>Result returned by one bounded Mesh ingress service pass.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshRuntimeServiceResult final {
    std::size_t WorkItemsProcessed{0U};
    bool WorkRemaining{false};
};

/// <summary>Receives a non-blocking signal that serialized Mesh runtime work is available.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic interface/object includes vptr storage where not supplied by a base.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class IMeshRuntimeWorkSignal {
public:
    virtual ~IMeshRuntimeWorkSignal() = default;
    virtual void OnMeshRuntimeWorkAvailable() noexcept = 0;
};

/// <summary>Runtime scheduling policy for one serialized Mesh execution context.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshRuntimeWorkerConfiguration final {
    /// <summary>Maximum idle interval between monotonic Mesh maintenance passes.</summary>
    std::uint32_t MaintenancePeriodMilliseconds{10U};

    /// <summary>Maximum ingress records admitted to one cooperative service pass.</summary>
    std::size_t IngressQuantum{8U};

    /// <summary>Task priority. The default sits above ordinary Radio data-plane work and below critical Radio control.</summary>
    unsigned int Priority{3U};

    /// <summary>Requested processor affinity, or a negative value for no fixed affinity.</summary>
    int Core{-1};

    /// <summary>Task stack size in bytes.</summary>
    std::uint32_t StackSize{8192U};
};

/// <summary>Cumulative execution diagnostics for MeshRuntimeWorker.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct MeshRuntimeWorkerStatistics final {
    std::uint64_t WorkSignals{0U};
    std::uint64_t IngressPasses{0U};
    std::uint64_t MaintenancePasses{0U};
    std::uint64_t ContinuationWakes{0U};
    std::uint64_t WorkItemsProcessed{0U};
    std::uint64_t IngressFailures{0U};
    std::uint64_t MaintenanceFailures{0U};
    std::uint64_t TotalIngressDurationNanoseconds{0U};
    std::uint64_t MaximumIngressDurationNanoseconds{0U};
    std::uint64_t TotalMaintenanceDurationNanoseconds{0U};
    std::uint64_t MaximumMaintenanceDurationNanoseconds{0U};
};

/// <summary>
/// Owns the serialized runtime execution context for Mesh ingress and monotonic lifecycle maintenance.
/// </summary>
/// <remarks>
/// Mesh coordinators deliberately remain synchronous and independently testable. This worker provides the execution
/// ownership needed by a composed runtime without making those coordinators internally concurrent. Latency-sensitive
/// ingress and periodic maintenance therefore share one task and cannot race membership, session, routing, protection,
/// or delivery state.
///
/// Async producers call <see cref="OnMeshRuntimeWorkAvailable"/>. One wake services at most IngressQuantum records and
/// queues another independent work wake when records remain. The periodic PrecisionThread schedule is not moved by an
/// ingress wake. In addition, every work wake checks the monotonic maintenance deadline so sustained ingress cannot starve
/// lifecycle work merely by continuously requesting asynchronous service.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: sizeof(Threads::PrecisionThread<Units::NanoSeconds<std::uint64_t>, Threads::PrecisionThreadTraits<Units::NanoSeconds<std::uint64_t>>>) [0 bytes dynamic allocation]
 * Requires Stack/Heap Preallocation
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 4 bytes known bases + sizeof(Threads::PrecisionThread<Units::NanoSeconds<std::uint64_t>, Threads::PrecisionThreadTraits<Units::NanoSeconds<std::uint64_t>>>) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class MeshRuntimeWorker final
    : public Threads::PrecisionThread<
          Units::NanoSeconds<std::uint64_t>,
          Threads::PrecisionThreadTraits<Units::NanoSeconds<std::uint64_t>>
      >,
      public IMeshRuntimeWorkSignal {
public:
    using Time = Units::NanoSeconds<std::uint64_t>;
    using Base = Threads::PrecisionThread<Time, Threads::PrecisionThreadTraits<Time>>;
    using IngressHandler = std::function<MeshRuntimeServiceResult(std::size_t)>;
    using MaintenanceHandler = std::function<void(std::uint64_t)>;

private:
    MeshRuntimeWorkerConfiguration _configuration{};
    IngressHandler _ingressHandler{};
    MaintenanceHandler _maintenanceHandler{};
    std::atomic<std::uint64_t> _nextMaintenanceNanoseconds{0U};

    std::atomic<std::uint64_t> _workSignals{0U};
    std::atomic<std::uint64_t> _ingressPasses{0U};
    std::atomic<std::uint64_t> _maintenancePasses{0U};
    std::atomic<std::uint64_t> _continuationWakes{0U};
    std::atomic<std::uint64_t> _workItemsProcessed{0U};
    std::atomic<std::uint64_t> _ingressFailures{0U};
    std::atomic<std::uint64_t> _maintenanceFailures{0U};
    std::atomic<std::uint64_t> _totalIngressDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumIngressDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _totalMaintenanceDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumMaintenanceDurationNanoseconds{0U};

    static void UpdateMaximum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept {
        auto current = target.load(std::memory_order_relaxed);
        while (value > current &&
               !target.compare_exchange_weak(
                   current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    std::uint64_t MaintenancePeriodNanoseconds() const noexcept {
        return static_cast<std::uint64_t>(_configuration.MaintenancePeriodMilliseconds) *
            Timing::NanosecondsPerMillisecond;
    }

    void RunMaintenanceIfDue(std::uint64_t nowNanoseconds) noexcept {
        if (!_maintenanceHandler) return;
        const auto period = MaintenancePeriodNanoseconds();
        if (period == 0U) return;

        const auto due = _nextMaintenanceNanoseconds.load(std::memory_order_acquire);
        if (due != 0U && nowNanoseconds < due) return;

        // Only this worker task executes this method, so publication is diagnostic/lifecycle state rather than a lock.
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        _nextMaintenanceNanoseconds.store(
            nowNanoseconds > maximum - period ? maximum : nowNanoseconds + period,
            std::memory_order_release);

        const auto started = System::Clock::Monotonic().NowNanoseconds();
        try {
            _maintenanceHandler(nowNanoseconds / Timing::NanosecondsPerMillisecond);
        } catch (...) {
            _maintenanceFailures.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const auto completed = System::Clock::Monotonic().NowNanoseconds();
        _maintenancePasses.fetch_add(1U, std::memory_order_relaxed);
        if (completed >= started) {
            const auto duration = completed - started;
            _totalMaintenanceDurationNanoseconds.fetch_add(duration, std::memory_order_relaxed);
            UpdateMaximum(_maximumMaintenanceDurationNanoseconds, duration);
        }
    }

    void ServiceIngressQuantum() noexcept {
        if (!_ingressHandler) return;
        _ingressPasses.fetch_add(1U, std::memory_order_relaxed);
        const auto started = System::Clock::Monotonic().NowNanoseconds();
        MeshRuntimeServiceResult result{};
        try {
            result = _ingressHandler(_configuration.IngressQuantum);
        } catch (...) {
            _ingressFailures.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const auto completed = System::Clock::Monotonic().NowNanoseconds();
        _workItemsProcessed.fetch_add(result.WorkItemsProcessed, std::memory_order_relaxed);
        if (completed >= started) {
            const auto duration = completed - started;
            _totalIngressDurationNanoseconds.fetch_add(duration, std::memory_order_relaxed);
            UpdateMaximum(_maximumIngressDurationNanoseconds, duration);
        }
        if (result.WorkRemaining) {
            _continuationWakes.fetch_add(1U, std::memory_order_relaxed);
            WakeForWork();
        }
    }

public:
    MeshRuntimeWorker(
        IngressHandler ingressHandler,
        MaintenanceHandler maintenanceHandler,
        MeshRuntimeWorkerConfiguration configuration = {}
    ) : _configuration(configuration),
        _ingressHandler(std::move(ingressHandler)),
        _maintenanceHandler(std::move(maintenanceHandler)) {
        SetStartOnInitialize(false);
        SetPriority(_configuration.Priority);
        SetCoreID(_configuration.Core);
        SetStackSize(_configuration.StackSize);
        const auto cadence = _configuration.MaintenancePeriodMilliseconds == 0U
            ? 1U
            : _configuration.MaintenancePeriodMilliseconds;
        SetIterationPeriod(Units::MilliSeconds<std::uint32_t>(cadence));
        SetDesiredIterationPeriod(Units::MilliSeconds<std::uint32_t>(cadence));
    }

    ~MeshRuntimeWorker() override { Shutdown(); }

    MeshRuntimeWorker(const MeshRuntimeWorker&) = delete;
    MeshRuntimeWorker& operator=(const MeshRuntimeWorker&) = delete;
    MeshRuntimeWorker(MeshRuntimeWorker&&) = delete;
    MeshRuntimeWorker& operator=(MeshRuntimeWorker&&) = delete;

    const MeshRuntimeWorkerConfiguration& Configuration() const noexcept { return _configuration; }

    void OnMeshRuntimeWorkAvailable() noexcept override {
        _workSignals.fetch_add(1U, std::memory_order_relaxed);
        try {
            WakeForWork();
        } catch (...) {
            // Producer callbacks must never observe a scheduler failure.
        }
    }

    MeshRuntimeWorkerStatistics GetStatistics() const noexcept {
        return {
            _workSignals.load(std::memory_order_relaxed),
            _ingressPasses.load(std::memory_order_relaxed),
            _maintenancePasses.load(std::memory_order_relaxed),
            _continuationWakes.load(std::memory_order_relaxed),
            _workItemsProcessed.load(std::memory_order_relaxed),
            _ingressFailures.load(std::memory_order_relaxed),
            _maintenanceFailures.load(std::memory_order_relaxed),
            _totalIngressDurationNanoseconds.load(std::memory_order_relaxed),
            _maximumIngressDurationNanoseconds.load(std::memory_order_relaxed),
            _totalMaintenanceDurationNanoseconds.load(std::memory_order_relaxed),
            _maximumMaintenanceDurationNanoseconds.load(std::memory_order_relaxed)
        };
    }

protected:
    void OnWorkWake() override {
        const auto now = System::Clock::Monotonic().NowNanoseconds();
        RunMaintenanceIfDue(now);
        ServiceIngressQuantum();
    }

    void Iterate(Time, Time, Threads::SkippedIterationCount) override {
        const auto now = System::Clock::Monotonic().NowNanoseconds();
        RunMaintenanceIfDue(now);
        // Periodic service is a fallback for producers that cannot publish an async work signal.
        ServiceIngressQuantum();
    }
};

} // namespace ESPressio::Mesh
