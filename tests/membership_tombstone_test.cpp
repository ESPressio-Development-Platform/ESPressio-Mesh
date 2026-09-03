#include <cassert>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "../src/ESPressio_MembershipTombstoneTable.hpp"

namespace {

ESPressio::System::DeviceIdentifier Device(std::uint8_t value) {
    ESPressio::System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return ESPressio::System::DeviceIdentifier(bytes);
}

ESPressio::Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    ESPressio::Mesh::MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return ESPressio::Mesh::MembershipIncarnation(bytes);
}

} // namespace

int main() {
    using namespace ESPressio::Mesh;

    MembershipTombstoneTable<3> table;
    assert(table.Empty());
    assert(table.MaximumSize() == 3);

    assert(!table.Record({}, Incarnation(1), MembershipTombstoneDisposition::LocallyForgotten, 100, 50));
    assert(!table.Record(Device(1), {}, MembershipTombstoneDisposition::LocallyForgotten, 100, 50));
    assert(!table.Record(Device(1), Incarnation(1), MembershipTombstoneDisposition::LocallyForgotten, 100, 0));

    assert(table.Record(Device(1), Incarnation(1), MembershipTombstoneDisposition::AuthoritativeLeave, 100, 500));
    assert(table.Record(Device(2), Incarnation(2), MembershipTombstoneDisposition::SupersededIncarnation, 100, 400));
    assert(table.Record(Device(3), Incarnation(3), MembershipTombstoneDisposition::LocallyForgotten, 100, 300));
    assert(table.Size() == 3);

    // Exact refresh does not consume another slot and updates disposition/deadline.
    assert(table.Record(Device(2), Incarnation(2), MembershipTombstoneDisposition::AuthoritativeLeave, 150, 600));
    assert(table.Size() == 3);
    const auto* refreshed = table.FindRetained(Device(2), Incarnation(2));
    assert(refreshed != nullptr);
    assert(refreshed->Disposition == MembershipTombstoneDisposition::AuthoritativeLeave);
    assert(refreshed->RetentionDeadlineMilliseconds == 750);

    // Saturation evicts LocallyForgotten before stronger retained dispositions,
    // regardless of the latter entries' absolute deadlines.
    assert(table.Record(Device(4), Incarnation(4), MembershipTombstoneDisposition::AuthoritativeLeave, 160, 700));
    assert(table.Size() == 3);
    assert(table.FindRetained(Device(3), Incarnation(3)) == nullptr);
    assert(table.FindRetained(Device(1), Incarnation(1)) != nullptr);
    assert(table.FindRetained(Device(2), Incarnation(2)) != nullptr);
    assert(table.FindRetained(Device(4), Incarnation(4)) != nullptr);

    // With equal disposition, the earliest deadline is the deterministic victim.
    MembershipTombstoneTable<2> sameDisposition;
    assert(sameDisposition.Record(Device(5), Incarnation(5), MembershipTombstoneDisposition::LocallyForgotten, 0, 500));
    assert(sameDisposition.Record(Device(6), Incarnation(6), MembershipTombstoneDisposition::LocallyForgotten, 0, 600));
    assert(sameDisposition.Record(Device(7), Incarnation(7), MembershipTombstoneDisposition::LocallyForgotten, 1, 700));
    assert(sameDisposition.FindRetained(Device(5), Incarnation(5)) == nullptr);
    assert(sameDisposition.FindRetained(Device(6), Incarnation(6)) != nullptr);
    assert(sameDisposition.FindRetained(Device(7), Incarnation(7)) != nullptr);

    // Expiry is local monotonic retention only and immediately frees capacity.
    assert(table.PurgeExpired(600) == 1);
    assert(table.FindRetained(Device(1), Incarnation(1)) == nullptr);
    assert(table.Size() == 2);
    assert(table.Record(Device(8), Incarnation(8), MembershipTombstoneDisposition::LocallyForgotten, 600, 50));
    assert(table.Size() == 3);
    assert(table.Find(Device(8), Incarnation(8), 650) == nullptr);
    assert(table.Size() == 2);

    assert(table.Remove(Device(2), Incarnation(2)));
    assert(!table.Remove(Device(2), Incarnation(2)));
    table.Clear();
    assert(table.Empty());

    return 0;
}
