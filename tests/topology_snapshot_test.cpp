#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_TopologySnapshot.hpp"

using namespace ESPressio;

struct Characteristics final {
    std::uint16_t ReliabilityPermille{0};
    std::uint16_t TypicalLatencyMilliseconds{0};

    constexpr bool operator==(const Characteristics& other) const noexcept {
        return ReliabilityPermille == other.ReliabilityPermille &&
               TypicalLatencyMilliseconds == other.TypicalLatencyMilliseconds;
    }
};

static System::DeviceIdentifier Device(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return Mesh::MembershipIncarnation{bytes};
}

int main() {
    using Snapshot = Mesh::TopologySnapshot<Characteristics, 2>;
    using Link = Snapshot::Link;

    const auto authority = Device(1);
    const auto neighbourA = Device(2);
    const auto neighbourB = Device(3);
    const auto incarnation = Incarnation(1);

    const Link generationOne[] = {
        {{authority, 1, neighbourA, 2}, {990, 8}},
        {{authority, 2, neighbourB, 0}, {970, 12}}
    };

    Snapshot snapshot;
    assert(snapshot.ApplyComplete(authority, incarnation, 1, generationOne, 2) ==
           Mesh::TopologySnapshotApplyResult::Applied);
    assert(snapshot.Generation() == 1);
    assert(snapshot.Size() == 2);
    assert(snapshot.Find(generationOne[0].Identity) != nullptr);

    // Ordering is not semantic: an identical complete set is a refresh, not a new truth value.
    const Link reordered[] = {generationOne[1], generationOne[0]};
    assert(snapshot.ApplyComplete(authority, incarnation, 1, reordered, 2) ==
           Mesh::TopologySnapshotApplyResult::RefreshedSameGeneration);

    // Reusing one generation for different semantic observations is invalid authority behavior.
    const Link conflicting[] = {
        {{authority, 1, neighbourA, 2}, {800, 8}},
        {{authority, 2, neighbourB, 0}, {970, 12}}
    };
    assert(snapshot.ApplyComplete(authority, incarnation, 1, conflicting, 2) ==
           Mesh::TopologySnapshotApplyResult::ConflictingSameGeneration);

    // A newer complete generation replaces the old set; disappearance is represented by absence.
    const Link generationTwo[] = {
        {{authority, 2, neighbourB, 0}, {975, 10}}
    };
    assert(snapshot.ApplyComplete(authority, incarnation, 2, generationTwo, 1) ==
           Mesh::TopologySnapshotApplyResult::Applied);
    assert(snapshot.Generation() == 2);
    assert(snapshot.Size() == 1);
    assert(snapshot.Find(generationOne[0].Identity) == nullptr);
    assert(snapshot.Find(generationTwo[0].Identity) != nullptr);

    assert(snapshot.ApplyComplete(authority, incarnation, 1, generationOne, 2) ==
           Mesh::TopologySnapshotApplyResult::StaleGeneration);

    // A genuinely new incarnation of the same authority owns an independent TopologyGeneration namespace.
    const auto nextIncarnation = Incarnation(2);
    assert(snapshot.ApplyComplete(authority, nextIncarnation, 1, generationOne, 2) ==
           Mesh::TopologySnapshotApplyResult::Applied);
    assert(snapshot.Incarnation() == nextIncarnation);
    assert(snapshot.Generation() == 1);

    // One snapshot is permanently authority-scoped until Clear; another member cannot overwrite it accidentally.
    const auto otherAuthority = Device(4);
    const Link foreignAuthority[] = {{{otherAuthority, 1, authority, 1}, {900, 20}}};
    assert(snapshot.ApplyComplete(otherAuthority, Incarnation(4), 1, foreignAuthority, 1) ==
           Mesh::TopologySnapshotApplyResult::Invalid);
    assert(snapshot.Authority() == authority);
    assert(snapshot.Incarnation() == nextIncarnation);

    snapshot.Clear();
    assert(snapshot.ApplyComplete(otherAuthority, Incarnation(4), 1, foreignAuthority, 1) ==
           Mesh::TopologySnapshotApplyResult::Applied);
    assert(snapshot.Authority() == otherAuthority);

    // Continue remaining validation on a fresh authority-scoped snapshot.
    Snapshot validation;
    assert(validation.ApplyComplete(authority, nextIncarnation, 1, generationOne, 2) ==
           Mesh::TopologySnapshotApplyResult::Applied);

    // Edges are directed, self-owned and capacity-bounded.
    const Link wrongAdvertiser[] = {{{neighbourA, 1, authority, 1}, {900, 20}}};
    assert(validation.ApplyComplete(authority, nextIncarnation, 2, wrongAdvertiser, 1) ==
           Mesh::TopologySnapshotApplyResult::Invalid);

    const Link duplicateIdentity[] = {
        {{authority, 1, neighbourA, 2}, {900, 20}},
        {{authority, 1, neighbourA, 2}, {901, 19}}
    };
    assert(validation.ApplyComplete(authority, nextIncarnation, 2, duplicateIdentity, 2) ==
           Mesh::TopologySnapshotApplyResult::Invalid);

    const Link overCapacity[] = {
        {{authority, 1, neighbourA, 2}, {900, 20}},
        {{authority, 2, neighbourB, 0}, {900, 20}},
        {{authority, 3, Device(5), 0}, {900, 20}}
    };
    assert(validation.ApplyComplete(authority, nextIncarnation, 2, overCapacity, 3) ==
           Mesh::TopologySnapshotApplyResult::ResourceUnavailable);

    return 0;
}
