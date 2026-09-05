#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ESPressio::Mesh {

using MeshNodeAlias = std::uint16_t;
using RadioIdentifier = std::uint8_t;
using MeshMessageId = std::uint64_t;
using ProfileGeneration = std::uint64_t;
using TopologyGeneration = std::uint64_t;
using RemainingHopLimit = std::uint8_t;
using CapabilityMask = std::uint64_t;

/// <summary>Application-supplied opaque identity of one Mesh domain.</summary>
class MeshIdentifier final {
public:
    static constexpr std::size_t Size = 16;
    using Storage = std::array<std::uint8_t, Size>;

private:
    Storage _bytes{};

public:
    constexpr MeshIdentifier() noexcept = default;
    constexpr explicit MeshIdentifier(const Storage& bytes) noexcept : _bytes(bytes) {}
    constexpr const Storage& Bytes() const noexcept { return _bytes; }
    constexpr bool IsZero() const noexcept {
        for (auto value : _bytes) if (value != 0U) return false;
        return true;
    }
    constexpr explicit operator bool() const noexcept { return !IsZero(); }
    constexpr bool operator==(const MeshIdentifier& other) const noexcept {
        for (std::size_t i = 0; i < Size; ++i) if (_bytes[i] != other._bytes[i]) return false;
        return true;
    }
    constexpr bool operator!=(const MeshIdentifier& other) const noexcept { return !(*this == other); }
    constexpr bool operator<(const MeshIdentifier& other) const noexcept {
        for (std::size_t i = 0; i < Size; ++i) {
            if (_bytes[i] < other._bytes[i]) return true;
            if (_bytes[i] > other._bytes[i]) return false;
        }
        return false;
    }
};

/// <summary>Non-zero opaque 128-bit identity of one application-defined Group within a MeshIdentifier domain.</summary>
/// <remarks>
/// The storage order is the canonical network representation: codecs copy Bytes()[0] through Bytes()[15] unchanged.
/// The value is not a native integer and a display name is never an identity or encoding input.
/// </remarks>
class GroupIdentifier final {
public:
    static constexpr std::size_t Size = 16;
    using Storage = std::array<std::uint8_t, Size>;

private:
    Storage _bytes{};

public:
    constexpr GroupIdentifier() noexcept = default;
    constexpr explicit GroupIdentifier(const Storage& bytes) noexcept : _bytes(bytes) {}
    constexpr const Storage& Bytes() const noexcept { return _bytes; }
    constexpr bool IsZero() const noexcept {
        for (auto value : _bytes) if (value != 0U) return false;
        return true;
    }
    constexpr explicit operator bool() const noexcept { return !IsZero(); }
    constexpr bool operator==(const GroupIdentifier& other) const noexcept {
        for (std::size_t i = 0; i < Size; ++i) if (_bytes[i] != other._bytes[i]) return false;
        return true;
    }
    constexpr bool operator!=(const GroupIdentifier& other) const noexcept { return !(*this == other); }
    constexpr bool operator<(const GroupIdentifier& other) const noexcept {
        for (std::size_t i = 0; i < Size; ++i) {
            if (_bytes[i] < other._bytes[i]) return true;
            if (_bytes[i] > other._bytes[i]) return false;
        }
        return false;
    }
};

/// <summary>Non-zero 128-bit identity of one participation incarnation of a device in a Mesh.</summary>
class MembershipIncarnation final {
public:
    static constexpr std::size_t Size = 16;
    using Storage = std::array<std::uint8_t, Size>;

private:
    Storage _bytes{};

public:
    constexpr MembershipIncarnation() noexcept = default;
    constexpr explicit MembershipIncarnation(const Storage& bytes) noexcept : _bytes(bytes) {}
    constexpr const Storage& Bytes() const noexcept { return _bytes; }
    constexpr bool IsZero() const noexcept {
        for (auto value : _bytes) if (value != 0U) return false;
        return true;
    }
    constexpr explicit operator bool() const noexcept { return !IsZero(); }
    constexpr bool operator==(const MembershipIncarnation& other) const noexcept {
        for (std::size_t i = 0; i < Size; ++i) if (_bytes[i] != other._bytes[i]) return false;
        return true;
    }
    constexpr bool operator!=(const MembershipIncarnation& other) const noexcept { return !(*this == other); }
    constexpr bool operator<(const MembershipIncarnation& other) const noexcept {
        for (std::size_t i = 0; i < Size; ++i) {
            if (_bytes[i] < other._bytes[i]) return true;
            if (_bytes[i] > other._bytes[i]) return false;
        }
        return false;
    }
};

/// <summary>Bounded, exact-case human-readable name advertised by one Mesh member.</summary>
class CanonicalName final {
public:
    static constexpr std::size_t MaximumBytes = 32;
    using Storage = std::array<char, MaximumBytes>;

private:
    std::uint8_t _length{0};
    Storage _bytes{};

    static constexpr bool IsPrintable(char value) noexcept {
        const auto byte = static_cast<unsigned char>(value);
        return byte >= 0x20U && byte <= 0x7EU;
    }

public:
    constexpr CanonicalName() noexcept = default;

    /// <summary>Attempts to create a canonical name from a bounded byte sequence.</summary>
    static bool TryCreate(const char* data, std::size_t length, CanonicalName& result) noexcept {
        if (data == nullptr || length == 0 || length > MaximumBytes) return false;
        if (data[0] == ' ' || data[length - 1] == ' ') return false;
        for (std::size_t i = 0; i < length; ++i) {
            if (!IsPrintable(data[i]) || data[i] == '\0') return false;
        }
        CanonicalName candidate;
        candidate._length = static_cast<std::uint8_t>(length);
        std::memcpy(candidate._bytes.data(), data, length);
        result = candidate;
        return true;
    }

    constexpr std::uint8_t Length() const noexcept { return _length; }
    constexpr bool IsValid() const noexcept { return _length != 0U; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr const Storage& Bytes() const noexcept { return _bytes; }

    bool operator==(const CanonicalName& other) const noexcept {
        return _length == other._length &&
               (_length == 0U || std::memcmp(_bytes.data(), other._bytes.data(), _length) == 0);
    }
    bool operator!=(const CanonicalName& other) const noexcept { return !(*this == other); }
};

/// <summary>Authoritative participation lifecycle state, distinct from reachability.</summary>
enum class MembershipState : std::uint8_t {
    Unknown,
    Discovered,
    Authenticating,
    Validating,
    Joining,
    Active
};

/// <summary>Local liveness classification derived from authenticated Mesh evidence and route availability.</summary>
/// <remarks>
/// Liveness degrades with policy-controlled hysteresis from Reachable to Suspect to Unreachable;
/// any valid authenticated Mesh evidence may restore Reachable immediately. MembershipState is
/// authoritative participation state and remains a separate concept.
/// </remarks>
enum class ReachabilityState : std::uint8_t {
    Unknown,
    Reachable,
    Suspect,
    Unreachable
};

/// <summary>Reason compact historical participation evidence is retained after active membership.</summary>
enum class MembershipTombstoneDisposition : std::uint8_t {
    LocallyForgotten,
    SupersededIncarnation,
    AuthoritativeLeave
};

static_assert(sizeof(MeshIdentifier) == 16, "MeshIdentifier must remain exactly 16 bytes.");
static_assert(sizeof(GroupIdentifier) == 16, "GroupIdentifier must remain exactly 16 bytes.");
static_assert(sizeof(MembershipIncarnation) == 16, "MembershipIncarnation must remain exactly 16 bytes.");
static_assert(sizeof(CanonicalName) == 33, "CanonicalName semantic storage must remain one length byte plus 32 bytes.");
static_assert(sizeof(MeshNodeAlias) == 2, "MeshNodeAlias must remain 16-bit.");
static_assert(sizeof(RadioIdentifier) == 1, "RadioIdentifier must remain 8-bit.");
static_assert(sizeof(MeshMessageId) == 8, "MeshMessageId must remain 64-bit.");
static_assert(sizeof(ProfileGeneration) == 8, "ProfileGeneration must remain 64-bit.");
static_assert(sizeof(TopologyGeneration) == 8, "TopologyGeneration must remain 64-bit.");
static_assert(sizeof(RemainingHopLimit) == 1, "RemainingHopLimit must remain 8-bit.");
static_assert(sizeof(CapabilityMask) == 8, "Default CapabilityMask must remain 64-bit.");

} // namespace ESPressio::Mesh
