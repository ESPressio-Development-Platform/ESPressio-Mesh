#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_DeliveryAcknowledgementCoordinator.hpp>

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
    Mesh::DeliveryAcknowledgementTracker<2> tracker;
    Mesh::DeliveryAcknowledgementCoordinator<2> coordinator{tracker};

    const Mesh::InboundDeliveryIdentity accepted{Device(4), Incarnation(5), 77};
    Mesh::DeliveryAcknowledgementIntent intent{};
    assert(coordinator.CreateIntent(accepted, 200U, intent) == Mesh::DeliveryAcknowledgementIntentResult::Created);
    assert(intent);
    assert(intent.Recipient == accepted.Source);
    assert(intent.RecipientIncarnation == accepted.Incarnation);
    assert(intent.AcknowledgedMessageId == accepted.MessageId);
    assert(intent.AbsoluteDeadlineMilliseconds == 200U);

    // Invalid inbound identity cannot create a control-plane intent.
    Mesh::DeliveryAcknowledgementIntent invalidIntent{};
    assert(coordinator.CreateIntent({}, 200U, invalidIntent) == Mesh::DeliveryAcknowledgementIntentResult::Invalid);
    assert(!invalidIntent);
    assert(coordinator.CreateIntent(accepted, 0U, invalidIntent) == Mesh::DeliveryAcknowledgementIntentResult::Invalid);
    assert(!invalidIntent);

    // Sender-local tracking remains separate from destination-side intent creation.
    const Mesh::PendingDeliveryAcknowledgementIdentity pending{
        Device(9), Incarnation(10), 88
    };
    assert(tracker.Reserve(pending, 100, 500) == Mesh::DeliveryAcknowledgementReserveResult::Reserved);

    // An already-authenticated ACK from another identity/incarnation cannot complete pending work.
    assert(coordinator.ApplyAuthenticated(Device(8), Incarnation(10), 88, 200) ==
           Mesh::DeliveryAcknowledgementApplyResult::NotPending);
    assert(coordinator.ApplyAuthenticated(Device(9), Incarnation(11), 88, 200) ==
           Mesh::DeliveryAcknowledgementApplyResult::NotPending);
    assert(tracker.Find(pending) != nullptr);

    // Exact authenticated destination-framework acknowledgement completes only sender-local tracking.
    assert(coordinator.ApplyAuthenticated(Device(9), Incarnation(10), 88, 200) ==
           Mesh::DeliveryAcknowledgementApplyResult::Acknowledged);
    assert(tracker.Find(pending) == nullptr);

    // This semantic coordination has no wire/control-family representation in the API.
    static_assert(sizeof(Mesh::DeliveryAcknowledgementIntent) >=
                  sizeof(System::DeviceIdentifier) + sizeof(Mesh::MembershipIncarnation) + sizeof(Mesh::MeshMessageId),
                  "Intent retains only semantic identity fields plus normal ABI padding.");

    return 0;
}
