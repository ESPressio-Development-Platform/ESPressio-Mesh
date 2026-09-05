#include <cassert>
#include <cstdint>
#include <cstring>

#include <ESPressio_ApplicationTransmissionTable.hpp>

using namespace ESPressio::Mesh;

namespace {
ESPressio::System::DeviceIdentifier Device(std::uint8_t value) { ESPressio::System::DeviceIdentifier::Storage bytes{}; bytes[15] = value; return ESPressio::System::DeviceIdentifier(bytes); }
MembershipIncarnation Incarnation(std::uint8_t value) { MembershipIncarnation::Storage bytes{}; bytes[15] = value; return MembershipIncarnation(bytes); }
class Repeatable final : public IRepeatableSerializedPayloadSource {
    const std::uint8_t* _data; std::size_t _size;
public:
    Repeatable(const std::uint8_t* data, std::size_t size) : _data(data), _size(size) {}
    std::size_t Size() const noexcept override { return _size; }
    bool Read(std::size_t offset, std::uint8_t* destination, std::size_t length) const noexcept override {
        if (destination == nullptr || offset > _size || length > _size - offset) return false;
        std::memcpy(destination, _data + offset, length); return true;
    }
};
}

int main() {
    constexpr ESPressio::Mesh::ApplicationPrimitiveDescriptor primitive{
        ESPressio::Primitive::FamilyIds::Event, 1
    };
    ApplicationTransmissionTable<2, 3> table;
    ApplicationTransmissionRecipient recipients[] = {{Device(1), Incarnation(11), 101},{Device(2), Incarnation(12), 102},{Device(3), Incarnation(13), 103}};
    const std::uint8_t bytes[] = {1,2,3,4};
    const auto payload = ApplicationPayload::Borrowed(bytes, sizeof(bytes));
    assert(payload && payload.Size() == sizeof(bytes));

    ApplicationTransmissionHandle first;
    assert(table.Begin(recipients, 3, primitive, payload, 100, 1000, first) == ApplicationTransmissionBeginResult::Begun);
    assert(first && table.Size() == 1U && table.RecipientCount(first) == 3U && table.AbsoluteDeadlineMilliseconds(first) == 1000U);
    const auto* retainedPrimitive = table.PrimitiveDescriptor(first);
    assert(retainedPrimitive != nullptr && retainedPrimitive->Family == ESPressio::Primitive::FamilyIds::Event);
    assert(retainedPrimitive->Version == 1U);
    const auto* retainedPayload = table.Payload(first);
    assert(retainedPayload != nullptr && retainedPayload->StableData() == bytes && retainedPayload->Size() == sizeof(bytes));
    std::uint8_t copy[2]{}; assert(retainedPayload->Read(1, copy, 2)); assert(copy[0] == 2 && copy[1] == 3);

    ApplicationTransmissionRecipient recipient; ApplicationRecipientOutcome outcome{};
    assert(table.TryGetRecipient(first, 1, recipient, outcome)); assert(recipient.Device == Device(2) && recipient.MessageId == 102U);
    assert(table.SetOutcome(first, 102, ApplicationRecipientOutcome::Delivered) == ApplicationTransmissionUpdateResult::Updated);
    assert(table.SetOutcome(first, 102, ApplicationRecipientOutcome::PermanentFailure) == ApplicationTransmissionUpdateResult::AlreadyTerminal);
    assert(!table.Expire(first, 999)); assert(table.Expire(first, 1000)); assert(table.IsTerminal(first));

    ApplicationTransmissionRecipient duplicateDevice[] = {{Device(4), Incarnation(14), 201},{Device(4), Incarnation(15), 202}};
    ApplicationTransmissionHandle invalid;
    assert(table.Begin(recipients, 3, {}, payload, 100, 1000, invalid) == ApplicationTransmissionBeginResult::Invalid);
    assert(table.Begin(
        recipients, 3,
        {ESPressio::Primitive::FamilyIds::MeshControl, 1},
        payload, 100, 1000, invalid
    ) == ApplicationTransmissionBeginResult::Invalid);
    assert(table.Begin(duplicateDevice, 2, primitive, payload, 100, 1000, invalid) == ApplicationTransmissionBeginResult::DuplicateRecipient);
    ApplicationTransmissionRecipient duplicateMessage[] = {{Device(4), Incarnation(14), 201},{Device(5), Incarnation(15), 201}};
    assert(table.Begin(duplicateMessage, 2, primitive, payload, 100, 1000, invalid) == ApplicationTransmissionBeginResult::DuplicateMessageId);
    assert(table.Begin(recipients, 3, primitive, {}, 100, 1000, invalid) == ApplicationTransmissionBeginResult::Invalid);

    Repeatable source(bytes, sizeof(bytes));
    const auto repeatable = ApplicationPayload::Repeatable(source);
    ApplicationTransmissionHandle second;
    assert(table.Begin(recipients, 1, primitive, repeatable, 100, 1000, second) == ApplicationTransmissionBeginResult::Begun);
    const auto* repeated = table.Payload(second); assert(repeated != nullptr && repeated->Type() == ApplicationPayload::Kind::RepeatableSerialized);
    std::uint8_t repeatedBytes[4]{}; assert(repeated->Read(0, repeatedBytes, sizeof(repeatedBytes))); assert(std::memcmp(bytes, repeatedBytes, sizeof(bytes)) == 0);

    ApplicationTransmissionHandle exhausted;
    assert(table.Begin(recipients + 1, 1, primitive, payload, 100, 1000, exhausted) == ApplicationTransmissionBeginResult::ResourceUnavailable);
    assert(table.Release(first));
    ApplicationTransmissionHandle replacement;
    assert(table.Begin(recipients + 1, 1, primitive, payload, 100, 1000, replacement) == ApplicationTransmissionBeginResult::Begun);
    assert(replacement.Slot == first.Slot && replacement.Generation != first.Generation && !table.Contains(first));
    assert(table.Release(second)); assert(table.Release(replacement)); assert(table.Size() == 0U);
    return 0;
}
