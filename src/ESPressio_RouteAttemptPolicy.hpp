#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Technology-independent outcome of one attempted Mesh forwarding route.</summary>
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
class RouteAttemptOutcome : std::uint8_t {
    Delivered,
    RetryableFailure,
    RouteUnavailable,
    ResourceUnavailable,
    DeadlineExpired,
    PermanentFailure
};

/// <summary>Read-only attempt counters supplied to route/retry policy.</summary>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
struct RouteAttemptEvidence final {
    std::uint8_t AttemptsOnCurrentRoute{0};
    std::uint8_t DistinctRoutesAttempted{0};
    RouteAttemptOutcome LastOutcome{RouteAttemptOutcome::RouteUnavailable};
};

/// <summary>Injected policy deciding whether the same already-selected route may be attempted again.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic interface/object includes vptr storage where not supplied by a base.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class IRouteAttemptPolicy {
public:
    virtual ~IRouteAttemptPolicy() = default;
    virtual bool ShouldRetryCurrentRoute(const RouteAttemptEvidence& evidence) const noexcept = 0;
};

/// <summary>Injected policy deciding whether Mesh should seek a distinct route after an unsuccessful route.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic interface/object includes vptr storage where not supplied by a base.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
class IRetryPolicy {
public:
    virtual ~IRetryPolicy() = default;
    virtual bool ShouldTryAnotherRoute(const RouteAttemptEvidence& evidence) const noexcept = 0;
};

/// <summary>
/// Default bounded route-attempt policy enforcing the frozen maximum of three attempts on one selected route.
/// </summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
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
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
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
