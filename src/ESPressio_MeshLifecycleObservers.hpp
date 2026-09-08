#pragma once

#include <cstdint>
#include <memory>

#include <ESPressio_DeviceIdentifier.hpp>
#include <ESPressio_Memory.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Semantic reason accompanying a Mesh node lifecycle notification.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum
class MeshNodeLifecycleReason : std::uint8_t {
    None = 0,
    AuthenticationRejected,
    AdmissionRejected,
    ConflictingIncarnation,
    ResourceUnavailable,
    UnreachableTimeout,
    AuthoritativeLeave,
    SupersededIncarnation,
    ControlledShutdown,
    Invalid
};

/// <summary>Immutable identity/state snapshot delivered to Mesh lifecycle observers.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Device (System::DeviceIdentifier): 16 bytes [0 bytes dynamic allocation]
 * - Incarnation (MembershipIncarnation): 16 bytes [0 bytes dynamic allocation]
 * - Membership (MembershipState): 1 bytes [0 bytes dynamic allocation]
 * - Reachability (ReachabilityState): 1 bytes [0 bytes dynamic allocation]
 * - Reason (MeshNodeLifecycleReason): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 35 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct MeshNodeLifecycleNotification final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MembershipState Membership{MembershipState::Unknown};
    ReachabilityState Reachability{ReachabilityState::Unknown};
    MeshNodeLifecycleReason Reason{MeshNodeLifecycleReason::None};

    constexpr bool HasIdentity() const noexcept {
        return static_cast<bool>(Device) && static_cast<bool>(Incarnation);
    }
};

/// <summary>
/// Observer surface for critical Mesh membership/admission/reachability lifecycle transitions.
/// </summary>
/// <remarks>
/// Methods are intentionally independent so consumers can override only the transitions they care about. Notifications
/// are synchronous and advisory; observer exceptions are isolated and can never change Mesh membership/security state.
/// "Unavailable" means the authenticated membership is retained but Reachability is Unreachable. "Lost" means local
/// retention expired and the full membership was retired. "Disconnected" is reserved for authenticated graceful Leave.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IMeshLifecycleObserver : public Observable::IObserver {
public:
    ~IMeshLifecycleObserver() override = default;

    virtual void OnMeshNodeJoining(const MeshNodeLifecycleNotification&) {}
    virtual void OnMeshNodeAuthenticated(const MeshNodeLifecycleNotification&) {}
    virtual void OnMeshNodeRejected(const MeshNodeLifecycleNotification&) {}
    virtual void OnMeshNodeUnavailable(const MeshNodeLifecycleNotification&) {}
    virtual void OnMeshNodeLost(const MeshNodeLifecycleNotification&) {}
    virtual void OnMeshNodeDisconnected(const MeshNodeLifecycleNotification&) {}
};

/// <summary>Shared Observable-backed source for Mesh lifecycle callbacks.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - _source (std::shared_ptr<Source>): 8 bytes [shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Total Memory: 8 bytes [_source: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; _source: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _source: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _source: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _source: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _source: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; _source: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; _source: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; _source: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; _source: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; _source: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class MeshLifecycleNotifications final {
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 96 bytes [ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 96 bytes [ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class Source final : public Observable::ThreadSafeObservable {
        template<typename TCallback>
        void Notify(TCallback&& callback) noexcept {
            try {
                ExecuteNotification([&](NotificationContext& notification) {
                    notification.WithObservers<IMeshLifecycleObserver>(
                        [&](IMeshLifecycleObserver* observer) {
                            if (observer == nullptr) return;
                            try { callback(*observer); } catch (...) {}
                        });
                });
            } catch (...) {
                // Lifecycle notification is advisory and must never perturb Mesh authority/state.
            }
        }

    public:
        void Joining(const MeshNodeLifecycleNotification& event) noexcept {
            Notify([&](IMeshLifecycleObserver& observer) { observer.OnMeshNodeJoining(event); });
        }
        void Authenticated(const MeshNodeLifecycleNotification& event) noexcept {
            Notify([&](IMeshLifecycleObserver& observer) { observer.OnMeshNodeAuthenticated(event); });
        }
        void Rejected(const MeshNodeLifecycleNotification& event) noexcept {
            Notify([&](IMeshLifecycleObserver& observer) { observer.OnMeshNodeRejected(event); });
        }
        void Unavailable(const MeshNodeLifecycleNotification& event) noexcept {
            Notify([&](IMeshLifecycleObserver& observer) { observer.OnMeshNodeUnavailable(event); });
        }
        void Lost(const MeshNodeLifecycleNotification& event) noexcept {
            Notify([&](IMeshLifecycleObserver& observer) { observer.OnMeshNodeLost(event); });
        }
        void Disconnected(const MeshNodeLifecycleNotification& event) noexcept {
            Notify([&](IMeshLifecycleObserver& observer) { observer.OnMeshNodeDisconnected(event); });
        }
    };

    std::shared_ptr<Source> _source{};

    std::shared_ptr<Source> EnsureSource() noexcept {
        if (_source) return _source;
        try {
            _source = System::Memory::MakeShared<Source, ExternalPreferred>();
        } catch (...) {
            _source.reset();
        }
        return _source;
    }

public:
    Observable::ObserverHandlePtr RegisterObserver(IMeshLifecycleObserver* observer) noexcept {
        if (observer == nullptr) return {};
        auto source = EnsureSource();
        if (!source) return {};
        try {
            return source->RegisterObserverAs<IMeshLifecycleObserver>(observer);
        } catch (...) {
            return {};
        }
    }

    void UnregisterObserver(IMeshLifecycleObserver* observer) noexcept {
        if (_source && observer != nullptr) {
            try { _source->UnregisterObserver(observer); } catch (...) {}
        }
    }

    void NotifyJoining(const MeshNodeLifecycleNotification& event) noexcept {
        if (_source) _source->Joining(event);
    }
    void NotifyAuthenticated(const MeshNodeLifecycleNotification& event) noexcept {
        if (_source) _source->Authenticated(event);
    }
    void NotifyRejected(const MeshNodeLifecycleNotification& event) noexcept {
        if (_source) _source->Rejected(event);
    }
    void NotifyUnavailable(const MeshNodeLifecycleNotification& event) noexcept {
        if (_source) _source->Unavailable(event);
    }
    void NotifyLost(const MeshNodeLifecycleNotification& event) noexcept {
        if (_source) _source->Lost(event);
    }
    void NotifyDisconnected(const MeshNodeLifecycleNotification& event) noexcept {
        if (_source) _source->Disconnected(event);
    }
};

} // namespace ESPressio::Mesh
