#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_DeliveryAcknowledgementTracker.hpp>

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
    using Tracker = Mesh::DeliveryAcknowledgementTracker<2>;
    Tracker tracker;

    const Mesh::PendingDeliveryAcknowledgementIdentity first{Device(1), Incarnation(1), 10};
    const Mesh::PendingDeliveryAcknowledgementIdentity second{Device(2), Incarnation(2), 11};
    const Mesh::PendingDeliveryAcknowledgementIdentity third{Device(3), Incarnation(3), 12};

    assert(tracker.Reserve(first, 100, 200) == Mesh::DeliveryAcknowledgementReserveResult::Reserved);
    assert(tracker.Reserve(first, 101, 200) == Mesh::DeliveryAcknowledgementReserveResult::AlreadyPending);
    assert(tracker.Reserve(second, 100, 300) == Mesh::DeliveryAcknowledgementReserveResult::Reserved);
    assert(tracker.Size() == 2U);
    assert(tracker.Reserve(third, 100, 400) == Mesh::DeliveryAcknowledgementReserveResult::ResourceUnavailable);

    // Wrong authenticated identity/incarnation cannot complete another destination's delivery.
    assert(tracker.AcknowledgeAuthenticated(Device(9), Incarnation(1), 10, 150) ==
           Mesh::DeliveryAcknowledgementApplyResult::NotPending);
    assert(tracker.AcknowledgeAuthenticated(Device(1), Incarnation(9), 10, 150) ==
           Mesh::DeliveryAcknowledgementApplyResult::NotPending);
    assert(tracker.Find(first) != nullptr);

    // Exact authenticated destination acknowledgement consumes exactly one pending record.
    assert(tracker.AcknowledgeAuthenticated(Device(1), Incarnation(1), 10, 150) ==
           Mesh::DeliveryAcknowledgementApplyResult::Acknowledged);
    assert(tracker.Find(first) == nullptr);
    assert(tracker.Size() == 1U);

    // Capacity becomes immediately reusable after definitive acknowledgement.
    assert(tracker.Reserve(third, 160, 400) == Mesh::DeliveryAcknowledgementReserveResult::Reserved);
    assert(tracker.Size() == 2U);

    // An acknowledgement arriving at/after the immutable deadline cannot complete the delivery.
    assert(tracker.AcknowledgeAuthenticated(Device(2), Incarnation(2), 11, 300) ==
           Mesh::DeliveryAcknowledgementApplyResult::DeadlineExpired);
    assert(tracker.Find(second) == nullptr);
    assert(tracker.Size() == 1U);

    // Explicit cancellation/definitive failure releases the remaining exact reservation.
    assert(tracker.Release(third));
    assert(!tracker.Release(third));
    assert(tracker.Empty());

    // Expired work is purgeable without acknowledgement and never extends its own deadline.
    assert(tracker.Reserve(first, 500, 550) == Mesh::DeliveryAcknowledgementReserveResult::Reserved);
    assert(tracker.Reserve(second, 500, 700) == Mesh::DeliveryAcknowledgementReserveResult::Reserved);
    assert(tracker.PurgeExpired(549) == 0U);
    assert(tracker.PurgeExpired(550) == 1U);
    assert(tracker.Find(first) == nullptr);
    assert(tracker.Find(second) != nullptr);

    // Already-expired work is rejected before consuming bounded storage.
    tracker.Clear();
    assert(tracker.Reserve(first, 800, 800) == Mesh::DeliveryAcknowledgementReserveResult::DeadlineExpired);
    assert(tracker.Empty());

    return 0;
}
