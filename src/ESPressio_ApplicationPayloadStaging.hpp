#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Mesh {

/// <summary>
/// Composition-supplied bounded scratch storage for materializing one repeatable application payload during submission.
/// </summary>
/// <remarks>
/// Mesh deliberately provides no default byte capacity. The application/composition root owns the storage and chooses
/// its finite capacity according to target memory policy. The buffer is borrowed only for the synchronous forwarding
/// submission call; it does not become aggregate-owned payload storage. Serialized Mesh execution may safely reuse one
/// instance across submissions when the composition guarantees non-overlap.
/// </remarks>
class IApplicationPayloadStagingBuffer {
public:
    virtual ~IApplicationPayloadStagingBuffer() = default;

    /// <summary>Returns the finite number of bytes available for one staged payload.</summary>
    virtual std::size_t Capacity() const noexcept = 0;

    /// <summary>Returns writable staging storage, or null when storage is presently unavailable.</summary>
    virtual std::uint8_t* Data() noexcept = 0;
};

} // namespace ESPressio::Mesh
