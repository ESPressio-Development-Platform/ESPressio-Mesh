#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ESPressio::Mesh {

/// <summary>Explicitly sized scratch storage for one serialized Mesh-v1 protect/open operation.</summary>
/// <remarks>
/// The two regions permit providers to reject overlapping plaintext/ciphertext buffers. No default capacity is hidden
/// in Mesh: the composition selects both sizes and includes this concrete object in its whole-device memory budget.
/// One workspace may be reused when the Mesh mutation domain guarantees that protect/open calls do not overlap.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none (empty object still occupies at least 1 byte unless empty-base optimisation applies).
 * Total Memory: 0 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
template<std::size_t InnerCapacityBytes, std::size_t PacketCapacityBytes>
class MeshV1FrameWorkspace final {
    static_assert(InnerCapacityBytes > 0U && PacketCapacityBytes > 0U,
                  "Mesh-v1 frame workspace capacities must be non-zero.");
    std::array<std::uint8_t, InnerCapacityBytes> _inner{};
    std::array<std::uint8_t, PacketCapacityBytes> _packet{};

    template<std::size_t Size>
    static void Erase(std::array<std::uint8_t, Size>& bytes) noexcept {
        volatile std::uint8_t* target = bytes.data();
        for (std::size_t index = 0; index < bytes.size(); ++index) target[index] = 0U;
    }

public:
    static constexpr std::size_t InnerCapacity = InnerCapacityBytes;
    static constexpr std::size_t PacketCapacity = PacketCapacityBytes;

    std::uint8_t* Inner(std::size_t requiredBytes) noexcept {
        return requiredBytes != 0U && requiredBytes <= _inner.size() ? _inner.data() : nullptr;
    }
    std::uint8_t* Packet(std::size_t requiredBytes) noexcept {
        return requiredBytes != 0U && requiredBytes <= _packet.size() ? _packet.data() : nullptr;
    }
    void Reset() noexcept {
        Erase(_inner);
        Erase(_packet);
    }
};

/**
 * ESPressio Memory Audit
 * Members:
 * - _workspace (TWorkspace&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI; GNU libstdc++ container control-block sizes are implementation-sensitive.
 * End ESPressio Memory Audit
 */
template<typename TWorkspace>
class MeshV1WorkspaceResetGuard final {
    TWorkspace& _workspace;
public:
    explicit MeshV1WorkspaceResetGuard(TWorkspace& workspace) noexcept : _workspace(workspace) {}
    ~MeshV1WorkspaceResetGuard() { _workspace.Reset(); }
};

} // namespace ESPressio::Mesh
