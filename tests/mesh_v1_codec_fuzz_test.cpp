#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshV1ProtectedFrame.hpp"
#include "ESPressio_MeshV1Security.hpp"

using namespace ESPressio;

namespace {

std::uint32_t Next(std::uint32_t& state) noexcept {
    state^=state<<13U;
    state^=state>>17U;
    state^=state<<5U;
    return state;
}

} // namespace

int main() {
    std::array<std::uint8_t,512> bytes{};
    std::uint32_t seed=0x4D385046U;

    // Deterministic bounded malformed-input fuzzing. Every public Mesh-v1 decoder must
    // reject or decode without reading outside the supplied span and must always leave
    // a self-consistent output object when it reports success.
    for(std::size_t iteration=0;iteration<4096;++iteration) {
        const auto size=static_cast<std::size_t>(Next(seed)%bytes.size());
        for(std::size_t i=0;i<size;++i) bytes[i]=static_cast<std::uint8_t>(Next(seed));

        Mesh::MeshV1InitiatorHello initiator{};
        const bool decodedInitiator=Mesh::MeshV1SecurityHandshakeCodec::DecodeInitiator(bytes.data(),size,initiator);
        assert(!decodedInitiator||initiator.IsValid());

        Mesh::MeshV1ResponderHello responder{};
        const bool decodedResponder=Mesh::MeshV1SecurityHandshakeCodec::DecodeResponder(bytes.data(),size,responder);
        assert(!decodedResponder||responder.IsValid());

        Mesh::MeshV1InitiatorFinish finish{};
        const bool decodedFinish=Mesh::MeshV1SecurityHandshakeCodec::DecodeFinish(bytes.data(),size,finish);
        assert(!decodedFinish||finish.IsValid());

        Mesh::MeshV1EndToEndFrameHeader endToEnd{};
        Mesh::MeshV1ProtectedFrameView endToEndView{};
        const bool decodedEndToEnd=Mesh::MeshV1ProtectedFrameCodec::DecodeEndToEnd(
            bytes.data(),size,endToEnd,endToEndView);
        if(decodedEndToEnd) {
            assert(endToEnd.IsValid());
            assert(endToEndView.AuthenticatedHeader==bytes.data());
            assert(endToEndView.AuthenticatedHeaderBytes==Mesh::MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes);
            assert(endToEndView.CiphertextBytes==endToEnd.PlaintextBytes);
            assert(endToEndView.AuthenticatedHeaderBytes+endToEndView.CiphertextBytes+
                   Mesh::MeshV1SecuritySuite::AuthenticationTagBytes==size);
        }

        Mesh::MeshV1HopFrameHeader hop{};
        Mesh::MeshV1ProtectedFrameView hopView{};
        const bool decodedHop=Mesh::MeshV1ProtectedFrameCodec::DecodeHop(bytes.data(),size,hop,hopView);
        if(decodedHop) {
            assert(hop.IsValid());
            assert(hopView.AuthenticatedHeader==bytes.data());
            assert(hopView.AuthenticatedHeaderBytes==Mesh::MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes);
            assert(hopView.CiphertextBytes==hop.InnerFrameBytes);
            assert(hopView.AuthenticatedHeaderBytes+hopView.CiphertextBytes+
                   Mesh::MeshV1SecuritySuite::AuthenticationTagBytes==size);
        }
    }

    // Boundary spans are exercised explicitly, including nullptr/zero and every size
    // around each fixed decoder envelope. No decoder may accept an undersized span.
    Mesh::MeshV1InitiatorHello initiator{};
    assert(!Mesh::MeshV1SecurityHandshakeCodec::DecodeInitiator(nullptr,0,initiator));
    for(std::size_t size=0;size<Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes;++size)
        assert(!Mesh::MeshV1SecurityHandshakeCodec::DecodeInitiator(bytes.data(),size,initiator));

    Mesh::MeshV1EndToEndFrameHeader endToEnd{};
    Mesh::MeshV1ProtectedFrameView endToEndView{};
    assert(!Mesh::MeshV1ProtectedFrameCodec::DecodeEndToEnd(nullptr,0,endToEnd,endToEndView));
    for(std::size_t size=0;
        size<Mesh::MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes+Mesh::MeshV1SecuritySuite::AuthenticationTagBytes;
        ++size)
        assert(!Mesh::MeshV1ProtectedFrameCodec::DecodeEndToEnd(bytes.data(),size,endToEnd,endToEndView));

    Mesh::MeshV1HopFrameHeader hop{};
    Mesh::MeshV1ProtectedFrameView hopView{};
    assert(!Mesh::MeshV1ProtectedFrameCodec::DecodeHop(nullptr,0,hop,hopView));
    for(std::size_t size=0;
        size<Mesh::MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes+Mesh::MeshV1SecuritySuite::AuthenticationTagBytes;
        ++size)
        assert(!Mesh::MeshV1ProtectedFrameCodec::DecodeHop(bytes.data(),size,hop,hopView));

    return 0;
}
