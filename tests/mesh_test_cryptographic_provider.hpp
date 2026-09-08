#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_MeshV1Security.hpp>

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class TestCryptographicProvider final : public ESPressio::Mesh::IMeshV1CryptographicProvider {
public:
    std::size_t Releases{0};
    std::size_t Resets{0};
    bool PermitRelease{true};

    bool GenerateEphemeralKey(ESPressio::Mesh::MeshEphemeralKeyHandle&, ESPressio::Mesh::MeshEphemeralPublicKey&) noexcept override { return false; }
    bool GenerateHandshakeNonce(ESPressio::Mesh::MeshHandshakeNonce&) noexcept override { return false; }
    bool Hash(const std::uint8_t*, std::size_t, ESPressio::Mesh::MeshSecurityDigest&) noexcept override { return false; }
    bool SignIdentityDigest(const ESPressio::System::DeviceIdentifier&, const ESPressio::Mesh::MeshSecurityDigest&, ESPressio::Mesh::MeshIdentitySignature&) noexcept override { return false; }
    ESPressio::Mesh::MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(const ESPressio::System::DeviceIdentifier&, const ESPressio::Mesh::MeshSecurityDigest&, const ESPressio::Mesh::MeshIdentitySignature&) noexcept override { return ESPressio::Mesh::MeshIdentityVerificationResult::Invalid; }
    bool DeriveSession(ESPressio::Mesh::MeshEphemeralKeyHandle, const ESPressio::Mesh::MeshEphemeralPublicKey&, const ESPressio::Mesh::MeshIdentifier&, const ESPressio::Mesh::MeshSecurityChannelBinding&, const ESPressio::System::DeviceIdentifier&, const ESPressio::Mesh::MembershipIncarnation&, const ESPressio::Mesh::MeshHandshakeNonce&, const ESPressio::System::DeviceIdentifier&, const ESPressio::Mesh::MembershipIncarnation&, const ESPressio::Mesh::MeshHandshakeNonce&, const ESPressio::Mesh::MeshSecurityDigest&, ESPressio::Mesh::MeshSecuritySessionRole, ESPressio::Mesh::MeshSecuritySessionHandle&, ESPressio::Mesh::MeshSecuritySessionIdentifier&) noexcept override { return false; }
    bool Seal(ESPressio::Mesh::MeshSecuritySessionHandle, ESPressio::Mesh::MeshSecurityTrafficPurpose, std::uint64_t, const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t, std::uint8_t*, ESPressio::Mesh::MeshAuthenticationTag&) noexcept override { return false; }
    bool Open(ESPressio::Mesh::MeshSecuritySessionHandle, ESPressio::Mesh::MeshSecurityTrafficPurpose, std::uint64_t, const std::uint8_t*, std::size_t, const std::uint8_t*, std::size_t, const ESPressio::Mesh::MeshAuthenticationTag&, std::uint8_t*) noexcept override { return false; }
    bool ReleaseEphemeralKey(ESPressio::Mesh::MeshEphemeralKeyHandle) noexcept override { return true; }
    bool ReleaseSession(ESPressio::Mesh::MeshSecuritySessionHandle) noexcept override {
        ++Releases;
        return PermitRelease;
    }
    void ResetForControlledShutdown() noexcept override { ++Resets; }
};
