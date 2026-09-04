#pragma once

#include <cstdint>

#include "ESPressio_ForwardingSubmissionCoordinator.hpp"

namespace ESPressio::Mesh {

/// <summary>Facts currently established for one submitted Mesh forwarding transition.</summary>
/// <remarks>
/// These are local orchestration facts only. `MeshDeliveryAcknowledged` means some separately authenticated Mesh-level
/// delivery-acknowledgement mechanism has established that the next Mesh node accepted the forwarded message. This type
/// does not define or encode that acknowledgement on the wire.
/// </remarks>
struct ForwardingTransitionEvidence final {
    ForwardingSubmissionResult Submission{};
    bool MeshDeliveryAcknowledged{false};
};

/// <summary>Policy result for a forwarding transition whose Radio submission has already been attempted.</summary>
enum class ForwardingTransitionDisposition : std::uint8_t {
    AwaitingEvidence,
    Successful,
    RetryableFailure,
    ResourceUnavailable,
    DeadlineExpired,
    PermanentFailure,
    Invalid
};

/// <summary>
/// Injectable policy that decides when accumulated transport/Mesh evidence is strong enough to call one forwarding
/// transition successful.
/// </summary>
/// <remarks>
/// This boundary exists specifically to prevent Radio admission, physical transmission completion, or a technology's
/// link-layer acknowledgement from being silently promoted into Mesh delivery semantics. Implementations must not
/// weaken authentication or reinterpret a local Radio fact as an end-to-end application delivery result.
/// </remarks>
class IForwardingEvidencePolicy {
public:
    virtual ~IForwardingEvidencePolicy() = default;
    virtual ForwardingTransitionDisposition Evaluate(
        const ForwardingTransitionEvidence& evidence
    ) const noexcept = 0;
};

/// <summary>
/// Portable default policy: a hop transition succeeds only after an authenticated Mesh-level delivery acknowledgement.
/// </summary>
/// <remarks>
/// Direct-link completion/acknowledgement remains useful diagnostic and retry evidence, but is intentionally insufficient
/// by itself because it does not prove that the receiving Mesh layer validated and accepted the logical message.
/// </remarks>
class DefaultForwardingEvidencePolicy final : public IForwardingEvidencePolicy {
public:
    ForwardingTransitionDisposition Evaluate(
        const ForwardingTransitionEvidence& evidence
    ) const noexcept override {
        using SD = ForwardingSubmissionDisposition;
        switch (evidence.Submission.Disposition) {
            case SD::Accepted:
                return evidence.MeshDeliveryAcknowledged
                    ? ForwardingTransitionDisposition::Successful
                    : ForwardingTransitionDisposition::AwaitingEvidence;
            case SD::DeadlineExpired:
                return ForwardingTransitionDisposition::DeadlineExpired;
            case SD::ResourceUnavailable:
                return ForwardingTransitionDisposition::ResourceUnavailable;
            case SD::MembershipUnavailable:
            case SD::PeerUnavailable:
            case SD::RetryableFailure:
                return ForwardingTransitionDisposition::RetryableFailure;
            case SD::HopLimitExhausted:
            case SD::PermanentFailure:
                return ForwardingTransitionDisposition::PermanentFailure;
            case SD::Invalid:
                return ForwardingTransitionDisposition::Invalid;
        }
        return ForwardingTransitionDisposition::Invalid;
    }
};

} // namespace ESPressio::Mesh
