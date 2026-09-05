#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_BoundedOwnedBytePool.hpp"
#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Build-selected retained-byte and whole-device reserve contract for one platform composition.</summary>
/// <remarks>
/// Mesh supplies no universal byte sizes. A platform build must name a non-zero profile identifier and select every
/// value explicitly. The expected Radio values are checked against the RadioTransport compiled into that same build,
/// preventing a budget/profile mismatch caused by independent preprocessor settings.
/// </remarks>
template<
    std::uint32_t ProfileIdentifier,
    std::size_t InboundBytesPerDelivery,
    std::size_t ControlBytesPerFrame,
    std::size_t ApplicationBytesPerTransmission,
    std::size_t ExpectedRadioReassemblies,
    std::size_t ExpectedRadioLogicalTransferBytes,
    std::size_t TaskStackBytes,
    std::size_t OtherCompositionBytes
>
struct MeshPlatformCapacityProfile final {
    static_assert(ProfileIdentifier != 0U, "A platform capacity profile must have a stable non-zero identifier.");
    static_assert(InboundBytesPerDelivery > 0U, "Inbound owned capacity must be explicit and non-zero.");
    static_assert(ControlBytesPerFrame > 0U, "Control owned capacity must be explicit and non-zero.");
    static_assert(ApplicationBytesPerTransmission > 0U,
                  "Bounded-owned application payload capacity must be explicit and non-zero.");
    static_assert(ExpectedRadioReassemblies > 0U && ExpectedRadioLogicalTransferBytes > 0U,
                  "Expected Radio reassembly bounds must be explicit and non-zero.");

    static constexpr std::uint32_t Identifier = ProfileIdentifier;
    static constexpr std::size_t RadioReassemblies = ExpectedRadioReassemblies;
    static constexpr std::size_t RadioLogicalTransferBytes = ExpectedRadioLogicalTransferBytes;
    static constexpr std::size_t ReservedTaskStackBytes = TaskStackBytes;
    static constexpr std::size_t ReservedOtherCompositionBytes = OtherCompositionBytes;

    using InboundDeliveryPool =
        BoundedOwnedBytePool<Limits::MaxActiveInboundDeliveries, InboundBytesPerDelivery>;
    using ControlFramePool = BoundedOwnedBytePool<
        Limits::InfrastructureResponseCapacity + Limits::ClockControlCapacity + Limits::GeneralControlCapacity,
        ControlBytesPerFrame
    >;
    using ApplicationPayloadPool =
        BoundedOwnedBytePool<Limits::MaxActiveApplicationTransmissions, ApplicationBytesPerTransmission>;
};

} // namespace ESPressio::Mesh
