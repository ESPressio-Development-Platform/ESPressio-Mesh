#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_MeshBroadcastLifecycle.hpp"

using namespace ESPressio;

static System::DeviceIdentifier Device(std::uint8_t tail){
    System::DeviceIdentifier::Storage bytes{};bytes[15]=tail;return System::DeviceIdentifier{bytes};
}
static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail){
    Mesh::MembershipIncarnation::Storage bytes{};bytes[15]=tail;return Mesh::MembershipIncarnation{bytes};
}

class Receiver final:public Mesh::IPrimitiveReceiver{
public:
    Primitive::PrimitiveAdmissionDisposition Next{Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable};
    unsigned Calls{0};
    std::array<std::uint8_t,4> Last{};
    Primitive::PrimitiveAdmissionDisposition Receive(
        const Mesh::MeshReceiveContext&,
        Primitive::PrimitiveProtocolVersion,
        Mesh::PrimitivePayloadView payload) noexcept override {
        ++Calls;
        for(std::size_t i=0;i<payload.Size&&i<Last.size();++i) Last[i]=payload.Data[i];
        return Next;
    }
};

int main(){
    using Network=Mesh::MeshBroadcastNetworkStateTable<2>;
    Network network;
    const auto source=Device(1);
    const auto incarnation=Incarnation(1);

    // Seen and Forwarded are separate network facts. Local family state is not consulted here.
    assert(network.Classify(source,incarnation,10)==Mesh::MeshBroadcastNetworkDisposition::NewlySeen);
    assert(network.CommitAuthenticatedSeen(source,incarnation,10)==Mesh::MeshBroadcastNetworkDisposition::NewlySeen);
    assert(network.Classify(source,incarnation,10)==Mesh::MeshBroadcastNetworkDisposition::SeenNotForwarded);
    assert(network.CommitAuthenticatedSeen(source,incarnation,10)==Mesh::MeshBroadcastNetworkDisposition::SeenNotForwarded);
    assert(network.CommitForwarded(source,incarnation,10)==Mesh::MeshBroadcastForwardCommitResult::Committed);
    assert(network.Classify(source,incarnation,10)==Mesh::MeshBroadcastNetworkDisposition::Forwarded);
    assert(network.CommitForwarded(source,incarnation,10)==Mesh::MeshBroadcastForwardCommitResult::AlreadyForwarded);
    assert(network.CommitAuthenticatedSeen(source,incarnation,10)==Mesh::MeshBroadcastNetworkDisposition::Forwarded);

    // A later seen message can exist independently before forwarding is committed.
    assert(network.CommitAuthenticatedSeen(source,incarnation,11)==Mesh::MeshBroadcastNetworkDisposition::NewlySeen);
    assert(network.Classify(source,incarnation,11)==Mesh::MeshBroadcastNetworkDisposition::SeenNotForwarded);

    Mesh::PrimitiveReceiverRegistry<> receivers;
    Receiver receiver;
    Mesh::PrimitiveReceiverHandle handle{};
    const Mesh::PrimitiveReceiverDescriptor descriptor{
        Primitive::FamilyIds::ApplicationPrivateFirst,
        Primitive::PrimitiveProtocolVersionRange{1,1},
        {},
        Mesh::PrimitiveReceiverExposure::Advertised
    };
    assert(receivers.Register(descriptor,receiver,handle)==Mesh::PrimitiveReceiverRegistrationResult::Registered);

    Mesh::MeshDeferredLocalTable<2,16> deferred;
    const std::array<std::uint8_t,4> payload{{1,2,3,4}};
    const Mesh::MeshReceiveContext context{source,incarnation,11,3,true};
    constexpr std::uint64_t expiry=2'000'000'000ULL;

    assert(deferred.Store(context,descriptor.Family,1,{payload.data(),payload.size()},expiry,5,1'000'000'000ULL)==
           Mesh::MeshDeferredLocalStoreResult::Stored);
    // A duplicate cannot allocate a second record, replace bytes or reset expiry/generation.
    const std::array<std::uint8_t,4> changed{{9,9,9,9}};
    assert(deferred.Store(context,descriptor.Family,1,{changed.data(),changed.size()},3'000'000'000ULL,99,1'000'000'000ULL)==
           Mesh::MeshDeferredLocalStoreResult::AlreadyRetained);

    Primitive::PrimitiveAdmissionDisposition disposition{};
    // No admission-generation change means no polling retry.
    assert(deferred.ServiceOne(receivers,5,1'100'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::NoWork);
    assert(receiver.Calls==0);

    // One capacity/lifecycle generation transition permits one retry quantum.
    receiver.Next=Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable;
    assert(deferred.ServiceOne(receivers,6,1'100'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::RemainsDeferred);
    assert(disposition==Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);
    assert(receiver.Calls==1);
    assert(receiver.Last==payload); // immutable retained representation, not duplicate bytes

    // Reusing the same generation cannot spin.
    assert(deferred.ServiceOne(receivers,6,1'200'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::NoWork);
    assert(receiver.Calls==1);

    receiver.Next=Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted;
    assert(deferred.ServiceOne(receivers,7,1'300'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::Admitted);
    assert(disposition==Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
    assert(receiver.Calls==2);

    // Expiry consumes the same original lifetime and requires no generation transition.
    Mesh::MeshReceiveContext context2{source,incarnation,12,2,true};
    assert(deferred.Store(context2,descriptor.Family,1,{payload.data(),payload.size()},1'500'000'000ULL,7,1'400'000'000ULL)==
           Mesh::MeshDeferredLocalStoreResult::Stored);
    assert(deferred.ServiceOne(receivers,7,1'500'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::Expired);
    assert(receiver.Calls==2);

    // Definitive family outcomes are terminal and are never retried under another generation.
    Mesh::MeshReceiveContext context3{source,incarnation,13,2,true};
    assert(deferred.Store(context3,descriptor.Family,1,{payload.data(),payload.size()},3'000'000'000ULL,8,1'400'000'000ULL)==
           Mesh::MeshDeferredLocalStoreResult::Stored);
    receiver.Next=Primitive::PrimitiveAdmissionDisposition::Rejected;
    assert(deferred.ServiceOne(receivers,9,1'500'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::Terminal);
    const auto calls=receiver.Calls;
    assert(deferred.ServiceOne(receivers,10,1'600'000'000ULL,disposition)==Mesh::MeshDeferredLocalServiceResult::NoWork);
    assert(receiver.Calls==calls);

    return 0;
}
