#pragma once

#include <cstdint>
#include <memory>

#include <ESPressio_DeviceIdentifier.hpp>
#include <ESPressio_Memory.hpp>
#include <ESPressio_ThreadSafeObservable.hpp>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Semantic reason accompanying a Mesh node lifecycle notification.</summary>
enum class MeshNodeLifecycleReason : std::uint8_t {
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
class MeshLifecycleNotifications final {
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;

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
