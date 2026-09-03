#pragma once

#include <cstdint>

#include "ESPressio_MeshTrafficGovernor.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Injectable policy supplying finite local retention/deadline lifetimes for admitted Mesh control work.
/// </summary>
/// <remarks>
/// The policy is operational local configuration and does not itself define wire semantics. A zero lifetime is
/// invalid because control work must never remain queued indefinitely. Application traffic owns its own immutable
/// delivery deadline and is therefore deliberately outside this control-work policy.
/// </remarks>
class IControlWorkLifetimePolicy {
public:
    virtual ~IControlWorkLifetimePolicy() = default;

    /// <summary>Returns the finite lifetime in milliseconds for one protected control traffic class.</summary>
    virtual std::uint64_t LifetimeMilliseconds(MeshTrafficClass trafficClass) const noexcept = 0;
};

/// <summary>
/// Explicit fixed control-work lifetime policy configured by the composition root.
/// </summary>
/// <remarks>
/// No universal timeout values are imposed here: the application/platform chooses finite values appropriate to its
/// topology, radio technology and execution budgets. Construction is valid only when every protected control class
/// has a non-zero lifetime. Application class queries return zero because application deliveries use their own deadline.
/// </remarks>
class FixedControlWorkLifetimePolicy final : public IControlWorkLifetimePolicy {
    std::uint64_t _infrastructureMilliseconds{0};
    std::uint64_t _clockMilliseconds{0};
    std::uint64_t _generalMilliseconds{0};

public:
    constexpr FixedControlWorkLifetimePolicy(
        std::uint64_t infrastructureMilliseconds,
        std::uint64_t clockMilliseconds,
        std::uint64_t generalMilliseconds
    ) noexcept :
        _infrastructureMilliseconds(infrastructureMilliseconds),
        _clockMilliseconds(clockMilliseconds),
        _generalMilliseconds(generalMilliseconds) {}

    /// <summary>Returns whether all protected control classes have finite non-zero configured lifetimes.</summary>
    constexpr bool IsValid() const noexcept {
        return _infrastructureMilliseconds != 0U &&
               _clockMilliseconds != 0U &&
               _generalMilliseconds != 0U;
    }

    std::uint64_t LifetimeMilliseconds(MeshTrafficClass trafficClass) const noexcept override {
        switch (trafficClass) {
            case MeshTrafficClass::InfrastructureResponse: return _infrastructureMilliseconds;
            case MeshTrafficClass::ClockControl: return _clockMilliseconds;
            case MeshTrafficClass::GeneralControl: return _generalMilliseconds;
            case MeshTrafficClass::Application: return 0U;
        }
        return 0U;
    }
};

/// <summary>Computes a saturating absolute monotonic deadline from a finite control-work lifetime.</summary>
inline bool TryControlWorkDeadline(
    const IControlWorkLifetimePolicy& policy,
    MeshTrafficClass trafficClass,
    std::uint64_t nowMilliseconds,
    std::uint64_t& deadlineMilliseconds
) noexcept {
    deadlineMilliseconds = 0U;
    if (trafficClass == MeshTrafficClass::Application || nowMilliseconds == 0U) return false;
    const auto lifetime = policy.LifetimeMilliseconds(trafficClass);
    if (lifetime == 0U) return false;
    const auto maximum = static_cast<std::uint64_t>(~std::uint64_t{0});
    deadlineMilliseconds = lifetime > (maximum - nowMilliseconds)
        ? maximum
        : nowMilliseconds + lifetime;
    return true;
}

} // namespace ESPressio::Mesh
