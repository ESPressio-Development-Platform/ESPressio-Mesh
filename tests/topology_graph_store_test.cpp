#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_TopologyGraphStore.hpp"

using namespace ESPressio;

struct Characteristics final {
    std::uint16_t Metric{0};
    constexpr bool operator==(const Characteristics& other) const noexcept { return Metric == other.Metric; }
    constexpr bool operator!=(const Characteristics& other) const noexcept { return !(*this == other); }
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

static Mesh::DirectedTopologyLink<Characteristics> Link(
    const System::DeviceIdentifier& advertiser,
    std::uint8_t radio,
    const System::DeviceIdentifier& neighbour,
    std::uint16_t metric
) {
    return {{advertiser, radio, neighbour, 0}, {metric}};
}

int main() {
    const auto a = Device(1);
    const auto b = Device(2);
    const auto c = Device(3);
    const auto d = Device(4);
    const auto ia = Incarnation(1);
    const auto ib = Incarnation(2);
    const auto ia2 = Incarnation(3);

    Mesh::TopologyGraphStore<Characteristics, 4, 2> graph;

    const std::array aGen1{Link(a, 1, b, 10), Link(a, 2, c, 20)};
    const std::array bGen1{Link(b, 1, a, 30), Link(b, 2, d, 40)};
    assert(graph.ApplyComplete(a, ia, 1, aGen1.data(), aGen1.size()) == Mesh::TopologySnapshotApplyResult::Applied);
    assert(graph.ApplyComplete(b, ib, 1, bGen1.data(), bGen1.size()) == Mesh::TopologySnapshotApplyResult::Applied);
    assert(graph.LinkCount() == 4);
    assert(graph.AuthorityCount() == 2);

    // MaxTopologyLinks is graph-wide: replacing A with three links would require five globally and is rejected transactionally.
    const std::array aTooLarge{
        Link(a, 1, b, 11),
        Link(a, 2, c, 21),
        Link(a, 3, d, 31)
    };
    assert(graph.ApplyComplete(a, ia, 2, aTooLarge.data(), aTooLarge.size()) ==
           Mesh::TopologySnapshotApplyResult::ResourceUnavailable);
    assert(graph.LinkCount() == 4);
    assert(graph.FindAuthority(a)->Generation == 1);
    assert(graph.Find(aGen1[0].Identity) != nullptr);

    // A smaller complete generation replaces only A; B remains intact.
    const std::array aGen2{Link(a, 1, d, 12)};
    assert(graph.ApplyComplete(a, ia, 2, aGen2.data(), aGen2.size()) == Mesh::TopologySnapshotApplyResult::Applied);
    assert(graph.LinkCount() == 3);
    assert(graph.Find(aGen1[0].Identity) == nullptr);
    assert(graph.Find(aGen2[0].Identity) != nullptr);
    assert(graph.Find(bGen1[0].Identity) != nullptr);
    assert(graph.FindAuthority(b)->Generation == 1);

    // Same-generation semantic retransmission refreshes; conflicting reuse is rejected.
    assert(graph.ApplyComplete(a, ia, 2, aGen2.data(), aGen2.size()) ==
           Mesh::TopologySnapshotApplyResult::RefreshedSameGeneration);
    const std::array conflict{Link(a, 1, d, 99)};
    assert(graph.ApplyComplete(a, ia, 2, conflict.data(), conflict.size()) ==
           Mesh::TopologySnapshotApplyResult::ConflictingSameGeneration);
    assert(graph.Find(aGen2[0].Identity)->Characteristics.Metric == 12);
    assert(graph.ApplyComplete(a, ia, 1, aGen2.data(), aGen2.size()) ==
           Mesh::TopologySnapshotApplyResult::StaleGeneration);

    // New incarnation resets only A's generation namespace and cannot disturb B.
    const std::array aReincarnated{Link(a, 4, c, 50)};
    assert(graph.ApplyComplete(a, ia2, 1, aReincarnated.data(), aReincarnated.size()) ==
           Mesh::TopologySnapshotApplyResult::Applied);
    assert(graph.FindAuthority(a)->Incarnation == ia2);
    assert(graph.FindAuthority(a)->Generation == 1);
    assert(graph.Find(bGen1[0].Identity) != nullptr);

    // Authority metadata is independently bounded even for a zero-link complete set.
    assert(graph.ApplyComplete(c, Incarnation(4), 1, nullptr, 0) ==
           Mesh::TopologySnapshotApplyResult::ResourceUnavailable);

    assert(graph.RemoveAuthority(b));
    assert(graph.AuthorityCount() == 1);
    assert(graph.LinkCount() == 1);
    assert(graph.Find(bGen1[0].Identity) == nullptr);

    assert(graph.ApplyComplete(c, Incarnation(4), 1, nullptr, 0) == Mesh::TopologySnapshotApplyResult::Applied);
    assert(graph.AuthorityCount() == 2);
    assert(graph.FindAuthority(c) != nullptr);

    graph.Clear();
    assert(graph.LinkCount() == 0);
    assert(graph.AuthorityCount() == 0);
    return 0;
}
