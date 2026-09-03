#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

/// <summary>Classification of one source/incarnation-scoped MeshMessageId against retained delivery history.</summary>
enum class DeduplicationDisposition : std::uint8_t {
    /// <summary>The sequence is non-zero and has not yet been committed inside the retained window.</summary>
    Unseen,
    /// <summary>The sequence has already been committed and must not be delivered upward again.</summary>
    Duplicate,
    /// <summary>The sequence predates the retained bounded window and is conservatively discarded.</summary>
    TooOld,
    /// <summary>Zero is Invalid/Unspecified and never represents a legitimate Mesh delivery sequence.</summary>
    Invalid
};

/// <summary>
/// Fixed-memory sliding delivery-deduplication window for one authenticated source membership incarnation.
/// </summary>
/// <remarks>
/// Bit zero represents HighestObservedSequence; increasing bit offsets represent progressively older
/// sequence values. Advancing the high-water mark does not imply skipped lower sequences were seen:
/// an unset in-window bit remains admissible later. This class owns only committed delivery history;
/// any temporary InProgress reservation needed while a receiver validates/hands off work belongs to
/// the bounded inbound-delivery execution state and must be resolved before Commit is called.
/// </remarks>
template<std::size_t WindowBits = Limits::DeduplicationWindowBits>
class DeduplicationWindow final {
    static_assert(WindowBits >= 32, "Mesh deduplication windows must retain at least 32 sequence positions.");
    static_assert(WindowBits % 64 == 0, "Mesh deduplication window size must be a whole number of 64-bit words.");

    static constexpr std::size_t WordBits = 64;
    static constexpr std::size_t WordCount = WindowBits / WordBits;

    MeshMessageId _highestObserved{0};
    std::array<std::uint64_t, WordCount> _seen{};

    static constexpr std::size_t WordIndex(std::size_t offset) noexcept {
        return offset / WordBits;
    }

    static constexpr std::uint64_t BitMask(std::size_t offset) noexcept {
        return std::uint64_t{1} << (offset % WordBits);
    }

    bool IsSet(std::size_t offset) const noexcept {
        return (_seen[WordIndex(offset)] & BitMask(offset)) != 0U;
    }

    void Set(std::size_t offset) noexcept {
        _seen[WordIndex(offset)] |= BitMask(offset);
    }

    void ShiftTowardOlder(std::size_t positions) noexcept {
        if (positions >= WindowBits) {
            _seen = {};
            return;
        }
        if (positions == 0U) return;

        const std::size_t wholeWords = positions / WordBits;
        const std::size_t bitShift = positions % WordBits;
        std::array<std::uint64_t, WordCount> shifted{};

        for (std::size_t destination = WordCount; destination-- > 0;) {
            if (destination < wholeWords) continue;
            const std::size_t source = destination - wholeWords;
            shifted[destination] |= _seen[source] << bitShift;
            if (bitShift != 0U && source > 0U) {
                shifted[destination] |= _seen[source - 1U] >> (WordBits - bitShift);
            }
        }
        _seen = shifted;
    }

public:
    /// <summary>Returns the compile-time number of retained sequence positions.</summary>
    static constexpr std::size_t CapacityBits() noexcept { return WindowBits; }

    /// <summary>Returns the greatest committed sequence observed so far, or zero when empty.</summary>
    constexpr MeshMessageId HighestObservedSequence() const noexcept {
        return _highestObserved;
    }

    /// <summary>Returns whether no delivery sequence has yet been committed.</summary>
    constexpr bool Empty() const noexcept { return _highestObserved == 0U; }

    /// <summary>Classifies a sequence without modifying retained history.</summary>
    DeduplicationDisposition Classify(MeshMessageId sequence) const noexcept {
        if (sequence == 0U) return DeduplicationDisposition::Invalid;
        if (_highestObserved == 0U || sequence > _highestObserved) {
            return DeduplicationDisposition::Unseen;
        }

        const auto offset = static_cast<std::uint64_t>(_highestObserved - sequence);
        if (offset >= WindowBits) return DeduplicationDisposition::TooOld;
        return IsSet(static_cast<std::size_t>(offset))
            ? DeduplicationDisposition::Duplicate
            : DeduplicationDisposition::Unseen;
    }

    /// <summary>
    /// Commits an unseen sequence to retained delivery history and returns its prior classification.
    /// </summary>
    /// <remarks>
    /// Duplicate, TooOld and Invalid sequences do not mutate the window. A caller that requires
    /// temporary InProgress exclusion must reserve that state separately before invoking family code,
    /// then call Commit only after the local receive disposition becomes definitive.
    /// </remarks>
    DeduplicationDisposition Commit(MeshMessageId sequence) noexcept {
        const auto disposition = Classify(sequence);
        if (disposition != DeduplicationDisposition::Unseen) return disposition;

        if (_highestObserved == 0U) {
            _highestObserved = sequence;
            _seen = {};
            Set(0);
            return disposition;
        }

        if (sequence > _highestObserved) {
            const auto delta = static_cast<std::uint64_t>(sequence - _highestObserved);
            ShiftTowardOlder(
                delta >= WindowBits
                    ? WindowBits
                    : static_cast<std::size_t>(delta)
            );
            _highestObserved = sequence;
            Set(0);
            return disposition;
        }

        const auto offset = static_cast<std::size_t>(_highestObserved - sequence);
        Set(offset);
        return disposition;
    }

    /// <summary>Clears all retained committed delivery history for this source/incarnation namespace.</summary>
    void Reset() noexcept {
        _highestObserved = 0U;
        _seen = {};
    }
};

static_assert(
    sizeof(DeduplicationWindow<Limits::DeduplicationWindowBits>) == 24,
    "The default committed deduplication core must remain one uint64 high-water value plus a 128-bit bitmap."
);

} // namespace ESPressio::Mesh
