#include <cassert>
#include <cstdint>

#include "ESPressio_MeshSystemClockSynchronization.hpp"

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t v){System::DeviceIdentifier::Storage b{};b.back()=v;return System::DeviceIdentifier{b};}
Mesh::MembershipIncarnation Inc(std::uint8_t v){Mesh::MembershipIncarnation::Storage b{};b.back()=v;return Mesh::MembershipIncarnation{b};}

class TimingTarget final : public Timing::IClockSynchronizationTarget {
public:
    std::uint64_t Selected{0};
    std::size_t SelectCalls{0};
    std::size_t Resets{0};
    bool Acquiring{false};
    bool ReferenceAvailable{false};
    Timing::ClockTimestampCapture<> CaptureSynchronizationTimestamp(
        Timing::ClockCaptureQuality=Timing::ClockCaptureQuality::SoftwareUnbounded,
        Timing::ClockUncertainty={}) const override{return {};}
    Timing::ClockSynchronizationResult SubmitSynchronizationObservation(
        const Timing::ClockSynchronizationObservation<>&) override{return {};}
    Timing::ClockSynchronizationStatus GetSynchronizationStatus() const override{return {};}
    Timing::ClockConfigurationStatus ConfigureSynchronization(const Timing::ClockSynchronizationProfile&) override{
        return Timing::ClockConfigurationStatus::Success;
    }
    Timing::ClockSynchronizationProfile GetSynchronizationProfile() const override{return {};}
    Timing::ClockConfigurationStatus SelectSynchronizationReference(std::uint64_t reference) override{
        Selected=reference;++SelectCalls;return reference?Timing::ClockConfigurationStatus::Success:Timing::ClockConfigurationStatus::InvalidReference;
    }
    void SetSynchronizationActivity(bool acquiring,bool available) override{Acquiring=acquiring;ReferenceAvailable=available;}
    void ResetSynchronization() override{++Resets;Selected=0;}
    void RecordSynchronizationDeadlineMiss() override{}
};

class TransportControl final : public Mesh::IMeshClockReferenceTransportControl {
public:
    Mesh::MeshClockReferenceRelationship Last{};
    std::size_t SelectCalls{0};
    std::size_t Clears{0};
    bool Accept{true};
    bool SelectDirectReference(const Mesh::MeshClockReferenceRelationship& r) noexcept override{
        if(!Accept||!r.IsValid()) return false;Last=r;++SelectCalls;return true;
    }
    void ClearDirectReference() noexcept override{Last={};++Clears;}
};
}

int main(){
    const auto local=Device(1),root=Device(9),parentA=Device(2),parentB=Device(3);
    const auto incA=Inc(2),incB=Inc(3);
    const Radio::RadioPeerHandle peerA{1,1},peerB{2,1};
    const Mesh::AuthenticatedDirectPeerBinding bindingA{parentA,incA,1,peerA};
    const Mesh::AuthenticatedDirectPeerBinding bindingB{parentB,incB,1,peerB};
    TimingTarget timing;TransportControl transport;
    Mesh::MeshSystemClockSynchronizationCoordinator coordinator(timing,transport,local);

    const Mesh::MeshClockReferenceLineage qualified{
        101,Timing::TimeReliability::Synchronized,Timing::ClockUncertainty::Known(300'000),Timing::ClockUncertainty::Known(100'000)};
    static_assert(Timing::ClockSynchronizationProfile::QualifiedCeilingNanoseconds==1'000'000);
    assert(qualified.IsValid());
    assert(qualified.EffectiveUncertainty().IsKnown&&qualified.EffectiveUncertainty().Nanoseconds==400'000);
    const Mesh::MeshClockReferenceLineage excessive{
        102,Timing::TimeReliability::Synchronized,Timing::ClockUncertainty::Known(900'000),Timing::ClockUncertainty::Known(100'000)};
    assert(!excessive.IsValid());
    const Mesh::MeshClockReferenceLineage acquiringUnknown{103,Timing::TimeReliability::Acquiring,{},{}};
    assert(acquiringUnknown.IsValid());

    const Mesh::ClockCoordinationSelection first{root,parentA,incA,1};
    assert(coordinator.Converge(first,&bindingA,1,qualified)==Mesh::MeshSystemClockConvergenceDisposition::ParentConfigured);
    assert(coordinator.Role()==Mesh::MeshSystemClockRole::ClientAndReference);
    assert(timing.Selected==101&&timing.SelectCalls==1&&timing.Acquiring&&timing.ReferenceAvailable);
    assert(transport.SelectCalls==1&&transport.Last.Lineage.EffectiveUncertainty().Nanoseconds==400'000);

    assert(coordinator.Converge(first,&bindingA,1,qualified)==Mesh::MeshSystemClockConvergenceDisposition::Unchanged);
    assert(timing.SelectCalls==1&&transport.SelectCalls==1);

    // Same root, different authenticated parent/source: Timing must be told the source identity changed.
    const Mesh::MeshClockReferenceLineage failover{
        202,Timing::TimeReliability::Holdover,Timing::ClockUncertainty::Known(450'000),Timing::ClockUncertainty::Known(100'000)};
    const Mesh::ClockCoordinationSelection second{root,parentB,incB,1};
    assert(coordinator.Converge(second,&bindingB,1,failover)==Mesh::MeshSystemClockConvergenceDisposition::ParentConfigured);
    assert(timing.Selected==202&&timing.SelectCalls==2);
    assert(coordinator.Lineage().Reliability==Timing::TimeReliability::Holdover);
    assert(coordinator.Lineage().EffectiveUncertainty().Nanoseconds==550'000);

    assert(coordinator.Converge(second,&bindingB,1,excessive)==Mesh::MeshSystemClockConvergenceDisposition::ReferenceLineageInvalid);
    assert(timing.Selected==202&&transport.SelectCalls==2);

    Mesh::ClockCoordinationSelection localRoot{local,{}, {},Mesh::ClockRootStratum};
    assert(coordinator.Converge(localRoot,nullptr,1)==Mesh::MeshSystemClockConvergenceDisposition::ReferenceConfigured);
    assert(coordinator.Role()==Mesh::MeshSystemClockRole::Reference);
    assert(!timing.Acquiring&&timing.ReferenceAvailable&&timing.Resets==1);

    assert(coordinator.Converge({},nullptr,0)==Mesh::MeshSystemClockConvergenceDisposition::Disabled);
    assert(!timing.Acquiring&&!timing.ReferenceAvailable&&timing.Resets==2);
    return 0;
}
