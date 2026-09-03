#pragma once

#include <cstdint>

#include "ESPressio_RouteAttemptPolicy.hpp"

namespace ESPressio::Mesh {

/// <summary>Action selected after one route attempt outcome.</summary>
enum class RouteAttemptAction : std::uint8_t {
    Complete,
    RetryCurrentRoute,
    ReplanDistinctRoute,
    StopDeadlineExpired,
    StopPermanentFailure,
    StopAttemptLimit
};

/// <summary>
/// Bounded per-delivery route-attempt state. It owns counters only; payload, route, Radio work and scheduling remain elsewhere.
/// </summary>
class RouteAttemptCoordinator final {
    const IRouteAttemptPolicy& _routePolicy;
    const IRetryPolicy& _retryPolicy;
    std::uint8_t _attemptsOnCurrentRoute{0};
    std::uint8_t _distinctRoutesAttempted{0};

public:
    RouteAttemptCoordinator(const IRouteAttemptPolicy& routePolicy, const IRetryPolicy& retryPolicy) noexcept
        : _routePolicy(routePolicy), _retryPolicy(retryPolicy) {}

    constexpr std::uint8_t AttemptsOnCurrentRoute() const noexcept { return _attemptsOnCurrentRoute; }
    constexpr std::uint8_t DistinctRoutesAttempted() const noexcept { return _distinctRoutesAttempted; }

    /// <summary>Begins an attempt on a newly selected distinct route.</summary>
    bool BeginDistinctRouteAttempt(std::uint64_t nowMilliseconds, std::uint64_t absoluteDeadlineMilliseconds) noexcept {
        if (absoluteDeadlineMilliseconds == 0U || nowMilliseconds >= absoluteDeadlineMilliseconds) return false;
        if (_distinctRoutesAttempted >= Limits::MaxRoutesAttempted) return false;
        ++_distinctRoutesAttempted;
        _attemptsOnCurrentRoute = 1U;
        return true;
    }

    /// <summary>Begins another attempt on the current route after policy approved retry.</summary>
    bool BeginCurrentRouteRetry(std::uint64_t nowMilliseconds, std::uint64_t absoluteDeadlineMilliseconds) noexcept {
        if (absoluteDeadlineMilliseconds == 0U || nowMilliseconds >= absoluteDeadlineMilliseconds) return false;
        if (_attemptsOnCurrentRoute == 0U || _attemptsOnCurrentRoute >= Limits::MaxSameRouteAttempts) return false;
        ++_attemptsOnCurrentRoute;
        return true;
    }

    /// <summary>
    /// Evaluates one completed route attempt against immutable deadline plus injected same-route/distinct-route policies.
    /// </summary>
    RouteAttemptAction Decide(
        RouteAttemptOutcome outcome,
        std::uint64_t nowMilliseconds,
        std::uint64_t absoluteDeadlineMilliseconds
    ) const noexcept {
        if (outcome == RouteAttemptOutcome::Delivered) return RouteAttemptAction::Complete;
        if (absoluteDeadlineMilliseconds == 0U || nowMilliseconds >= absoluteDeadlineMilliseconds ||
            outcome == RouteAttemptOutcome::DeadlineExpired) {
            return RouteAttemptAction::StopDeadlineExpired;
        }
        if (outcome == RouteAttemptOutcome::PermanentFailure) return RouteAttemptAction::StopPermanentFailure;

        const RouteAttemptEvidence evidence{_attemptsOnCurrentRoute, _distinctRoutesAttempted, outcome};
        if (_routePolicy.ShouldRetryCurrentRoute(evidence) && _attemptsOnCurrentRoute < Limits::MaxSameRouteAttempts) {
            return RouteAttemptAction::RetryCurrentRoute;
        }
        if (_retryPolicy.ShouldTryAnotherRoute(evidence) && _distinctRoutesAttempted < Limits::MaxRoutesAttempted) {
            return RouteAttemptAction::ReplanDistinctRoute;
        }
        return RouteAttemptAction::StopAttemptLimit;
    }

    /// <summary>Resets all local counters for a new conceptual delivery.</summary>
    void Reset() noexcept {
        _attemptsOnCurrentRoute = 0U;
        _distinctRoutesAttempted = 0U;
    }
};

} // namespace ESPressio::Mesh
