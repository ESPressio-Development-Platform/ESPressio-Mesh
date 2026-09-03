#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

#include "ESPressio_TopologyPublicationPolicy.hpp"

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
    std::uint8_t localRadio,
    const System::DeviceIdentifier& neighbour,
    std::uint16_t metric
) {
    return {{advertiser, localRadio, neighbour, 0}, {metric}};
}

class Policy final : public Mesh::ITopologyPublicationPolicy<Characteristics, 4> {
public:
    bool Publish{true};
    mutable int Calls{0};
    mutable bool LastNewIncarnation{false};

    bool ShouldPublish(const Mesh::TopologyPublicationEvidence<Characteristics, 4>& evidence) const noexcept override {
        ++Calls;
        LastNewIncarnation = evidence.NewIncarnation;
        return Publish;
    }
};

int main() {
    const auto local = Device(1);
    const auto peerA = Device(2);
    const auto peerB = Device(3);
    const auto incarnationA = Incarnation(1);
    const auto incarnationB = Incarnation(2);

    Mesh::TopologySnapshot<Characteristics, 4> published;
    Policy policy;
    Mesh::TopologyPublicationCoordinator<Characteristics, 4> coordinator{published, policy};

    const std::array first{Link(local, 1, peerA, 10)};
    assert(coordinator.ConsiderComplete(local, incarnationA, first.data(), first.size()) ==
           Mesh::TopologyPublicationResult::Published);
    assert(published.Generation() == 1);
    assert(policy.Calls == 1);
    assert(policy.LastNewIncarnation);

    // Set-equivalent observation is not material and does not even consult policy.
    assert(coordinator.ConsiderComplete(local, incarnationA, first.data(), first.size()) ==
           Mesh::TopologyPublicationResult::Unchanged);
    assert(published.Generation() == 1);
    assert(policy.Calls == 1);

    const std::array changed{Link(local, 1, peerA, 11), Link(local, 2, peerB, 20)};
    policy.Publish = false;
    assert(coordinator.ConsiderComplete(local, incarnationA, changed.data(), changed.size()) ==
           Mesh::TopologyPublicationResult::SuppressedByPolicy);
    assert(published.Generation() == 1);
    assert(published.Size() == 1);

    policy.Publish = true;
    assert(coordinator.ConsiderComplete(local, incarnationA, changed.data(), changed.size()) ==
           Mesh::TopologyPublicationResult::Published);
    assert(published.Generation() == 2);
    assert(published.Size() == 2);
    assert(!policy.LastNewIncarnation);

    // A genuine new incarnation restarts its independent generation namespace at 1.
    const std::array reincarnated{Link(local, 1, peerB, 30)};
    assert(coordinator.ConsiderComplete(local, incarnationB, reincarnated.data(), reincarnated.size()) ==
           Mesh::TopologyPublicationResult::Published);
    assert(published.Generation() == 1);
    assert(published.Incarnation() == incarnationB);
    assert(policy.LastNewIncarnation);

    // Duplicate edge identities are rejected before policy can bless them.
    const std::array duplicate{Link(local, 1, peerA, 1), Link(local, 1, peerA, 2)};
    assert(coordinator.ConsiderComplete(local, incarnationB, duplicate.data(), duplicate.size()) ==
           Mesh::TopologyPublicationResult::Invalid);

    // Generation exhaustion is explicit and never wraps.
    Mesh::TopologySnapshot<Characteristics, 4> exhausted;
    assert(exhausted.ApplyComplete(
        local,
        incarnationA,
        std::numeric_limits<Mesh::TopologyGeneration>::max(),
        first.data(),
        first.size()) == Mesh::TopologySnapshotApplyResult::Applied);
    Mesh::TopologyPublicationCoordinator<Characteristics, 4> exhaustedCoordinator{exhausted, policy};
    assert(exhaustedCoordinator.ConsiderComplete(local, incarnationA, changed.data(), changed.size()) ==
           Mesh::TopologyPublicationResult::GenerationExhausted);
    assert(exhausted.Generation() == std::numeric_limits<Mesh::TopologyGeneration>::max());

    return 0;
}
