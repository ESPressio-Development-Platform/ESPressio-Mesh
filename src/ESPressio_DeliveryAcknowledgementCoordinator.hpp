#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_DeliveryAcknowledgementTracker.hpp"
#include "ESPressio_InboundDeliveryReservations.hpp"

namespace ESPressio::Mesh {

/// <summary>
/// Wire-neutral intent to acknowledge definitive destination-framework acceptance of one inbound Mesh delivery.
/// </summary>
/// <remarks>
/// This value is not a Mesh control message and has no serialization or routing contract. It captures only the semantic
/// fact a future control-plane encoder will need: acknowledge the original authenticated source incarnation's exact
/// MeshMessageId. The intent must be created only after the destination framework has made a definitive acceptance
/// decision; intermediate Radio/link success and forwarding admission are insufficient.
/// </remarks>
struct DeliveryAcknowledgementIntent final {
    System::DeviceIdentifier Recipient{};
    MembershipIncarnation RecipientIncarnation{};
    MeshMessageId AcknowledgedMessageId{0};

    constexpr bool IsValid() const noexcept {
        return static_cast<bool>(Recipient) &&
               static_cast<bool>(RecipientIncarnation) &&
               AcknowledgedMessageId != 0U;
    }

    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Result of creating one destination-side acknowledgement intent.</summary>
enum class DeliveryAcknowledgementIntentResult : std::uint8_t {
    Created,
    Invalid
};

/// <summary>
/// Narrow semantic boundary around destination acknowledgement intent creation and sender-local authenticated ACK use.
/// </summary>
/// <remarks>
/// This coordinator deliberately knows nothing about the Mesh control PrimitiveFamilyId, wire schema, routing, Radio,
/// encryption or authentication mechanism. The caller is responsible for transporting a created intent through the
/// eventual Mesh control plane. On receipt, the caller must authenticate the ACK's exact source identity/incarnation
/// before calling ApplyAuthenticated().
///
/// An applied ACK means only that the destination framework accepted the Mesh delivery. It never represents completion
/// of an application Command or other requested operation.
/// </remarks>
template<std::size_t TrackerCapacity>
class DeliveryAcknowledgementCoordinator final {
    DeliveryAcknowledgementTracker<TrackerCapacity>& _tracker;

public:
    explicit DeliveryAcknowledgementCoordinator(
        DeliveryAcknowledgementTracker<TrackerCapacity>& tracker
    ) noexcept : _tracker(tracker) {}

    /// <summary>
    /// Creates an acknowledgement intent for one already-authenticated inbound delivery after definitive destination
    /// acceptance. The caller decides whether the delivery semantics require an ACK before invoking this method.
    /// </summary>
    DeliveryAcknowledgementIntentResult CreateIntent(
        const InboundDeliveryIdentity& acceptedDelivery,
        DeliveryAcknowledgementIntent& intent
    ) const noexcept {
        intent = {};
        if (!acceptedDelivery.IsValid()) return DeliveryAcknowledgementIntentResult::Invalid;
        intent = DeliveryAcknowledgementIntent{
            acceptedDelivery.Source,
            acceptedDelivery.Incarnation,
            acceptedDelivery.MessageId
        };
        return DeliveryAcknowledgementIntentResult::Created;
    }

    /// <summary>
    /// Applies an already-authenticated ACK to sender-local pending delivery state.
    /// </summary>
    DeliveryAcknowledgementApplyResult ApplyAuthenticated(
        const System::DeviceIdentifier& authenticatedSource,
        const MembershipIncarnation& authenticatedSourceIncarnation,
        MeshMessageId acknowledgedMessageId,
        std::uint64_t nowMilliseconds
    ) noexcept {
        return _tracker.AcknowledgeAuthenticated(
            authenticatedSource,
            authenticatedSourceIncarnation,
            acknowledgedMessageId,
            nowMilliseconds
        );
    }
};

} // namespace ESPressio::Mesh
