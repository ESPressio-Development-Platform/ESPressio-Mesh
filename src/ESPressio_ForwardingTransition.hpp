#pragma once

#include <cstdint>

#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Result of committing one proven successful Mesh forwarding transition.</summary>
enum class ForwardingTransitionResult : std::uint8_t {
    Committed,
    HopLimitExhausted
};

/// <summary>
/// Commits exactly one proven successful Mesh forwarding transition to RemainingHopLimit.
/// </summary>
/// <remarks>
/// Call this only after the forwarding layer has evidence that its Mesh hop transition succeeded. Planning, cache hits,
/// queue admission, RadioTransport Send acceptance and failed physical/link attempts must never call this function.
/// Keeping the mutation separate prevents asynchronous Radio admission from consuming hop budget prematurely.
/// </remarks>
inline ForwardingTransitionResult CommitSuccessfulForwardingTransition(RemainingHopLimit& remainingHopLimit) noexcept {
    if (remainingHopLimit == 0U) return ForwardingTransitionResult::HopLimitExhausted;
    --remainingHopLimit;
    return ForwardingTransitionResult::Committed;
}

} // namespace ESPressio::Mesh
