#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <ESPressio_DeviceIdentifier.hpp>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Frozen Mesh v1 cryptographic suite.</summary>
/// <remarks>
/// Long-term identity signatures use ECDSA P-256 with SHA-256 and fixed-width raw r||s encoding. Ephemeral agreement
/// uses ECDH P-256 with uncompressed SEC1 points. Session material uses HKDF-SHA-256; traffic protection uses
/// AES-256-GCM with a 96-bit nonce and 128-bit tag. Implementations belong in ESPressio-Security/platform composition.
/// </remarks>
struct MeshV1SecuritySuite final {
    static constexpr std::uint16_t Identifier = 0x0001U;
    static constexpr std::size_t DigestBytes = 32U;
    static constexpr std::size_t EphemeralPublicKeyBytes = 65U;
    static constexpr std::size_t HandshakeNonceBytes = 32U;
    static constexpr std::size_t IdentitySignatureBytes = 64U;
    static constexpr std::size_t TrafficKeyBytes = 32U;
    static constexpr std::size_t TrafficNonceBytes = 12U;
    static constexpr std::size_t AuthenticationTagBytes = 16U;
    static constexpr std::size_t SessionIdentifierBytes = 16U;
    static constexpr std::size_t ReplayWindowBits = 64U;
    static constexpr std::size_t DerivedDirectionalKeyCount = 4U;
    static constexpr std::size_t DerivedIvCount = 6U;
    static constexpr std::size_t DerivedConfirmationKeyCount = 2U;
    static constexpr std::size_t DerivedBytes =
        (DerivedDirectionalKeyCount + DerivedConfirmationKeyCount) * TrafficKeyBytes +
        DerivedIvCount * TrafficNonceBytes + SessionIdentifierBytes;
    inline static constexpr char SaltLabel[] = "ESPressio-Mesh-v1 salt";
    inline static constexpr char SessionLabel[] = "ESPressio-Mesh-v1 session";
};

template<std::size_t Size>
struct MeshSecurityBytes final {
    std::array<std::uint8_t, Size> Value{};

    constexpr bool IsZero() const noexcept {
        for (const auto byte : Value) if (byte != 0U) return false;
        return true;
    }
    constexpr explicit operator bool() const noexcept { return !IsZero(); }
};

using MeshSecurityDigest = MeshSecurityBytes<MeshV1SecuritySuite::DigestBytes>;
using MeshEphemeralPublicKey = MeshSecurityBytes<MeshV1SecuritySuite::EphemeralPublicKeyBytes>;
using MeshHandshakeNonce = MeshSecurityBytes<MeshV1SecuritySuite::HandshakeNonceBytes>;
using MeshIdentitySignature = MeshSecurityBytes<MeshV1SecuritySuite::IdentitySignatureBytes>;
using MeshAuthenticationTag = MeshSecurityBytes<MeshV1SecuritySuite::AuthenticationTagBytes>;
using MeshSecuritySessionIdentifier = MeshSecurityBytes<MeshV1SecuritySuite::SessionIdentifierBytes>;

enum class MeshV1SecurityMessageType : std::uint8_t {
    InitiatorHello = 1U,
    ResponderHello = 2U,
    InitiatorFinish = 3U,
    HopProtectedFrame = 4U,
    EndToEndProtectedFrame = 5U
};

struct MeshV1InitiatorHello final {
    MeshIdentifier Mesh{};
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MeshEphemeralPublicKey EphemeralPublicKey{};
    MeshHandshakeNonce Nonce{};
    MeshIdentitySignature Signature{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Device) && static_cast<bool>(Incarnation) &&
               static_cast<bool>(EphemeralPublicKey) && EphemeralPublicKey.Value[0] == 0x04U &&
               static_cast<bool>(Nonce) && static_cast<bool>(Signature);
    }
};

struct MeshV1ResponderHello final {
    MeshIdentifier Mesh{};
    System::DeviceIdentifier Device{};
    MembershipIncarnation Incarnation{};
    MeshEphemeralPublicKey EphemeralPublicKey{};
    MeshHandshakeNonce Nonce{};
    MeshSecurityDigest InitiatorHelloDigest{};
    MeshIdentitySignature Signature{};
    MeshAuthenticationTag ConfirmationTag{};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Mesh) && static_cast<bool>(Device) && static_cast<bool>(Incarnation) &&
               static_cast<bool>(EphemeralPublicKey) && EphemeralPublicKey.Value[0] == 0x04U &&
               static_cast<bool>(Nonce) && static_cast<bool>(Signature);
    }
};

struct MeshV1InitiatorFinish final {
    MeshSecurityDigest HandshakeTranscriptDigest{};
    MeshAuthenticationTag ConfirmationTag{};

    constexpr bool IsValid() const noexcept { return true; }
};

/// <summary>Canonical network-byte-order Mesh v1 handshake codec.</summary>
/// <remarks>
/// Signature input is the exact encoded common header plus the unsigned body for that hello. The responder unsigned
/// body includes SHA-256 of the complete signed InitiatorHello, binding direction and ordering without retaining an
/// unbounded transcript. ResponderHello and InitiatorFinish each confirm possession of their directional derived key
/// over SHA-256 of both signed hellos (excluding the responder confirmation tag, which is produced from that digest).
/// Decoding accepts only an exact packet length; trailing bytes, unknown types, suite changes and malformed points fail.
/// </remarks>
class MeshV1SecurityHandshakeCodec final {
    static constexpr std::array<std::uint8_t, 4> Magic{{0x45U, 0x53U, 0x4DU, 0x31U}}; // ESM1
    static constexpr std::uint8_t Version = 1U;

    static void WriteU16(std::uint8_t* output, std::uint16_t value) noexcept {
        output[0] = static_cast<std::uint8_t>(value >> 8U);
        output[1] = static_cast<std::uint8_t>(value);
    }

    static std::uint16_t ReadU16(const std::uint8_t* input) noexcept {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[0]) << 8U) | input[1]);
    }

    static bool EncodeHeader(
        MeshV1SecurityMessageType type,
        std::uint16_t bodyBytes,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (output == nullptr || outputBytes < HeaderBytes) return false;
        std::memcpy(output, Magic.data(), Magic.size());
        output[4] = Version;
        output[5] = static_cast<std::uint8_t>(type);
        WriteU16(output + 6, MeshV1SecuritySuite::Identifier);
        WriteU16(output + 8, bodyBytes);
        return true;
    }

    static bool DecodeHeader(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1SecurityMessageType expectedType,
        std::uint16_t expectedBodyBytes
    ) noexcept {
        return input != nullptr && inputBytes == HeaderBytes + expectedBodyBytes &&
               std::memcmp(input, Magic.data(), Magic.size()) == 0 && input[4] == Version &&
               input[5] == static_cast<std::uint8_t>(expectedType) &&
               ReadU16(input + 6) == MeshV1SecuritySuite::Identifier &&
               ReadU16(input + 8) == expectedBodyBytes;
    }

    static void Copy(std::uint8_t*& output, const std::uint8_t* input, std::size_t size) noexcept {
        std::memcpy(output, input, size);
        output += size;
    }

    static void Read(const std::uint8_t*& input, std::uint8_t* output, std::size_t size) noexcept {
        std::memcpy(output, input, size);
        input += size;
    }

public:
    static constexpr std::size_t HeaderBytes = 10U;
    static constexpr std::size_t InitiatorUnsignedBodyBytes = 145U;
    static constexpr std::size_t InitiatorBodyBytes =
        InitiatorUnsignedBodyBytes + MeshV1SecuritySuite::IdentitySignatureBytes;
    static constexpr std::size_t InitiatorPacketBytes = HeaderBytes + InitiatorBodyBytes;
    static constexpr std::size_t ResponderUnsignedBodyBytes = 177U;
    static constexpr std::size_t ResponderBodyBytes =
        ResponderUnsignedBodyBytes + MeshV1SecuritySuite::IdentitySignatureBytes +
        MeshV1SecuritySuite::AuthenticationTagBytes;
    static constexpr std::size_t ResponderPacketBytes = HeaderBytes + ResponderBodyBytes;
    static constexpr std::size_t FinishBodyBytes =
        MeshV1SecuritySuite::DigestBytes + MeshV1SecuritySuite::AuthenticationTagBytes;
    static constexpr std::size_t FinishPacketBytes = HeaderBytes + FinishBodyBytes;

    static bool EncodeInitiatorUnsigned(
        const MeshV1InitiatorHello& hello,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (!static_cast<bool>(hello.Mesh) || !static_cast<bool>(hello.Device) ||
            !static_cast<bool>(hello.Incarnation) || !static_cast<bool>(hello.EphemeralPublicKey) ||
            hello.EphemeralPublicKey.Value[0] != 0x04U || !static_cast<bool>(hello.Nonce) ||
            outputBytes != HeaderBytes + InitiatorUnsignedBodyBytes ||
            !EncodeHeader(MeshV1SecurityMessageType::InitiatorHello, InitiatorBodyBytes, output, outputBytes)) return false;
        auto* cursor = output + HeaderBytes;
        Copy(cursor, hello.Mesh.Bytes().data(), hello.Mesh.Bytes().size());
        Copy(cursor, hello.Device.Bytes().data(), hello.Device.Bytes().size());
        Copy(cursor, hello.Incarnation.Bytes().data(), hello.Incarnation.Bytes().size());
        Copy(cursor, hello.EphemeralPublicKey.Value.data(), hello.EphemeralPublicKey.Value.size());
        Copy(cursor, hello.Nonce.Value.data(), hello.Nonce.Value.size());
        return true;
    }

    static bool EncodeInitiator(
        const MeshV1InitiatorHello& hello,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (!hello.IsValid() || outputBytes != InitiatorPacketBytes ||
            !EncodeInitiatorUnsigned(hello, output, HeaderBytes + InitiatorUnsignedBodyBytes)) return false;
        std::memcpy(output + HeaderBytes + InitiatorUnsignedBodyBytes,
                    hello.Signature.Value.data(), hello.Signature.Value.size());
        return true;
    }

    static bool DecodeInitiator(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1InitiatorHello& hello
    ) noexcept {
        hello = {};
        if (!DecodeHeader(input, inputBytes, MeshV1SecurityMessageType::InitiatorHello, InitiatorBodyBytes)) return false;
        const auto* cursor = input + HeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage device{};
        MembershipIncarnation::Storage incarnation{};
        Read(cursor, mesh.data(), mesh.size());
        Read(cursor, device.data(), device.size());
        Read(cursor, incarnation.data(), incarnation.size());
        hello.Mesh = MeshIdentifier{mesh};
        hello.Device = System::DeviceIdentifier{device};
        hello.Incarnation = MembershipIncarnation{incarnation};
        Read(cursor, hello.EphemeralPublicKey.Value.data(), hello.EphemeralPublicKey.Value.size());
        Read(cursor, hello.Nonce.Value.data(), hello.Nonce.Value.size());
        Read(cursor, hello.Signature.Value.data(), hello.Signature.Value.size());
        if (!hello.IsValid()) hello = {};
        return hello.IsValid();
    }

    static bool EncodeResponderUnsigned(
        const MeshV1ResponderHello& hello,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (!static_cast<bool>(hello.Mesh) || !static_cast<bool>(hello.Device) ||
            !static_cast<bool>(hello.Incarnation) || !static_cast<bool>(hello.EphemeralPublicKey) ||
            hello.EphemeralPublicKey.Value[0] != 0x04U || !static_cast<bool>(hello.Nonce) ||
            outputBytes != HeaderBytes + ResponderUnsignedBodyBytes ||
            !EncodeHeader(MeshV1SecurityMessageType::ResponderHello, ResponderBodyBytes, output, outputBytes)) return false;
        auto* cursor = output + HeaderBytes;
        Copy(cursor, hello.Mesh.Bytes().data(), hello.Mesh.Bytes().size());
        Copy(cursor, hello.Device.Bytes().data(), hello.Device.Bytes().size());
        Copy(cursor, hello.Incarnation.Bytes().data(), hello.Incarnation.Bytes().size());
        Copy(cursor, hello.EphemeralPublicKey.Value.data(), hello.EphemeralPublicKey.Value.size());
        Copy(cursor, hello.Nonce.Value.data(), hello.Nonce.Value.size());
        Copy(cursor, hello.InitiatorHelloDigest.Value.data(), hello.InitiatorHelloDigest.Value.size());
        return true;
    }

    static bool EncodeResponder(
        const MeshV1ResponderHello& hello,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (!hello.IsValid() || outputBytes != ResponderPacketBytes ||
            !EncodeResponderUnsigned(hello, output, HeaderBytes + ResponderUnsignedBodyBytes)) return false;
        std::memcpy(output + HeaderBytes + ResponderUnsignedBodyBytes,
                    hello.Signature.Value.data(), hello.Signature.Value.size());
        std::memcpy(output + HeaderBytes + ResponderUnsignedBodyBytes + hello.Signature.Value.size(),
                    hello.ConfirmationTag.Value.data(), hello.ConfirmationTag.Value.size());
        return true;
    }

    static bool DecodeResponder(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1ResponderHello& hello
    ) noexcept {
        hello = {};
        if (!DecodeHeader(input, inputBytes, MeshV1SecurityMessageType::ResponderHello, ResponderBodyBytes)) return false;
        const auto* cursor = input + HeaderBytes;
        MeshIdentifier::Storage mesh{};
        System::DeviceIdentifier::Storage device{};
        MembershipIncarnation::Storage incarnation{};
        Read(cursor, mesh.data(), mesh.size());
        Read(cursor, device.data(), device.size());
        Read(cursor, incarnation.data(), incarnation.size());
        hello.Mesh = MeshIdentifier{mesh};
        hello.Device = System::DeviceIdentifier{device};
        hello.Incarnation = MembershipIncarnation{incarnation};
        Read(cursor, hello.EphemeralPublicKey.Value.data(), hello.EphemeralPublicKey.Value.size());
        Read(cursor, hello.Nonce.Value.data(), hello.Nonce.Value.size());
        Read(cursor, hello.InitiatorHelloDigest.Value.data(), hello.InitiatorHelloDigest.Value.size());
        Read(cursor, hello.Signature.Value.data(), hello.Signature.Value.size());
        Read(cursor, hello.ConfirmationTag.Value.data(), hello.ConfirmationTag.Value.size());
        if (!hello.IsValid()) hello = {};
        return hello.IsValid();
    }

    static bool EncodeFinish(
        const MeshV1InitiatorFinish& finish,
        std::uint8_t* output,
        std::size_t outputBytes
    ) noexcept {
        if (!finish.IsValid() || outputBytes != FinishPacketBytes ||
            !EncodeHeader(MeshV1SecurityMessageType::InitiatorFinish, FinishBodyBytes, output, outputBytes)) return false;
        auto* cursor = output + HeaderBytes;
        Copy(cursor, finish.HandshakeTranscriptDigest.Value.data(), finish.HandshakeTranscriptDigest.Value.size());
        Copy(cursor, finish.ConfirmationTag.Value.data(), finish.ConfirmationTag.Value.size());
        return true;
    }

    static bool DecodeFinish(
        const std::uint8_t* input,
        std::size_t inputBytes,
        MeshV1InitiatorFinish& finish
    ) noexcept {
        finish = {};
        if (!DecodeHeader(input, inputBytes, MeshV1SecurityMessageType::InitiatorFinish, FinishBodyBytes)) return false;
        const auto* cursor = input + HeaderBytes;
        Read(cursor, finish.HandshakeTranscriptDigest.Value.data(), finish.HandshakeTranscriptDigest.Value.size());
        Read(cursor, finish.ConfirmationTag.Value.data(), finish.ConfirmationTag.Value.size());
        if (!finish.IsValid()) finish = {};
        return finish.IsValid();
    }
};

struct MeshEphemeralKeyHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
};

struct MeshSecuritySessionHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
};

enum class MeshIdentityVerificationResult : std::uint8_t {
    Verified,
    Unregistered,
    InvalidSignature,
    ResourceUnavailable,
    Invalid
};

enum class MeshSecuritySessionRole : std::uint8_t { Initiator, Responder };
enum class MeshSecurityTrafficPurpose : std::uint8_t { Hop, EndToEnd, KeyConfirmation };

/// <summary>Injected bounded implementation of the frozen Mesh v1 suite.</summary>
/// <remarks>
/// The provider resolves provisioned long-term public/private identity keys by DeviceIdentifier; transmitted identity
/// values never become credentials. DeriveSession performs ECDH then HKDF-Extract/Expand with both authenticated nonces,
/// MeshIdentifier, ordered device/incarnation identities, role and complete signed-hello transcript digest as context.
/// The exact KDF is HKDF-Extract(SHA-256(SaltLabel || initiatorNonce || responderNonce), ECDH-x-coordinate), followed by
/// HKDF-Expand with SessionLabel || MeshIdentifier || initiator DeviceIdentifier || initiator MembershipIncarnation ||
/// responder DeviceIdentifier || responder MembershipIncarnation || signedHelloTranscriptDigest. Its 280 output bytes
/// are consumed in this order: initiator-to-responder Hop key, responder-to-initiator Hop key,
/// initiator-to-responder EndToEnd key, responder-to-initiator EndToEnd key, initiator confirmation key, responder
/// confirmation key, the six corresponding base IVs in the same order, then the 16-byte session identifier.
/// The provider retains those values behind the returned opaque handle.
/// Seal/Open construct each 96-bit GCM nonce by XORing the purpose/direction base IV with the big-endian 64-bit sequence.
/// A sequence is never zero or reused; exhaustion requires a new handshake. Open must authenticate before replay state is
/// committed. ResponderHello confirmation and InitiatorFinish use their directional KeyConfirmation material, sequence
/// 1, zero plaintext and the complete signed-hello transcript digest as AAD. Providers must validate received P-256
/// points and canonical low-S raw ECDSA signatures. Release methods
/// synchronously erase provider-owned secret material and make handles stale.
/// </remarks>
class IMeshV1CryptographicProvider {
public:
    virtual ~IMeshV1CryptographicProvider() = default;

    virtual bool GenerateEphemeralKey(
        MeshEphemeralKeyHandle& handle,
        MeshEphemeralPublicKey& publicKey
    ) noexcept = 0;
    virtual bool Hash(const std::uint8_t* bytes, std::size_t size, MeshSecurityDigest& digest) noexcept = 0;
    virtual bool SignIdentityDigest(
        const System::DeviceIdentifier& localDevice,
        const MeshSecurityDigest& digest,
        MeshIdentitySignature& signature
    ) noexcept = 0;
    virtual MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(
        const System::DeviceIdentifier& claimedDevice,
        const MeshSecurityDigest& digest,
        const MeshIdentitySignature& signature
    ) noexcept = 0;
    virtual bool DeriveSession(
        MeshEphemeralKeyHandle localEphemeral,
        const MeshEphemeralPublicKey& peerEphemeral,
        const MeshIdentifier& mesh,
        const System::DeviceIdentifier& initiatorDevice,
        const MembershipIncarnation& initiatorIncarnation,
        const MeshHandshakeNonce& initiatorNonce,
        const System::DeviceIdentifier& responderDevice,
        const MembershipIncarnation& responderIncarnation,
        const MeshHandshakeNonce& responderNonce,
        const MeshSecurityDigest& signedHelloTranscriptDigest,
        MeshSecuritySessionRole role,
        MeshSecuritySessionHandle& session,
        MeshSecuritySessionIdentifier& sessionIdentifier
    ) noexcept = 0;
    virtual bool Seal(
        MeshSecuritySessionHandle session,
        MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence,
        const std::uint8_t* authenticatedData,
        std::size_t authenticatedDataBytes,
        const std::uint8_t* plaintext,
        std::size_t plaintextBytes,
        std::uint8_t* ciphertext,
        MeshAuthenticationTag& tag
    ) noexcept = 0;
    virtual bool Open(
        MeshSecuritySessionHandle session,
        MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence,
        const std::uint8_t* authenticatedData,
        std::size_t authenticatedDataBytes,
        const std::uint8_t* ciphertext,
        std::size_t ciphertextBytes,
        const MeshAuthenticationTag& tag,
        std::uint8_t* plaintext
    ) noexcept = 0;
    virtual bool ReleaseEphemeralKey(MeshEphemeralKeyHandle handle) noexcept = 0;
    virtual bool ReleaseSession(MeshSecuritySessionHandle handle) noexcept = 0;
    virtual void ResetForControlledShutdown() noexcept = 0;
};

static_assert(MeshV1SecurityHandshakeCodec::InitiatorPacketBytes == 219U);
static_assert(MeshV1SecurityHandshakeCodec::ResponderPacketBytes == 267U);
static_assert(MeshV1SecurityHandshakeCodec::FinishPacketBytes == 58U);
static_assert(MeshV1SecuritySuite::DerivedBytes == 280U);

} // namespace ESPressio::Mesh
