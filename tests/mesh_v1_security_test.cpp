#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_MeshSecuritySessionTable.hpp>
#include <ESPressio_MeshV1ProtectedFrame.hpp>

#include "mesh_test_cryptographic_provider.hpp"

using namespace ESPressio;

template<std::size_t Size>
static std::array<std::uint8_t, Size> Bytes(std::uint8_t first) {
    std::array<std::uint8_t, Size> bytes{};
    for (std::size_t index = 0; index < Size; ++index) {
        bytes[index] = static_cast<std::uint8_t>(first + index);
    }
    return bytes;
}

int main() {
    Mesh::MeshV1InitiatorHello initiator{};
    initiator.Mesh = Mesh::MeshIdentifier{Bytes<16>(1)};
    initiator.Device = System::DeviceIdentifier{Bytes<16>(17)};
    initiator.Incarnation = Mesh::MembershipIncarnation{Bytes<16>(33)};
    initiator.EphemeralPublicKey.Value = Bytes<Mesh::MeshV1SecuritySuite::EphemeralPublicKeyBytes>(49);
    initiator.EphemeralPublicKey.Value[0] = 0x04U;
    initiator.Nonce.Value = Bytes<Mesh::MeshV1SecuritySuite::HandshakeNonceBytes>(65);
    initiator.Signature.Value = Bytes<Mesh::MeshV1SecuritySuite::IdentitySignatureBytes>(97);
    assert(initiator.IsValid());

    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::InitiatorPacketBytes> initiatorWire{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeInitiator(
        initiator, initiatorWire.data(), initiatorWire.size()));
    assert(initiatorWire[0] == 0x45U && initiatorWire[1] == 0x53U &&
           initiatorWire[2] == 0x4DU && initiatorWire[3] == 0x31U);
    assert(initiatorWire[4] == 1U);
    assert(initiatorWire[5] == static_cast<std::uint8_t>(Mesh::MeshV1SecurityMessageType::InitiatorHello));
    assert(initiatorWire[6] == 0U && initiatorWire[7] == 1U);
    assert(initiatorWire[8] == 0U && initiatorWire[9] == 209U);

    Mesh::MeshV1InitiatorHello decodedInitiator{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::DecodeInitiator(
        initiatorWire.data(), initiatorWire.size(), decodedInitiator));
    assert(decodedInitiator.Mesh == initiator.Mesh);
    assert(decodedInitiator.Device == initiator.Device);
    assert(decodedInitiator.Incarnation == initiator.Incarnation);
    assert(decodedInitiator.EphemeralPublicKey.Value == initiator.EphemeralPublicKey.Value);
    assert(decodedInitiator.Nonce.Value == initiator.Nonce.Value);
    assert(decodedInitiator.Signature.Value == initiator.Signature.Value);

    std::array<std::uint8_t,
        Mesh::MeshV1SecurityHandshakeCodec::HeaderBytes +
        Mesh::MeshV1SecurityHandshakeCodec::InitiatorUnsignedBodyBytes> initiatorUnsigned{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeInitiatorUnsigned(
        initiator, initiatorUnsigned.data(), initiatorUnsigned.size()));
    assert(initiatorUnsigned.back() == initiator.Nonce.Value.back());

    auto malformed = initiatorWire;
    malformed[7] = 2U;
    assert(!Mesh::MeshV1SecurityHandshakeCodec::DecodeInitiator(
        malformed.data(), malformed.size(), decodedInitiator));
    assert(!decodedInitiator.IsValid());
    assert(!Mesh::MeshV1SecurityHandshakeCodec::DecodeInitiator(
        initiatorWire.data(), initiatorWire.size() - 1U, decodedInitiator));

    Mesh::MeshV1ResponderHello responder{};
    responder.Mesh = initiator.Mesh;
    responder.Device = System::DeviceIdentifier{Bytes<16>(2)};
    responder.Incarnation = Mesh::MembershipIncarnation{Bytes<16>(3)};
    responder.EphemeralPublicKey.Value = Bytes<Mesh::MeshV1SecuritySuite::EphemeralPublicKeyBytes>(4);
    responder.EphemeralPublicKey.Value[0] = 0x04U;
    responder.Nonce.Value = Bytes<Mesh::MeshV1SecuritySuite::HandshakeNonceBytes>(5);
    responder.InitiatorHelloDigest.Value = Bytes<Mesh::MeshV1SecuritySuite::DigestBytes>(6);
    responder.Signature.Value = Bytes<Mesh::MeshV1SecuritySuite::IdentitySignatureBytes>(7);
    responder.ConfirmationTag.Value = Bytes<Mesh::MeshV1SecuritySuite::AuthenticationTagBytes>(8);
    assert(responder.IsValid());

    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::ResponderPacketBytes> responderWire{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeResponder(
        responder, responderWire.data(), responderWire.size()));
    Mesh::MeshV1ResponderHello decodedResponder{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::DecodeResponder(
        responderWire.data(), responderWire.size(), decodedResponder));
    assert(decodedResponder.Mesh == responder.Mesh);
    assert(decodedResponder.Device == responder.Device);
    assert(decodedResponder.InitiatorHelloDigest.Value == responder.InitiatorHelloDigest.Value);
    assert(decodedResponder.ConfirmationTag.Value == responder.ConfirmationTag.Value);

    Mesh::MeshV1InitiatorFinish finish{};
    finish.HandshakeTranscriptDigest.Value = Bytes<Mesh::MeshV1SecuritySuite::DigestBytes>(8);
    finish.ConfirmationTag.Value = Bytes<Mesh::MeshV1SecuritySuite::AuthenticationTagBytes>(9);
    std::array<std::uint8_t, Mesh::MeshV1SecurityHandshakeCodec::FinishPacketBytes> finishWire{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::EncodeFinish(finish, finishWire.data(), finishWire.size()));
    Mesh::MeshV1InitiatorFinish decodedFinish{};
    assert(Mesh::MeshV1SecurityHandshakeCodec::DecodeFinish(
        finishWire.data(), finishWire.size(), decodedFinish));
    assert(decodedFinish.HandshakeTranscriptDigest.Value == finish.HandshakeTranscriptDigest.Value);
    assert(decodedFinish.ConfirmationTag.Value == finish.ConfirmationTag.Value);

    TestCryptographicProvider provider;
    Mesh::MeshSecuritySessionTable<1> sessions;
    Mesh::MeshSecuritySessionIdentifier sessionIdentifier{};
    sessionIdentifier.Value = Bytes<Mesh::MeshV1SecuritySuite::SessionIdentifierBytes>(10);
    Mesh::MeshSecuritySessionRecordHandle session{};
    assert(sessions.Install(
        initiator.Device,
        initiator.Incarnation,
        sessionIdentifier,
        Mesh::MeshSecuritySessionHandle{0, 1},
        provider,
        session));
    assert(session);
    assert(sessions.Find(initiator.Device, initiator.Incarnation).Generation == session.Generation);
    assert(sessions.IssueSequence(session, Mesh::MeshSecurityTrafficPurpose::Hop) == 1U);
    assert(sessions.IssueSequence(session, Mesh::MeshSecurityTrafficPurpose::EndToEnd) == 1U);
    assert(sessions.CanAcceptInbound(session, Mesh::MeshSecurityTrafficPurpose::Hop, 3U));
    assert(sessions.CommitAuthenticatedInbound(session, Mesh::MeshSecurityTrafficPurpose::Hop, 3U));
    assert(sessions.CanAcceptInbound(session, Mesh::MeshSecurityTrafficPurpose::Hop, 2U));
    assert(sessions.CommitAuthenticatedInbound(session, Mesh::MeshSecurityTrafficPurpose::Hop, 2U));
    assert(!sessions.CanAcceptInbound(session, Mesh::MeshSecurityTrafficPurpose::Hop, 2U));
    assert(!sessions.CanAcceptInbound(session, Mesh::MeshSecurityTrafficPurpose::Hop, 0U));
    assert(sessions.CanAcceptInbound(session, Mesh::MeshSecurityTrafficPurpose::EndToEnd, 2U));

    const auto stale = session;
    assert(sessions.Release(session, provider));
    assert(provider.Releases == 1U);
    assert(!sessions.ProviderSession(stale));
    assert(!sessions.CanAcceptInbound(stale, Mesh::MeshSecurityTrafficPurpose::Hop, 4U));

    Mesh::MeshV1EndToEndFrameHeader endToEnd{};
    endToEnd.Mesh = initiator.Mesh;
    endToEnd.Session = sessionIdentifier;
    endToEnd.Sequence = 7U;
    endToEnd.Source = initiator.Device;
    endToEnd.SourceIncarnation = initiator.Incarnation;
    endToEnd.Destination = responder.Device;
    endToEnd.DestinationIncarnation = responder.Incarnation;
    endToEnd.MessageId = 11U;
    endToEnd.PrimitiveFamily = Primitive::FamilyIds::Event;
    endToEnd.PrimitiveVersion = 2U;
    endToEnd.PlaintextBytes = 4U;
    const std::array<std::uint8_t, 4> protectedPayload{{1, 2, 3, 4}};
    std::array<std::uint8_t, 148> endToEndWire{};
    static_assert(endToEndWire.size() ==
        Mesh::MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes + 4U +
        Mesh::MeshV1SecuritySuite::AuthenticationTagBytes);
    assert(Mesh::MeshV1ProtectedFrameCodec::EncodeEndToEndAuthenticatedHeader(
        endToEnd, endToEndWire.data(), endToEndWire.size()));
    std::copy(protectedPayload.begin(), protectedPayload.end(),
              endToEndWire.begin() + Mesh::MeshV1ProtectedFrameCodec::EndToEndAuthenticatedHeaderBytes);
    std::fill(endToEndWire.end() - Mesh::MeshV1SecuritySuite::AuthenticationTagBytes,
              endToEndWire.end(), 0xA5U);
    Mesh::MeshV1EndToEndFrameHeader decodedEndToEnd{};
    Mesh::MeshV1ProtectedFrameView endToEndView{};
    assert(Mesh::MeshV1ProtectedFrameCodec::DecodeEndToEnd(
        endToEndWire.data(), endToEndWire.size(), decodedEndToEnd, endToEndView));
    assert(decodedEndToEnd.Source == endToEnd.Source);
    assert(decodedEndToEnd.Destination == endToEnd.Destination);
    assert(decodedEndToEnd.MessageId == endToEnd.MessageId);
    assert(decodedEndToEnd.PrimitiveFamily == Primitive::FamilyIds::Event);
    assert(endToEndView.CiphertextBytes == protectedPayload.size());
    assert(endToEndView.Ciphertext[0] == 1U && endToEndView.Ciphertext[3] == 4U);

    Mesh::MeshV1HopFrameHeader hop{};
    hop.Mesh = initiator.Mesh;
    hop.Session = sessionIdentifier;
    hop.Sequence = 12U;
    hop.Sender = initiator.Device;
    hop.SenderIncarnation = initiator.Incarnation;
    hop.NextHop = responder.Device;
    hop.NextHopIncarnation = responder.Incarnation;
    hop.Destination = responder.Device;
    hop.DestinationIncarnation = responder.Incarnation;
    hop.MessageId = endToEnd.MessageId;
    hop.HopLimit = 16U;
    hop.InnerFrameBytes = static_cast<std::uint16_t>(endToEndWire.size());
    std::array<std::uint8_t, 321> hopWire{};
    static_assert(hopWire.size() == Mesh::MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes +
        endToEndWire.size() + Mesh::MeshV1SecuritySuite::AuthenticationTagBytes);
    assert(Mesh::MeshV1ProtectedFrameCodec::EncodeHopAuthenticatedHeader(
        hop, hopWire.data(), hopWire.size()));
    std::copy(endToEndWire.begin(), endToEndWire.end(),
              hopWire.begin() + Mesh::MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes);
    std::fill(hopWire.end() - Mesh::MeshV1SecuritySuite::AuthenticationTagBytes, hopWire.end(), 0x5AU);
    Mesh::MeshV1HopFrameHeader decodedHop{};
    Mesh::MeshV1ProtectedFrameView hopView{};
    assert(Mesh::MeshV1ProtectedFrameCodec::DecodeHop(
        hopWire.data(), hopWire.size(), decodedHop, hopView));
    assert(decodedHop.Sender == hop.Sender && decodedHop.NextHop == hop.NextHop);
    assert(decodedHop.Destination == hop.Destination && decodedHop.MessageId == hop.MessageId);
    assert(decodedHop.HopLimit == 16U && hopView.CiphertextBytes == endToEndWire.size());
    auto badHop = hopWire;
    badHop[Mesh::MeshV1ProtectedFrameCodec::HopAuthenticatedHeaderBytes - 1U] ^= 1U;
    assert(!Mesh::MeshV1ProtectedFrameCodec::DecodeHop(
        badHop.data(), badHop.size(), decodedHop, hopView));

    return 0;
}
