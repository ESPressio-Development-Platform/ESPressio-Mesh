#include <cassert>
#include <cstdint>

#include "ESPressio_MeshLifecycleObservers.hpp"

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t value){
    System::DeviceIdentifier::Storage bytes{};bytes.back()=value;return System::DeviceIdentifier{bytes};
}
Mesh::MembershipIncarnation Incarnation(std::uint8_t value){
    Mesh::MembershipIncarnation::Storage bytes{};bytes.back()=value;return Mesh::MembershipIncarnation{bytes};
}
class Sink final : public Mesh::IMeshLifecycleSink {
public:
    Mesh::MeshNodeLifecycleEvent LastEvent{Mesh::MeshNodeLifecycleEvent::Joining};
    Mesh::MeshNodeLifecycleNotification Last{};
    std::size_t Calls{0};
    void MeshLifecycleChanged(Mesh::MeshNodeLifecycleEvent event,
                              const Mesh::MeshNodeLifecycleNotification& notification) noexcept override {
        LastEvent=event;Last=notification;++Calls;
    }
};
}

int main(){
    Sink sink;
    Mesh::MeshLifecycleNotifications notifications;
    assert(notifications.Sink()==nullptr);
    assert(notifications.SetSink(&sink));
    const Mesh::MeshNodeLifecycleNotification event{
        Device(4),Incarnation(7),Mesh::MembershipState::Active,Mesh::ReachabilityState::Reachable,
        Mesh::MeshNodeLifecycleReason::None};
    notifications.NotifyAuthenticated(event);
    assert(sink.Calls==1);
    assert(sink.LastEvent==Mesh::MeshNodeLifecycleEvent::Authenticated);
    assert(sink.Last.Device==event.Device&&sink.Last.Incarnation==event.Incarnation);
    Sink other;
    assert(!notifications.SetSink(&other));
    assert(notifications.SetSink(nullptr));
    notifications.NotifyLost(event);
    assert(sink.Calls==1);
    return 0;
}
