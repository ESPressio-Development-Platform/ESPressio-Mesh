#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Technology-independent outcome of one attempted Mesh forwarding route.</summary>
enum class RouteAttemptOutcome : std::uint8_t {
    Delivered,
    RetryableFailure,
    RouteUnavailable,
    ResourceUnavailable,
    DeadlineExpired,
    PermanentFailure
};

/// <summary>Read-only attempt counters supplied to route/retry policy.</summary>
struct RouteAttemptEvidence final {
    std::uint8_t AttemptsOnCurrentRoute{0};
    std::uint8_t DistinctRoutesAttempted{0};
    RouteAttemptOutcome LastOutcome{RouteAttemptOutcome::RouteUnavailable};
};

/// <summary>Injected policy deciding whether the same already-selected route may be attempted again.</summary>
class IRouteAttemptPolicy {
public:
    virtual ~IRouteAttemptPolicy() = default;
    virtual bool ShouldRetryCurrentRoute(const RouteAttemptEvidence& evidence) const noexcept = 0;
};

/// <summary>Injected policy deciding whether Mesh should seek a distinct route after an unsuccessful route.</summary>
class IRetryPolicy {
public:
    virtual ~IRetryPolicy() = default;
    virtual bool ShouldTryAnotherRoute(const RouteAttemptEvidence& evidence) const noexcept = 0;
};

/// <summary>
/// Default bounded route-attempt policy enforcing the frozen maximum of three attempts on one selected route.
/// </summary>
class DefaultRouteAttemptPolicy final : public IRouteAttemptPolicy {
public:
    bool ShouldRetryCurrentRoute(const RouteAttemptEvidence& evidence) const noexcept override {
        if (evidence.LastOutcome != RouteAttemptOutcome::RetryableFailure &&
            evidence.LastOutcome != RouteAttemptOutcome::ResourceUnavailable) return false;
        return evidence.AttemptsOnCurrentRoute < Limits::MaxSameRouteAttempts;
    }
};

/// <summary>
/// Default bounded retry policy enforcing the frozen maximum of four distinct attempted routes.
/// </summary>
class DefaultRetryPolicy final : public IRetryPolicy {
public:
    bool ShouldTryAnotherRoute(const RouteAttemptEvidence& evidence) const noexcept override {
        if (evidence.LastOutcome == RouteAttemptOutcome::Delivered ||
            evidence.LastOutcome == RouteAttemptOutcome::DeadlineExpired ||
            evidence.LastOutcome == RouteAttemptOutcome::PermanentFailure) return false;
        return evidence.DistinctRoutesAttempted < Limits::MaxRoutesAttempted;
    }
};

} // namespace ESPressio::Mesh
