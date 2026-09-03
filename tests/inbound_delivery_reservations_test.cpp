#include <cassert>
#include <cstdint>

#include <ESPressio_DeviceIdentifier.hpp>

#include "../src/ESPressio_InboundDeliveryReservations.hpp"

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

ESPressio::Mesh::InboundDeliveryIdentity Delivery(
    std::uint8_t device,
    std::uint8_t incarnation,
    ESPressio::Mesh::MeshMessageId message
) {
    return {Device(device), Incarnation(incarnation), message};
}

} // namespace

int main() {
    using namespace ESPressio::Mesh;

    InboundDeliveryReservationTable<2> reservations;
    assert(reservations.Empty());
    assert(reservations.MaximumSize() == 2);

    InboundDeliveryIdentity invalid{};
    assert(
        reservations.TryReserve(invalid) ==
        InboundDeliveryReservationResult::Invalid
    );

    const auto first = Delivery(1, 1, 10);
    const auto duplicateCopy = Delivery(1, 1, 10);
    const auto second = Delivery(1, 1, 11);
    const auto differentIncarnation = Delivery(1, 2, 10);

    assert(
        reservations.TryReserve(first) ==
        InboundDeliveryReservationResult::Reserved
    );
    assert(reservations.Size() == 1);
    assert(reservations.Contains(first));

    // A concurrent copy of the same authenticated delivery cannot acquire a
    // second semantic handoff reservation.
    assert(
        reservations.TryReserve(duplicateCopy) ==
        InboundDeliveryReservationResult::AlreadyInProgress
    );
    assert(reservations.Size() == 1);

    assert(
        reservations.TryReserve(second) ==
        InboundDeliveryReservationResult::Reserved
    );
    assert(reservations.Size() == 2);

    assert(
        reservations.TryReserve(differentIncarnation) ==
        InboundDeliveryReservationResult::ResourceUnavailable
    );
    assert(reservations.Size() == 2);

    assert(reservations.Release(first));
    assert(!reservations.Contains(first));
    assert(reservations.Size() == 1);

    // Once a temporary/resource-unavailable handoff releases its slot without
    // committed deduplication, a later retry may reserve the same identity.
    assert(
        reservations.TryReserve(first) ==
        InboundDeliveryReservationResult::Reserved
    );
    assert(reservations.Release(first));
    assert(!reservations.Release(first));

    // Incarnation participates in identity and creates an independent namespace.
    assert(
        reservations.TryReserve(differentIncarnation) ==
        InboundDeliveryReservationResult::Reserved
    );
    assert(reservations.Size() == 2);

    reservations.Clear();
    assert(reservations.Empty());
    assert(!reservations.Contains(second));

    return 0;
}
