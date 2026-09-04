#include <cassert>
#include <cstdint>

#include <ESPressio_ClockParentUsability.hpp>

using namespace ESPressio;
using namespace ESPressio::Mesh;

namespace {
struct Quality { std::uint32_t Value{0}; };
class QualityPolicy final : public IClockQualityPolicy<Quality> {
public:
    ClockQualityComparison Compare(const Quality& a, const Quality& b) const noexcept override {
        if (a.Value < b.Value) return ClockQualityComparison::Better;
        if (a.Value > b.Value) return ClockQualityComparison::Worse;
        return ClockQualityComparison::Equivalent;
    }
};
class Eligible final : public IClockEligibilityPolicy<Quality> {
public: bool IsEligible(const ClockCoordinationAdvertisement<Quality>&) const noexcept override { return true; }
};
System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{}; bytes[15] = tail; return System::DeviceIdentifier(bytes);
}
MembershipIncarnation Inc(std::uint8_t tail) {
    MembershipIncarnation::Storage bytes{}; bytes[15] = tail; return MembershipIncarnation(bytes);
}
ClockCoordinationAdvertisement<Quality> Ad(
    std::uint8_t sender, std::uint8_t inc, std::uint8_t root, ClockStratum stratum, std::uint32_t quality, std::uint64_t time
) { return {Device(sender), Inc(inc), Device(root), stratum, Quality{quality}, time}; }
}

int main() {
    ClockCoordinationTable<Quality, 4> clocks;
    AuthenticatedDirectPeerBindingTable<4> peers;
    DirectClockParentUsabilityPolicy<Quality, 4> usable(peers);
    QualityPolicy quality;
    Eligible eligible;
    DefaultClockRootElectionPolicy<Quality> roots;
    DefaultClockParentSelectionPolicy<Quality> parents;

    const auto local = Ad(9, 1, 9, 0, 900, 100);
    // Device 1 is the globally best root but is not directly reachable.
    assert(clocks.Observe(Ad(1, 1, 1, 0, 10, 110)));
    // Devices 2 and 3 advertise root 1; device 3 is the better parent by stratum.
    assert(clocks.Observe(Ad(2, 1, 1, 2, 10, 111)));
    assert(clocks.Observe(Ad(3, 1, 1, 1, 10, 112)));

    auto selection = clocks.Select(local, quality, eligible, usable, roots, parents);
    assert(selection.Root == Device(1));
    assert(!selection.HasParent()); // no direct authenticated synchronization peer yet

    assert(peers.Bind({Device(2), Inc(1), 1, Radio::RadioPeerHandle{1, 1}}) == DirectPeerBindingResult::Bound);
    selection = clocks.Select(local, quality, eligible, usable, roots, parents);
    assert(selection.Root == Device(1));
    assert(selection.Parent == Device(2));
    assert(selection.LocalStratum == 3U);

    assert(peers.Bind({Device(3), Inc(1), 1, Radio::RadioPeerHandle{2, 1}}) == DirectPeerBindingResult::Bound);
    selection = clocks.Select(local, quality, eligible, usable, roots, parents);
    assert(selection.Root == Device(1));
    assert(selection.Parent == Device(3));
    assert(selection.LocalStratum == 2U);

    // A stale incarnation binding cannot make the newly advertised incarnation executable.
    assert(clocks.Observe(Ad(3, 2, 1, 1, 10, 120)));
    selection = clocks.Select(local, quality, eligible, usable, roots, parents);
    assert(selection.Parent == Device(2));

    assert(peers.Bind({Device(3), Inc(2), 1, Radio::RadioPeerHandle{3, 1}}) == DirectPeerBindingResult::Replaced);
    selection = clocks.Select(local, quality, eligible, usable, roots, parents);
    assert(selection.Parent == Device(3));
    assert(selection.ParentIncarnation == Inc(2));

    // Direct-root reachability is not required: losing the direct binding to root 1 never changes root election.
    assert(selection.Root == Device(1));
    return 0;
}
