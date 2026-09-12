#include <cassert>
#include <cstdint>
#include <type_traits>

#include "ESPressio_MeshRelayCapacity.hpp"

using namespace ESPressio::Mesh;

using SmallArena=MeshRelayByteArena<MeshRelayByteClass<32,1>,MeshRelayByteClass<128,1>>;
using Domain=MeshRelayCapacityDomain<1,24,SmallArena>;
using Inbound=MeshRelayCapacityPlane<MeshRelayDirection::Inbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=MeshRelayCapacityPlane<MeshRelayDirection::Outbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain>;

static_assert(!std::is_copy_constructible_v<MeshRelayByteLease>);
static_assert(!std::is_copy_constructible_v<MeshRelayCapacityBundle>);
static_assert(SmallArena::ClassCount==2);
static_assert(SmallArena::Shapes()[0].SlotBytes==32 && SmallArena::Shapes()[1].SlotBytes==128);
static_assert(static_cast<std::uint8_t>(MeshRelayServiceClass::Infrastructure)==0);
static_assert(static_cast<std::uint8_t>(MeshRelayServiceClass::Clock)==1);
static_assert(static_cast<std::uint8_t>(MeshRelayServiceClass::Critical)==2);
static_assert(static_cast<std::uint8_t>(MeshRelayServiceClass::Responsive)==3);
static_assert(static_cast<std::uint8_t>(MeshRelayServiceClass::Convergent)==4);
static_assert(static_cast<std::uint8_t>(MeshRelayServiceClass::BestEffort)==5);

struct WakeCounter final { unsigned Count{0}; static void Wake(void* context) noexcept {++static_cast<WakeCounter*>(context)->Count;} };

int main(){
    WakeCounter wakes;
    Inbound inbound;
    inbound.Initialize({&wakes,&WakeCounter::Wake});

    MeshRelayCapacityBundle clock;
    assert(inbound.TryAcquireTrusted(MeshRelayServiceClass::Clock,20,clock)==MeshRelayResourceStatus::Success);
    assert(clock);
    assert(clock.Identity().Direction==MeshRelayDirection::Inbound);
    assert(clock.Identity().Domain==MeshRelayCapacityDomainKind::ClockPrivate);
    assert(clock.Bytes.Capacity()==32);
    assert(!clock.IsCommitted());
    auto bytes=clock.Bytes.MutableView();assert(bytes&&bytes.Capacity==32);
    bytes.Data[0]=0xA5;bytes.Data[19]=0x5A;
    auto workspace=clock.Workspace();assert(workspace&&workspace.Capacity==24);
    workspace.Data[0]=7;
    assert(clock.Bytes.Commit(20)==MeshRelayResourceStatus::Success);
    assert(clock.IsCommitted());
    assert(!clock.Bytes.MutableView());
    assert(clock.Bytes.View().Size==20&&clock.Bytes.View().Data[0]==0xA5);

    // Clock private is full; the next Clock record may use only SharedOverflow, never another private class.
    MeshRelayCapacityBundle overflow;
    assert(inbound.TryAcquireTrusted(MeshRelayServiceClass::Clock,20,overflow)==MeshRelayResourceStatus::Success);
    assert(overflow.Identity().Domain==MeshRelayCapacityDomainKind::SharedOverflow);

    // SharedOverflow is now full too; Critical still has its own isolated private guarantee.
    MeshRelayCapacityBundle critical;
    assert(inbound.TryAcquireTrusted(MeshRelayServiceClass::Critical,20,critical)==MeshRelayResourceStatus::Success);
    assert(critical.Identity().Domain==MeshRelayCapacityDomainKind::CriticalPrivate);

    // Untrusted ingress can occupy only its physically separate quarantine domain.
    MeshRelayCapacityBundle untrusted;
    assert(inbound.TryAcquireUntrusted(100,untrusted)==MeshRelayResourceStatus::Success);
    assert(untrusted.Identity().Domain==MeshRelayCapacityDomainKind::UntrustedIngress);
    assert(untrusted.Bytes.Capacity()==128);

    // Atomic rollback: record acquisition cannot leak when no fitting bytes are available.
    Inbound rollbackPlane;rollbackPlane.Initialize();
    MeshRelayCapacityBundle firstLarge;
    assert(rollbackPlane.TryAcquireTrusted(MeshRelayServiceClass::BestEffort,100,firstLarge)==MeshRelayResourceStatus::Success);
    MeshRelayCapacityBundle sharedLarge;
    assert(rollbackPlane.TryAcquireTrusted(MeshRelayServiceClass::BestEffort,100,sharedLarge)==MeshRelayResourceStatus::Success);
    MeshRelayCapacityBundle rejected;
    assert(rollbackPlane.TryAcquireTrusted(MeshRelayServiceClass::BestEffort,100,rejected)==MeshRelayResourceStatus::Exhausted);
    assert(!rejected);
    firstLarge.Reset();
    MeshRelayCapacityBundle replacement;
    assert(rollbackPlane.TryAcquireTrusted(MeshRelayServiceClass::BestEffort,100,replacement)==MeshRelayResourceStatus::Success);
    assert(replacement.Identity().Domain==MeshRelayCapacityDomainKind::BestEffortPrivate);

    // Directional isolation: outbound occupancy cannot consume inbound reserves and outbound has no quarantine API capacity.
    Outbound outbound;outbound.Initialize();
    MeshRelayCapacityBundle outboundClock;
    assert(outbound.TryAcquireTrusted(MeshRelayServiceClass::Clock,20,outboundClock)==MeshRelayResourceStatus::Success);
    MeshRelayCapacityBundle inboundResponsive;
    assert(inbound.TryAcquireTrusted(MeshRelayServiceClass::Responsive,20,inboundResponsive)==MeshRelayResourceStatus::Success);
    MeshRelayCapacityBundle invalidQuarantine;
    assert(outbound.TryAcquireUntrusted(20,invalidQuarantine)==MeshRelayResourceStatus::InvalidConfiguration);

    const auto wakeBefore= wakes.Count;
    clock.Reset();
    assert(wakes.Count>wakeBefore); // release produces a coalescible availability wake boundary

    return 0;
}
