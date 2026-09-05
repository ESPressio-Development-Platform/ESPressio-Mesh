#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_MeshLimits.hpp"
#include "ESPressio_MeshV1Security.hpp"

namespace ESPressio::Mesh {

struct MeshSecuritySessionRecordHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
};

/// <summary>64-position replay window committed only after successful authentication.</summary>
class MeshSecurityReplayWindow final {
    std::uint64_t _highest{0};
    std::uint64_t _seen{0};

public:
    constexpr bool CanAccept(std::uint64_t sequence) const noexcept {
        if (sequence == 0U) return false;
        if (_highest == 0U || sequence > _highest) return true;
        const auto distance = _highest - sequence;
        return distance < MeshV1SecuritySuite::ReplayWindowBits &&
               (_seen & (std::uint64_t{1} << distance)) == 0U;
    }

    bool CommitAuthenticated(std::uint64_t sequence) noexcept {
        if (!CanAccept(sequence)) return false;
        if (_highest == 0U) {
            _highest = sequence;
            _seen = 1U;
        } else if (sequence > _highest) {
            const auto advance = sequence - _highest;
            _seen = advance >= MeshV1SecuritySuite::ReplayWindowBits ? 1U : ((_seen << advance) | 1U);
            _highest = sequence;
        } else {
            _seen |= std::uint64_t{1} << (_highest - sequence);
        }
        return true;
    }

    constexpr std::uint64_t HighestAuthenticated() const noexcept { return _highest; }
    void Reset() noexcept { _highest = 0U; _seen = 0U; }
};

/// <summary>Bounded ownership of established pairwise Mesh v1 session state.</summary>
/// <remarks>
/// At most one session is retained for each peer DeviceIdentifier. Installing a new incarnation/session for that peer
/// synchronously releases its previous provider-owned keys. Hop and EndToEnd purposes have independent outbound sequence
/// and inbound replay domains, matching the provider's independently derived keys/base IVs. Callers preflight replay,
/// authenticate with IMeshV1CryptographicProvider::Open, then commit; unauthenticated input can never advance a window.
/// </remarks>
template<std::size_t Capacity = Limits::MaxMeshNodes>
class MeshSecuritySessionTable final {
    static_assert(Capacity > 0U && Capacity < std::numeric_limits<std::uint16_t>::max(),
                  "Mesh security session capacity must be explicit and fit its handle slot.");

    struct Record final {
        System::DeviceIdentifier Device{};
        MembershipIncarnation Incarnation{};
        MeshSecuritySessionIdentifier Identifier{};
        MeshSecuritySessionHandle ProviderSession{};
        std::uint64_t NextHopSequence{1U};
        std::uint64_t NextEndToEndSequence{1U};
        MeshSecurityReplayWindow HopReplay{};
        MeshSecurityReplayWindow EndToEndReplay{};
        std::uint16_t Generation{0};
        bool Used{false};
    };

    std::array<Record, Capacity> _records{};

    static constexpr std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        const auto next = static_cast<std::uint16_t>(current + 1U);
        return next == 0U ? 1U : next;
    }

    Record* ResolveRecord(MeshSecuritySessionRecordHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }

    const Record* ResolveRecord(MeshSecuritySessionRecordHandle handle) const noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        const auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }

    static MeshSecurityReplayWindow* Replay(Record& record, MeshSecurityTrafficPurpose purpose) noexcept {
        if (purpose == MeshSecurityTrafficPurpose::Hop) return &record.HopReplay;
        if (purpose == MeshSecurityTrafficPurpose::EndToEnd) return &record.EndToEndReplay;
        return nullptr;
    }

    static const MeshSecurityReplayWindow* Replay(const Record& record, MeshSecurityTrafficPurpose purpose) noexcept {
        if (purpose == MeshSecurityTrafficPurpose::Hop) return &record.HopReplay;
        if (purpose == MeshSecurityTrafficPurpose::EndToEnd) return &record.EndToEndReplay;
        return nullptr;
    }

    static std::uint64_t* NextSequence(Record& record, MeshSecurityTrafficPurpose purpose) noexcept {
        if (purpose == MeshSecurityTrafficPurpose::Hop) return &record.NextHopSequence;
        if (purpose == MeshSecurityTrafficPurpose::EndToEnd) return &record.NextEndToEndSequence;
        return nullptr;
    }

    static void ClearRecord(Record& record) noexcept {
        const auto generation = record.Generation;
        record = {};
        record.Generation = generation;
    }

public:
    static constexpr std::size_t MaximumSize = Capacity;

    bool Install(
        const System::DeviceIdentifier& peer,
        const MembershipIncarnation& incarnation,
        const MeshSecuritySessionIdentifier& identifier,
        MeshSecuritySessionHandle providerSession,
        IMeshV1CryptographicProvider& provider,
        MeshSecuritySessionRecordHandle& handle
    ) noexcept {
        handle = {};
        if (!peer || !incarnation || !identifier || !providerSession) return false;

        std::size_t target = Capacity;
        for (std::size_t index = 0; index < Capacity; ++index) {
            if (_records[index].Used && _records[index].Device == peer) {
                target = index;
                break;
            }
            if (!_records[index].Used && target == Capacity) target = index;
        }
        if (target == Capacity) return false;

        auto& record = _records[target];
        if (record.Used && !provider.ReleaseSession(record.ProviderSession)) return false;
        record.Device = peer;
        record.Incarnation = incarnation;
        record.Identifier = identifier;
        record.ProviderSession = providerSession;
        record.NextHopSequence = 1U;
        record.NextEndToEndSequence = 1U;
        record.HopReplay.Reset();
        record.EndToEndReplay.Reset();
        record.Generation = NextGeneration(record.Generation);
        record.Used = true;
        handle = {static_cast<std::uint16_t>(target), record.Generation};
        return true;
    }

    MeshSecuritySessionRecordHandle Find(
        const System::DeviceIdentifier& peer,
        const MembershipIncarnation& incarnation
    ) const noexcept {
        if (!peer || !incarnation) return {};
        for (std::size_t index = 0; index < Capacity; ++index) {
            const auto& record = _records[index];
            if (record.Used && record.Device == peer && record.Incarnation == incarnation) {
                return {static_cast<std::uint16_t>(index), record.Generation};
            }
        }
        return {};
    }

    MeshSecuritySessionHandle ProviderSession(MeshSecuritySessionRecordHandle handle) const noexcept {
        const auto* record = ResolveRecord(handle);
        return record == nullptr ? MeshSecuritySessionHandle{} : record->ProviderSession;
    }

    MeshSecuritySessionIdentifier Identifier(MeshSecuritySessionRecordHandle handle) const noexcept {
        const auto* record = ResolveRecord(handle);
        return record == nullptr ? MeshSecuritySessionIdentifier{} : record->Identifier;
    }

    std::uint64_t IssueSequence(
        MeshSecuritySessionRecordHandle handle,
        MeshSecurityTrafficPurpose purpose
    ) noexcept {
        auto* record = ResolveRecord(handle);
        if (record == nullptr) return 0U;
        auto* next = NextSequence(*record, purpose);
        if (next == nullptr || *next == 0U || *next == std::numeric_limits<std::uint64_t>::max()) return 0U;
        const auto issued = *next;
        ++(*next);
        return issued;
    }

    bool CanAcceptInbound(
        MeshSecuritySessionRecordHandle handle,
        MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence
    ) const noexcept {
        const auto* record = ResolveRecord(handle);
        const auto* replay = record == nullptr ? nullptr : Replay(*record, purpose);
        return replay != nullptr && replay->CanAccept(sequence);
    }

    bool CommitAuthenticatedInbound(
        MeshSecuritySessionRecordHandle handle,
        MeshSecurityTrafficPurpose purpose,
        std::uint64_t sequence
    ) noexcept {
        auto* record = ResolveRecord(handle);
        auto* replay = record == nullptr ? nullptr : Replay(*record, purpose);
        return replay != nullptr && replay->CommitAuthenticated(sequence);
    }

    bool Release(
        MeshSecuritySessionRecordHandle handle,
        IMeshV1CryptographicProvider& provider
    ) noexcept {
        auto* record = ResolveRecord(handle);
        if (record == nullptr || !provider.ReleaseSession(record->ProviderSession)) return false;
        ClearRecord(*record);
        return true;
    }

    bool ResetForControlledShutdown(IMeshV1CryptographicProvider& provider) noexcept {
        bool releasedAll = true;
        for (auto& record : _records) {
            if (!record.Used) continue;
            if (!provider.ReleaseSession(record.ProviderSession)) releasedAll = false;
        }
        provider.ResetForControlledShutdown();
        for (auto& record : _records) {
            if (!record.Used) continue;
            ClearRecord(record);
        }
        return releasedAll;
    }
};

} // namespace ESPressio::Mesh
