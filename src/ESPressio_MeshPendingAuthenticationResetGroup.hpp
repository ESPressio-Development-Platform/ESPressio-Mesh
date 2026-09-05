#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshSecurityAuthority.hpp"

namespace ESPressio::Mesh {

enum class PendingAuthenticationResetRegistrationResult : std::uint8_t {
    Registered, AlreadyRegistered, ResourceUnavailable, Invalid
};

/// <summary>Fixed composition-time group for independently bounded handshake-direction owners.</summary>
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
