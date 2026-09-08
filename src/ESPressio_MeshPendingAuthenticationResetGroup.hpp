#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshSecurityAuthority.hpp"

namespace ESPressio::Mesh {

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
enum
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 1 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class PendingAuthenticationResetRegistrationResult : std::uint8_t {
    Registered, AlreadyRegistered, ResourceUnavailable, Invalid
};

/// <summary>Fixed composition-time group for independently bounded handshake-direction owners.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
template<std::size_t Capacity>
class MeshPendingAuthenticationResetGroup final : public IMeshPendingAuthenticationReset {
    static_assert(Capacity > 0U, "Pending-authentication reset participant capacity must be non-zero.");
    std::array<IMeshPendingAuthenticationReset*, Capacity> _participants{};

public:
    PendingAuthenticationResetRegistrationResult Register(IMeshPendingAuthenticationReset& participant) noexcept {
        for (const auto* retained : _participants) {
            if (retained == &participant) return PendingAuthenticationResetRegistrationResult::AlreadyRegistered;
        }
        for (auto& retained : _participants) {
            if (retained != nullptr) continue;
            retained = &participant;
            return PendingAuthenticationResetRegistrationResult::Registered;
        }
        return PendingAuthenticationResetRegistrationResult::ResourceUnavailable;
    }

    bool ReleasePendingAuthenticationBeforeProviderReset() noexcept override {
        bool releasedAll = true;
        for (auto* participant : _participants) {
            if (participant != nullptr && !participant->ReleasePendingAuthenticationBeforeProviderReset()) {
                releasedAll = false;
            }
        }
        return releasedAll;
    }

    void ClearPendingAuthenticationAfterProviderReset() noexcept override {
        for (auto* participant : _participants) {
            if (participant != nullptr) participant->ClearPendingAuthenticationAfterProviderReset();
        }
    }
};

} // namespace ESPressio::Mesh
