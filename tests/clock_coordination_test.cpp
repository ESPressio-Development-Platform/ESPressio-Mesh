#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_ClockCoordination.hpp>

using namespace ESPressio;
using namespace ESPressio::Mesh;

namespace {
struct Quality final { std::uint32_t UncertaintyNanoseconds{0}; };

class QualityPolicy final : public IClockQualityPolicy<Quality> {
public:
    ClockQualityComparison Compare(const Quality& candidate, const Quality& incumbent) const noexcept override {
        if (candidate.UncertaintyNanoseconds < incumbent.UncertaintyNanoseconds) return ClockQualityComparison::Better;
        if (candidate.UncertaintyNanoseconds > incumbent.UncertaintyNanoseconds) return ClockQualityComparison::Worse;
        return ClockQualityComparison::Equivalent;
    }
};

class EligibilityPolicy final : public IClockEligibilityPolicy<Quality> {
public:
    bool IsEligible(const ClockCoordinationAdvertisement<Quality>& advertisement) const noexcept override {
        return advertisement.RootQuality.UncertaintyNanoseconds <= 1000U;
    }
};

class ParentUsabilityPolicy final : public IClockParentUsabilityPolicy<Quality> {
public:
    bool IsUsableParent(const ClockCoordinationAdvertisement<Quality>&) const noexcept override { return true; }
};

System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{}; bytes[15] = tail; return System::DeviceIdentifier(bytes);
}
MembershipIncarnation Incarnation(std::uint8_t tail) {
    MembershipIncarnation::Storage bytes{}; bytes[15] = tail; return MembershipIncarnation(bytes);
}
ClockCoordinationAdvertisement<Quality> Advertisement(
    std::uint8_t sender, std::uint8_t incarnation, std::uint8_t root, ClockStratum stratum,
    std::uint32_t uncertainty, std::uint64_t observedAt
) { return {Device(sender), Incarnation(incarnation), Device(root), stratum, Quality{uncertainty}, observedAt}; }
}

int main() {
    QualityPolicy quality;
    EligibilityPolicy eligibility;
    ParentUsabilityPolicy usability;
    DefaultClockRootElectionPolicy<Quality> roots;
    DefaultClockParentSelectionPolicy<Quality> parents;
    const auto local = Advertisement(9, 1, 9, ClockRootStratum, 900, 100);
    ClockCoordinationTable<Quality, 4> table;

    auto selection = table.Select(local, quality, eligibility, usability, roots, parents);
    assert(selection.Root == Device(9));
    assert(!selection.HasParent());
    assert(selection.LocalStratum == ClockRootStratum);

    assert(table.Observe(Advertisement(2, 1, 1, 2, 100, 110)));
    assert(table.Observe(Advertisement(3, 1, 1, 1, 100, 111)));
    selection = table.Select(local, quality, eligibility, usability, roots, parents);
    assert(selection.Root == Device(1));
    assert(selection.Parent == Device(3));
    assert(selection.ParentIncarnation == Incarnation(1));
    assert(selection.LocalStratum == 2U);

    assert(table.Observe(Advertisement(4, 1, 4, 0, 50, 112)));
    selection = table.Select(local, quality, eligibility, usability, roots, parents);
    assert(selection.Root == Device(4));
    assert(selection.Parent == Device(4));
    assert(selection.LocalStratum == 1U);

    assert(table.Observe(Advertisement(5, 1, 5, 0, 50, 113)));
    selection = table.Select(local, quality, eligibility, usability, roots, parents);
    assert(selection.Root == Device(4));

    assert(table.Observe(Advertisement(4, 2, 4, 0, 40, 200)));
    assert(!table.Observe(Advertisement(4, 2, 4, 0, 30, 199)));
    selection = table.Select(local, quality, eligibility, usability, roots, parents);
    assert(selection.Root == Device(4));
    assert(selection.ParentIncarnation == Incarnation(2));

    ClockCoordinationTable<Quality, 2> ineligible;
    assert(ineligible.Observe(Advertisement(1, 1, 1, 0, 2000, 100)));
    selection = ineligible.Select(local, quality, eligibility, usability, roots, parents);
    assert(selection.Root == Device(9));

    ClockCoordinationTable<Quality, 1> bounded;
    assert(bounded.Observe(Advertisement(1, 1, 1, 0, 100, 100)));
    assert(!bounded.Observe(Advertisement(2, 1, 2, 0, 100, 100)));
    assert(bounded.Size() == 1U);
    assert(!bounded.Remove(Device(1), Incarnation(2)));
    assert(bounded.Remove(Device(1), Incarnation(1)));
    assert(bounded.Size() == 0U);
    return 0;
}
