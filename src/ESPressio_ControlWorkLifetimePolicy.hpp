#pragma once

#include <cstdint>
#include <limits>

#include "ESPressio_MeshRelayCapacity.hpp"
#include "ESPressio_MeshRemainingResidence.hpp"

namespace ESPressio::Mesh {

/// <summary>Injectable finite local lifetime policy for trusted Mesh control work in the final neutral service taxonomy.</summary>
/// <remarks>
/// This policy owns operational retention/deadline configuration only. It does not select a service class, grant
/// authorization or define wire semantics. The caller supplies the already-trusted MeshRelayServiceClass chosen by the
/// owning control/admission policy. Every configured lifetime is finite and non-zero; there is no universal platform
/// timeout and no predecessor GeneralControl/Application bucket.
/// </remarks>
class IControlWorkLifetimePolicy {
public:
    virtual ~IControlWorkLifetimePolicy() = default;
    virtual std::uint64_t LifetimeMilliseconds(MeshRelayServiceClass serviceClass) const noexcept = 0;
};

/// <summary>Explicit six-class control-work lifetime profile configured by the composition root.</summary>
class FixedControlWorkLifetimePolicy final : public IControlWorkLifetimePolicy {
    std::uint64_t _infrastructureMilliseconds{0};
    std::uint64_t _clockMilliseconds{0};
    std::uint64_t _criticalMilliseconds{0};
    std::uint64_t _responsiveMilliseconds{0};
    std::uint64_t _convergentMilliseconds{0};
    std::uint64_t _bestEffortMilliseconds{0};

public:
    constexpr FixedControlWorkLifetimePolicy(
        std::uint64_t infrastructureMilliseconds,
        std::uint64_t clockMilliseconds,
        std::uint64_t criticalMilliseconds,
        std::uint64_t responsiveMilliseconds,
        std::uint64_t convergentMilliseconds,
        std::uint64_t bestEffortMilliseconds
    ) noexcept :
        _infrastructureMilliseconds(infrastructureMilliseconds),
        _clockMilliseconds(clockMilliseconds),
        _criticalMilliseconds(criticalMilliseconds),
        _responsiveMilliseconds(responsiveMilliseconds),
        _convergentMilliseconds(convergentMilliseconds),
        _bestEffortMilliseconds(bestEffortMilliseconds) {}

    constexpr bool IsValid() const noexcept {
        return _infrastructureMilliseconds != 0U &&
               _clockMilliseconds != 0U &&
               _criticalMilliseconds != 0U &&
               _responsiveMilliseconds != 0U &&
               _convergentMilliseconds != 0U &&
               _bestEffortMilliseconds != 0U;
    }

    std::uint64_t LifetimeMilliseconds(MeshRelayServiceClass serviceClass) const noexcept override {
        switch (serviceClass) {
            case MeshRelayServiceClass::Infrastructure: return _infrastructureMilliseconds;
            case MeshRelayServiceClass::Clock: return _clockMilliseconds;
            case MeshRelayServiceClass::Critical: return _criticalMilliseconds;
            case MeshRelayServiceClass::Responsive: return _responsiveMilliseconds;
            case MeshRelayServiceClass::Convergent: return _convergentMilliseconds;
            case MeshRelayServiceClass::BestEffort: return _bestEffortMilliseconds;
        }
        return 0U;
    }
};

/// <summary>Computes a saturating absolute monotonic millisecond deadline for one trusted neutral service class.</summary>
inline bool TryControlWorkDeadline(
    const IControlWorkLifetimePolicy& policy,
    MeshRelayServiceClass serviceClass,
    std::uint64_t nowMilliseconds,
    std::uint64_t& deadlineMilliseconds
) noexcept {
    deadlineMilliseconds = 0U;
    if (!IsMeshRelayServiceClass(serviceClass) || nowMilliseconds == 0U) return false;
    const auto lifetime = policy.LifetimeMilliseconds(serviceClass);
    if (lifetime == 0U) return false;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    deadlineMilliseconds = lifetime > (maximum - nowMilliseconds)
        ? maximum
        : nowMilliseconds + lifetime;
    return true;
}

/// <summary>Computes the same finite deadline in the nanosecond coordinate required by managed Radio.</summary>
inline bool TryControlWorkDeadlineNanoseconds(
    const IControlWorkLifetimePolicy& policy,
    MeshRelayServiceClass serviceClass,
    std::uint64_t nowMilliseconds,
    std::uint64_t& deadlineNanoseconds
) noexcept {
    deadlineNanoseconds = 0U;
    std::uint64_t deadlineMilliseconds = 0U;
    if (!TryControlWorkDeadline(policy, serviceClass, nowMilliseconds, deadlineMilliseconds)) return false;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    deadlineNanoseconds = deadlineMilliseconds > maximum / MeshNanosecondsPerMillisecond
        ? maximum
        : deadlineMilliseconds * MeshNanosecondsPerMillisecond;
    return deadlineNanoseconds != 0U;
}

} // namespace ESPressio::Mesh
