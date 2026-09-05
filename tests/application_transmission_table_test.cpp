#include <cassert>
#include <cstdint>

#include <ESPressio_ApplicationTransmissionTable.hpp>

using namespace ESPressio::Mesh;

namespace {
ESPressio::System::DeviceIdentifier Device(std::uint8_t value) {
    ESPressio::System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return ESPressio::System::DeviceIdentifier(bytes);
}

MembershipIncarnation Incarnation(std::uint8_t value) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return MembershipIncarnation(bytes);
}
}

int main() {
    ApplicationTransmissionTable<2, 3> table;
    ApplicationTransmissionRecipient recipients[] = {
        {Device(1), Incarnation(11), 101},
        {Device(2), Incarnation(12), 102},
        {Device(3), Incarnation(13), 103}
    };

    ApplicationTransmissionHandle first;
    assert(table.Begin(recipients, 3, 100, 1000, first) == ApplicationTransmissionBeginResult::Begun);
    assert(first);
    assert(table.Size() == 1U);
    assert(table.RecipientCount(first) == 3U);
    assert(table.AbsoluteDeadlineMilliseconds(first) == 1000U);
    assert(!table.IsTerminal(first));

    ApplicationTransmissionRecipient recipient;
    ApplicationRecipientOutcome outcome{};
    assert(table.TryGetRecipient(first, 1, recipient, outcome));
    assert(recipient.Device == Device(2));
    assert(recipient.MessageId == 102U);
    assert(outcome == ApplicationRecipientOutcome::Pending);

    assert(table.SetOutcome(first, 102, ApplicationRecipientOutcome::Delivered) == ApplicationTransmissionUpdateResult::Updated);
    assert(table.SetOutcome(first, 102, ApplicationRecipientOutcome::PermanentFailure) == ApplicationTransmissionUpdateResult::AlreadyTerminal);
    assert(table.SetOutcome(first, 999, ApplicationRecipientOutcome::Delivered) == ApplicationTransmissionUpdateResult::UnknownRecipient);
    assert(!table.IsTerminal(first));

    assert(!table.Expire(first, 999));
    assert(table.Expire(first, 1000));
    assert(table.IsTerminal(first));
    assert(table.TryGetRecipient(first, 0, recipient, outcome));
    assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);
    assert(table.TryGetRecipient(first, 1, recipient, outcome));
    assert(outcome == ApplicationRecipientOutcome::Delivered);
    assert(table.TryGetRecipient(first, 2, recipient, outcome));
    assert(outcome == ApplicationRecipientOutcome::DeadlineExpired);

    ApplicationTransmissionRecipient duplicateDevice[] = {
        {Device(4), Incarnation(14), 201},
        {Device(4), Incarnation(15), 202}
    };
    ApplicationTransmissionHandle invalid;
    assert(table.Begin(duplicateDevice, 2, 100, 1000, invalid) == ApplicationTransmissionBeginResult::DuplicateRecipient);
    assert(!invalid);

    ApplicationTransmissionRecipient duplicateMessage[] = {
        {Device(4), Incarnation(14), 201},
        {Device(5), Incarnation(15), 201}
    };
    assert(table.Begin(duplicateMessage, 2, 100, 1000, invalid) == ApplicationTransmissionBeginResult::DuplicateMessageId);
    assert(table.Begin(recipients, 3, 1000, 1000, invalid) == ApplicationTransmissionBeginResult::DeadlineExpired);

    ApplicationTransmissionHandle second;
    assert(table.Begin(recipients, 1, 100, 1000, second) == ApplicationTransmissionBeginResult::Begun);
    ApplicationTransmissionHandle exhausted;
    assert(table.Begin(recipients + 1, 1, 100, 1000, exhausted) == ApplicationTransmissionBeginResult::ResourceUnavailable);

    assert(table.Release(first));
    assert(!table.Contains(first));
    ApplicationTransmissionHandle replacement;
    assert(table.Begin(recipients + 1, 1, 100, 1000, replacement) == ApplicationTransmissionBeginResult::Begun);
    assert(replacement.Slot == first.Slot);
    assert(replacement.Generation != first.Generation);
    assert(!table.Contains(first));

    assert(table.Release(second));
    assert(table.Release(replacement));
    assert(table.Size() == 0U);
    return 0;
}
