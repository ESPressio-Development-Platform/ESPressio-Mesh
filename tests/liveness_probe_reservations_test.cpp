#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_LivenessProbeReservations.hpp"

using namespace ESPressio;

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
    Mesh::LivenessProbeReservationTable<2> probes;

    Mesh::LivenessProbeReservation first{};
    assert(probes.TryReserve(Device(1), Incarnation(1), first) ==
           Mesh::LivenessProbeReservationResult::Reserved);
    assert(first);
    assert(probes.Size() == 1);
    assert(probes.Contains(Device(1), Incarnation(1)));

    Mesh::LivenessProbeReservation duplicate{};
    assert(probes.TryReserve(Device(1), Incarnation(1), duplicate) ==
           Mesh::LivenessProbeReservationResult::AlreadyInProgress);
    assert(duplicate.Slot == first.Slot);
    assert(duplicate.Generation == first.Generation);
    assert(probes.Size() == 1);

    Mesh::LivenessProbeReservation second{};
    assert(probes.TryReserve(Device(2), Incarnation(2), second) ==
           Mesh::LivenessProbeReservationResult::Reserved);
    assert(probes.Size() == 2);

    Mesh::LivenessProbeReservation full{};
    assert(probes.TryReserve(Device(3), Incarnation(3), full) ==
           Mesh::LivenessProbeReservationResult::ResourceUnavailable);
    assert(!full);

    assert(probes.Release(first));
    assert(!probes.Contains(Device(1), Incarnation(1)));

    Mesh::LivenessProbeReservation replacement{};
    assert(probes.TryReserve(Device(3), Incarnation(3), replacement) ==
           Mesh::LivenessProbeReservationResult::Reserved);
    assert(replacement.Slot == first.Slot);
    assert(replacement.Generation != first.Generation);
    assert(!probes.Release(first));

    Mesh::LivenessProbeReservation invalid{};
    assert(probes.TryReserve(System::DeviceIdentifier{}, Incarnation(4), invalid) ==
           Mesh::LivenessProbeReservationResult::Invalid);
    assert(probes.TryReserve(Device(4), Mesh::MembershipIncarnation{}, invalid) ==
           Mesh::LivenessProbeReservationResult::Invalid);

    assert(probes.Release(second));
    assert(probes.Release(replacement));
    assert(probes.Size() == 0);
    return 0;
}
