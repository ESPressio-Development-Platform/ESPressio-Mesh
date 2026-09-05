#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_ApplicationPayloadStaging.hpp"
#include "ESPressio_ApplicationTransmissionTable.hpp"
#include "ESPressio_ForwardingSubmissionCoordinator.hpp"

namespace ESPressio::Mesh {

enum class ApplicationForwardingDisposition : std::uint8_t {
    Submitted,
    StagingRequired,
    StagingCapacityExceeded,
    StagingUnavailable,
    SerializationFailed,
    UnknownTransmission,
    UnknownRecipient,
    RecipientTerminal,
    RouteMismatch,
    Invalid
};

struct ApplicationForwardingResult final {
    ApplicationForwardingDisposition Disposition{ApplicationForwardingDisposition::Invalid};
    ForwardingSubmissionResult Submission{};

    constexpr explicit operator bool() const noexcept {
        return Disposition == ApplicationForwardingDisposition::Submitted && static_cast<bool>(Submission);
    }
};

/// <summary>
/// Binds one frozen application recipient to the aggregate's immutable payload and the authenticated forwarding submitter.
/// </summary>
/// <remarks>
/// Borrowed Stable payloads are submitted directly from their aggregate backing without copying or re-serialization.
/// Repeatable Serialized Source may be materialized into an optional composition-supplied bounded staging buffer. Mesh
/// intentionally defines no staging byte capacity; absent/insufficient storage is reported explicitly. The staging bytes
/// are borrowed only for the synchronous forwarding submission call and do not become aggregate-owned payload storage.
/// The aggregate remains the sole owner of recipient identity/message selection and payload-reference semantics.
/// </remarks>
template<std::size_t TransmissionCapacity = Limits::MaxActiveApplicationTransmissions,
         std::size_t RecipientCapacity = Limits::MaxRecipientsPerTransmission,
         std::size_t MembershipCapacity = Limits::MaxMeshNodes,
         std::size_t BindingCapacity = Limits::MaxTopologyLinks,
         std::size_t HopCapacity = Limits::MaxRouteHops>
class ApplicationForwardingCoordinator final {
    const ApplicationTransmissionTable<TransmissionCapacity, RecipientCapacity>& _transmissions;
    ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& _forwarding;
    IApplicationPayloadStagingBuffer* _staging{nullptr};

public:
    ApplicationForwardingCoordinator(
        const ApplicationTransmissionTable<TransmissionCapacity, RecipientCapacity>& transmissions,
        ForwardingSubmissionCoordinator<MembershipCapacity, BindingCapacity, HopCapacity>& forwarding,
        IApplicationPayloadStagingBuffer* staging = nullptr
    ) noexcept : _transmissions(transmissions), _forwarding(forwarding), _staging(staging) {}

    ApplicationForwardingResult SubmitRecipient(
        ApplicationTransmissionHandle transmission,
        std::size_t recipientIndex,
        const System::DeviceIdentifier& localDevice,
        const ResolvedRoute<HopCapacity>& route,
        RemainingHopLimit remainingHopLimit,
        std::uint64_t nowMilliseconds
    ) {
        if (!_transmissions.Contains(transmission) || !localDevice) {
            return {ApplicationForwardingDisposition::UnknownTransmission, {}};
        }

        ApplicationTransmissionRecipient recipient{};
        ApplicationRecipientOutcome outcome{};
        if (!_transmissions.TryGetRecipient(transmission, recipientIndex, recipient, outcome)) {
            return {ApplicationForwardingDisposition::UnknownRecipient, {}};
        }
        if (outcome != ApplicationRecipientOutcome::Pending) {
            return {ApplicationForwardingDisposition::RecipientTerminal, {}};
        }
        if (route.Source() != localDevice || route.Destination() != recipient.Device) {
            return {ApplicationForwardingDisposition::RouteMismatch, {}};
        }

        const auto* payload = _transmissions.Payload(transmission);
        if (payload == nullptr || !*payload) return {ApplicationForwardingDisposition::Invalid, {}};

        const std::uint8_t* bytes = payload->StableData();
        if (payload->Type() == ApplicationPayload::Kind::RepeatableSerialized) {
            if (_staging == nullptr) return {ApplicationForwardingDisposition::StagingRequired, {}};
            if (payload->Size() > _staging->Capacity()) {
                return {ApplicationForwardingDisposition::StagingCapacityExceeded, {}};
            }
            auto* stagingBytes = _staging->Data();
            if (stagingBytes == nullptr) return {ApplicationForwardingDisposition::StagingUnavailable, {}};
            if (!payload->Read(0U, stagingBytes, payload->Size())) {
                return {ApplicationForwardingDisposition::SerializationFailed, {}};
            }
            bytes = stagingBytes;
        }
        if (bytes == nullptr) return {ApplicationForwardingDisposition::Invalid, {}};

        auto submission = _forwarding.Submit(
            localDevice,
            route,
            remainingHopLimit,
            bytes,
            payload->Size(),
            nowMilliseconds,
            _transmissions.AbsoluteDeadlineMilliseconds(transmission)
        );
        return {ApplicationForwardingDisposition::Submitted, submission};
    }
};

} // namespace ESPressio::Mesh
