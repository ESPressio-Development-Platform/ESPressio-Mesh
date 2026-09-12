#pragma once

#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Semantic reason accompanying a Mesh node lifecycle notification.</summary>
enum class MeshNodeLifecycleReason : std::uint8_t {
    None=0,
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

/// <summary>Closed lifecycle event vocabulary delivered to one fixed infrastructure sink.</summary>
enum class MeshNodeLifecycleEvent : std::uint8_t {
    Joining=0,
    Authenticated,
    Rejected,
    Unavailable,
    Lost,
    Disconnected
};

/// <summary>Immutable identity/state snapshot for one bounded lifecycle diagnostic notification.</summary>
struct MeshNodeLifecycleNotification final {
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MembershipState Membership{MembershipState::Unknown};
    ReachabilityState Reachability{ReachabilityState::Unknown};
    MeshNodeLifecycleReason Reason{MeshNodeLifecycleReason::None};

    constexpr bool HasIdentity() const noexcept {
        return static_cast<bool>(Device)&&static_cast<bool>(Incarnation);
    }
};

/// <summary>
/// Fixed optional infrastructure sink for Mesh lifecycle diagnostics. It is not an application callback registry.
/// </summary>
/// <remarks>
/// Composition installs at most one borrowed sink before Running. The sink must be bounded/noexcept and must not call
/// back into Mesh mutation paths. Missing sinks are a no-op; diagnostics never alter membership/security outcomes.
/// </remarks>
class IMeshLifecycleSink {
public:
    virtual ~IMeshLifecycleSink()=default;
    virtual void MeshLifecycleChanged(MeshNodeLifecycleEvent event,
                                      const MeshNodeLifecycleNotification& notification) noexcept=0;
};

/// <summary>Allocation-free fixed lifecycle notification relay replacing the predecessor Observable fan-out.</summary>
class MeshLifecycleNotifications final {
    IMeshLifecycleSink* _sink{nullptr};

    void Notify(MeshNodeLifecycleEvent event,const MeshNodeLifecycleNotification& notification) noexcept {
        auto* sink=_sink;
        if(sink!=nullptr) sink->MeshLifecycleChanged(event,notification);
    }
public:
    constexpr MeshLifecycleNotifications() noexcept=default;
    explicit constexpr MeshLifecycleNotifications(IMeshLifecycleSink* sink) noexcept:_sink(sink) {}

    bool SetSink(IMeshLifecycleSink* sink) noexcept {
        if(_sink!=nullptr&&sink!=nullptr&&_sink!=sink) return false;
        _sink=sink;return true;
    }
    IMeshLifecycleSink* Sink() const noexcept { return _sink; }

    void NotifyJoining(const MeshNodeLifecycleNotification& event) noexcept {
        Notify(MeshNodeLifecycleEvent::Joining,event);
    }
    void NotifyAuthenticated(const MeshNodeLifecycleNotification& event) noexcept {
        Notify(MeshNodeLifecycleEvent::Authenticated,event);
    }
    void NotifyRejected(const MeshNodeLifecycleNotification& event) noexcept {
        Notify(MeshNodeLifecycleEvent::Rejected,event);
    }
    void NotifyUnavailable(const MeshNodeLifecycleNotification& event) noexcept {
        Notify(MeshNodeLifecycleEvent::Unavailable,event);
    }
    void NotifyLost(const MeshNodeLifecycleNotification& event) noexcept {
        Notify(MeshNodeLifecycleEvent::Lost,event);
    }
    void NotifyDisconnected(const MeshNodeLifecycleNotification& event) noexcept {
        Notify(MeshNodeLifecycleEvent::Disconnected,event);
    }
};

} // namespace ESPressio::Mesh
