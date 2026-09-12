#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_MeshRemainingResidence.hpp"
#include "ESPressio_MeshV1BroadcastFrame.hpp"

using namespace ESPressio::Mesh;

static MeshIdentifier MeshId(){MeshIdentifier::Storage bytes{};bytes[15]=1;return MeshIdentifier{bytes};}
static ESPressio::System::DeviceIdentifier Device(std::uint8_t tail){ESPressio::System::DeviceIdentifier::Storage bytes{};bytes[15]=tail;return ESPressio::System::DeviceIdentifier{bytes};}
static MembershipIncarnation Incarnation(std::uint8_t tail){MembershipIncarnation::Storage bytes{};bytes[15]=tail;return MembershipIncarnation{bytes};}

int main(){
    std::uint64_t expiry=0;
    assert(TryEstablishMeshLocalExpiry(1'000'000'000ULL,250,0,expiry));
    assert(expiry==1'250'000'000ULL);

    // Adapter admission ceiling may only shorten the local lifetime.
    assert(TryEstablishMeshLocalExpiry(1'000'000'000ULL,250,40'000'000ULL,expiry));
    assert(expiry==1'040'000'000ULL);
    assert(!TryEstablishMeshLocalExpiry(1'000'000'000ULL,0,0,expiry));

    std::uint32_t remaining=0;
    assert(TryEncodeMeshRemainingResidenceMilliseconds(1'250'000'000ULL,1'100'000'000ULL,250,remaining));
    assert(remaining==150);
    // Even if the retained local expiry would imply more time, forwarding may never exceed the received maximum.
    assert(TryEncodeMeshRemainingResidenceMilliseconds(2'000'000'000ULL,1'100'000'000ULL,250,remaining));
    assert(remaining==250);
    assert(!TryEncodeMeshRemainingResidenceMilliseconds(1'100'999'999ULL,1'100'000'000ULL,250,remaining));

    std::uint64_t retainedExpiry=2'000'000'000ULL;
    assert(TightenMeshLocalExpiry(1'500'000'000ULL,100,retainedExpiry));
    assert(retainedExpiry==1'600'000'000ULL);
    assert(TightenMeshLocalExpiry(1'550'000'000ULL,300,retainedExpiry));
    assert(retainedExpiry==1'600'000'000ULL); // a later duplicate cannot extend the lifetime

    MeshV1BroadcastOriginHeader header{};
    header.Mesh=MeshId();
    header.Source=Device(1);
    header.SourceIncarnation=Incarnation(2);
    header.MessageId=0x0102030405060708ULL;
    header.RemainingResidenceMilliseconds=0x01020304U;
    header.PrimitiveFamily=ESPressio::Primitive::FamilyIds::ApplicationPrivateFirst;
    header.PrimitiveVersion=1;
    header.PayloadBytes=3;
    assert(header.IsValid());
    static_assert(MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes==76);

    const auto packetBytes=MeshV1BroadcastFrameCodec::OriginPacketBytes(header.PayloadBytes);
    std::array<std::uint8_t,256> wire{};
    assert(packetBytes<=wire.size());
    assert(MeshV1BroadcastFrameCodec::EncodeOriginAuthenticatedHeader(header,wire.data(),wire.size()));
    wire[MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes+0]=7;
    wire[MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes+1]=8;
    wire[MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes+2]=9;
    const auto signatureOffset=MeshV1BroadcastFrameCodec::OriginAuthenticatedHeaderBytes+header.PayloadBytes;
    for(std::size_t i=0;i<MeshV1SecuritySuite::IdentitySignatureBytes;++i) wire[signatureOffset+i]=0x5A;

    // Remaining residence is manual big-endian wire data immediately after MessageId.
    constexpr std::size_t residenceOffset=10+16+16+16+8;
    assert(wire[residenceOffset+0]==0x01);
    assert(wire[residenceOffset+1]==0x02);
    assert(wire[residenceOffset+2]==0x03);
    assert(wire[residenceOffset+3]==0x04);

    MeshV1BroadcastOriginHeader decoded{};
    MeshV1BroadcastOriginView view{};
    assert(MeshV1BroadcastFrameCodec::DecodeOrigin(wire.data(),packetBytes,decoded,view));
    assert(decoded.RemainingResidenceMilliseconds==header.RemainingResidenceMilliseconds);
    assert(decoded.MessageId==header.MessageId);
    assert(view.PayloadByteCount==3&&view.Payload[0]==7&&view.Payload[2]==9);

    header.RemainingResidenceMilliseconds=0;
    assert(!header.IsValid());
    return 0;
}
