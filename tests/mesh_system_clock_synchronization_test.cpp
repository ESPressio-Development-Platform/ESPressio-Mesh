#include <cassert>
#include <cstdint>

#include <ESPressio_MeshSystemClockSynchronization.hpp>

using namespace ESPressio;
using namespace ESPressio::Mesh;

namespace {

System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

MembershipIncarnation Incarnation(std::uint8_t tail) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = tail;
    return MembershipIncarnation{bytes};
}

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Now (Timing::ClockTick): 8 bytes [0 bytes dynamic allocation]
 * - Status (Timing::ClockSynchronizationStatus<Timing::ClockTick>): 72 bytes [0 bytes dynamic allocation]
 * - Resets (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 88 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class FakeClock final : public Timing::IClockSynchronizationTarget<Timing::ClockTick> {
public:
    Timing::ClockTick Now{5000000000ULL};
    Timing::ClockSynchronizationStatus<Timing::ClockTick> Status{};
    std::uint32_t Resets{0U};

    Timing::ClockTick GetSynchronizationTimestampNanoseconds() const override { return Now; }
    Timing::ClockSynchronizationResult<Timing::ClockTick> SubmitSynchronizationSample(
        const Timing::ClockSynchronizationSample<Timing::ClockTick>&,
        Timing::ClockSynchronizationAdjustmentMode
    ) override { return {}; }
    Timing::ClockSynchronizationStatus<Timing::ClockTick> GetSynchronizationStatus() const override {
        return Status;
    }
    void ConfigureSynchronization(const Timing::ClockSynchronizationConfig&) override {}
    Timing::ClockSynchronizationConfig GetSynchronizationConfig() const override { return {}; }
    void ResetSynchronization() override { ++Resets; Status = {}; }
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - ReferenceConfigurations (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - ParentConfigurations (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Updates (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Shutdowns (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - AcceptConfiguration (bool): 1 bytes [0 bytes dynamic allocation]
 * - Parent (AuthenticatedDirectPeerBinding): 38 bytes [0 bytes dynamic allocation]
 * Total Memory: 60 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class FakeTransport final : public IMeshSystemClockSynchronizationTransport {
public:
    std::uint32_t ReferenceConfigurations{0U};
    std::uint32_t ParentConfigurations{0U};
    std::uint32_t Updates{0U};
    std::uint32_t Shutdowns{0U};
    bool AcceptConfiguration{true};
    AuthenticatedDirectPeerBinding Parent{};

    bool ConfigureReference() override {
        ++ReferenceConfigurations;
        return AcceptConfiguration;
    }
    bool ConfigureClientAndReference(const AuthenticatedDirectPeerBinding& parent) override {
        ++ParentConfigurations;
        Parent = parent;
        return AcceptConfiguration;
    }
    void Update() override { ++Updates; }
    void Shutdown() noexcept override { ++Shutdowns; }
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _local (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _observers (Radio::RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Total Memory: 24 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class FakeRadio final : public Radio::IRadio {
    Radio::RadioAddress _local;
    bool _started{false};
    Radio::RadioObserverSubscriptions _observers{};
public:
    explicit FakeRadio(std::uint8_t address) :
        _local(Radio::RadioAddress::FromBytes(&address, 1U)) {}
    bool Start() override { _started = true; _observers.NotifyStarted(*this); return true; }
    void Stop() noexcept override { _started = false; _observers.NotifyStopped(*this); }
    bool IsStarted() const noexcept override { return _started; }
    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {Radio::RadioCapability::ReceiveTimestamp, 32U, 1U};
    }
    Radio::RadioAddress LocalAddress() const noexcept override { return _local; }
    Radio::RadioSendResult Send(
        const Radio::RadioAddress&,
        const std::uint8_t*,
        std::size_t
    ) override { return Radio::RadioSendResult::Accepted(); }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
};

} // namespace

int main() {
    FakeClock clock;
    FakeTransport transport;
    MeshSystemClockSynchronizationCoordinator coordinator(clock, transport, Device(2U));

    const ClockCoordinationSelection parentSelection{Device(1U), Device(1U), Incarnation(1U), 1U};
    const AuthenticatedDirectPeerBinding parent{Device(1U), Incarnation(1U), 1U, {3U, 1U}};
    assert(coordinator.Converge(parentSelection, &parent, 1U) ==
           MeshSystemClockConvergenceDisposition::ParentConfigured);
    assert(coordinator.Role() == MeshSystemClockRole::ClientAndReference);
    assert(transport.ParentConfigurations == 1U);
    assert(transport.Parent.Peer == parent.Peer);
    assert(!coordinator.IsDeadlineClockReady());
    assert(coordinator.NowMilliseconds() == 5000U);

    clock.Status.State = Timing::ClockSynchronizationState::Synchronized;
    coordinator.Update();
    assert(coordinator.IsDeadlineClockReady());
    assert(coordinator.SynchronizationStatus().State ==
           Timing::ClockSynchronizationState::Synchronized);
    assert(transport.Updates == 1U);
    clock.Status.State = Timing::ClockSynchronizationState::Acquiring;
    coordinator.Update();
    assert(coordinator.IsDeadlineClockReady());
    clock.Status.State = Timing::ClockSynchronizationState::Unsynchronized;
    coordinator.Update();
    assert(!coordinator.IsDeadlineClockReady());
    clock.Status.State = Timing::ClockSynchronizationState::Synchronized;
    coordinator.Update();
    assert(coordinator.IsDeadlineClockReady());
    assert(transport.Updates == 4U);
    assert(coordinator.Converge(parentSelection, &parent, 1U) ==
           MeshSystemClockConvergenceDisposition::Unchanged);
    assert(transport.ParentConfigurations == 1U);

    const ClockCoordinationSelection otherRoot{Device(3U), Device(3U), Incarnation(1U), 1U};
    const AuthenticatedDirectPeerBinding otherParent{Device(3U), Incarnation(1U), 1U, {4U, 1U}};
    assert(coordinator.Converge(otherRoot, &otherParent, 1U) ==
           MeshSystemClockConvergenceDisposition::ParentConfigured);
    assert(clock.Resets == 1U);
    assert(!coordinator.IsDeadlineClockReady());

    const ClockCoordinationSelection localRoot{Device(2U), {}, {}, ClockRootStratum};
    assert(coordinator.Converge(localRoot, nullptr, 1U) ==
           MeshSystemClockConvergenceDisposition::ReferenceConfigured);
    assert(coordinator.IsDeadlineClockReady());
    assert(transport.ReferenceConfigurations == 1U);

    const AuthenticatedDirectPeerBinding wrongRadio{Device(1U), Incarnation(1U), 2U, {3U, 1U}};
    assert(coordinator.Converge(parentSelection, &wrongRadio, 1U) ==
           MeshSystemClockConvergenceDisposition::ParentBindingUnavailable);
    assert(coordinator.Role() == MeshSystemClockRole::Reference);

    assert(coordinator.Converge({}, nullptr, 0U) ==
           MeshSystemClockConvergenceDisposition::Disabled);
    assert(coordinator.Role() == MeshSystemClockRole::Disabled);
    assert(transport.Shutdowns == 1U);
    coordinator.Reset();
    assert(clock.Resets == 2U);
    assert(transport.Shutdowns == 2U);

    FakeRadio radio(0x21U);
    Radio::RadioTransport radioTransport;
    assert(radioTransport.AddInterface(radio));
    assert(radioTransport.Start());
    const std::uint8_t parentAddressByte = 0x31U;
    const auto parentAddress = Radio::RadioAddress::FromBytes(&parentAddressByte, 1U);
    Radio::RadioPeerHandle peer{};
    assert(radioTransport.Peers().Observe(radio, parentAddress, peer) ==
           Radio::RadioPeerObserveResult::Observed);
    FakeClock radioClockTarget;
    Radio::RadioClockSynchronizer radioSynchronizer(radio, &radioClockTarget);
    Radio::RadioClockSynchronizationConfig base;
    base.SynchronizationIntervalMilliseconds = 250U;
    base.AdjustmentMode = Timing::ClockSynchronizationAdjustmentMode::StepIfUnsynchronized;
    base.RequireReceiveTimestamp = true;
    RadioMeshSystemClockSynchronizationTransport radioAdapter(
        radioSynchronizer, radioTransport, radio, base);
    const AuthenticatedDirectPeerBinding radioParent{
        Device(1U), Incarnation(1U), 1U, peer};
    assert(radioAdapter.ConfigureClientAndReference(radioParent));
    auto applied = radioSynchronizer.GetConfig();
    assert(applied.Mode == Radio::RadioClockSynchronizationMode::ClientAndReference);
    assert(applied.ReferencePeer == parentAddress);
    assert(applied.SynchronizationIntervalMilliseconds == 250U);
    assert(applied.RequireReceiveTimestamp);
    assert(radioAdapter.ConfigureReference());
    applied = radioSynchronizer.GetConfig();
    assert(applied.Mode == Radio::RadioClockSynchronizationMode::Reference);
    assert(!applied.ReferencePeer.IsValid());
    radioAdapter.Shutdown();
    radioTransport.Stop();
    return 0;
}
