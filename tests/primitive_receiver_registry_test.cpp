#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_PrimitiveReceiverRegistry.hpp"

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

class Receiver final : public Mesh::IPrimitiveReceiver {
public:
    Mesh::PrimitiveReceiveDisposition Receive(
        const Mesh::MeshReceiveContext& context,
        Primitive::PrimitiveProtocolVersion version,
        Mesh::PrimitivePayloadView payload
    ) noexcept override {
        LastContext = context;
        LastVersion = version;
        LastSize = payload.Size;
        ++Count;
        return NextDisposition;
    }

    Mesh::MeshReceiveContext LastContext{};
    Primitive::PrimitiveProtocolVersion LastVersion{0};
    std::size_t LastSize{0};
    int Count{0};
    Mesh::PrimitiveReceiveDisposition NextDisposition{Mesh::PrimitiveReceiveDisposition::Accepted};
};

int main() {
    Mesh::PrimitiveReceiverRegistry<2> registry;
    Receiver first;
    Receiver second;

    Mesh::PrimitiveReceiverHandle reservedHandle{};
    const Mesh::PrimitiveReceiverDescriptor reservedDescriptor{
        Primitive::FamilyIds::MeshControl,
        Primitive::PrimitiveProtocolVersionRange{1, 1},
        {},
        Mesh::PrimitiveReceiverExposure::Hidden
    };
    assert(registry.Register(reservedDescriptor, first, reservedHandle) ==
           Mesh::PrimitiveReceiverRegistrationResult::Invalid);
    assert(!reservedHandle);

    Primitive::ContractFingerprint::Storage fingerprintBytes{};
    fingerprintBytes[0] = 0xA5;
    const Mesh::PrimitiveReceiverDescriptor descriptor{
        Primitive::FamilyIds::ApplicationPrivateFirst,
        Primitive::PrimitiveProtocolVersionRange{2, 4},
        Primitive::ContractFingerprint{fingerprintBytes},
        Mesh::PrimitiveReceiverExposure::Advertised
    };

    Mesh::PrimitiveReceiverHandle firstHandle{};
    assert(registry.Register(descriptor, first, firstHandle) ==
           Mesh::PrimitiveReceiverRegistrationResult::Registered);
    assert(firstHandle);
    assert(registry.Size() == 1);
    assert(registry.FindDescriptor(descriptor.Family) != nullptr);

    Mesh::PrimitiveReceiverHandle duplicate{};
    assert(registry.Register(descriptor, second, duplicate) ==
           Mesh::PrimitiveReceiverRegistrationResult::FamilyAlreadyRegistered);
    assert(!duplicate);

    const Mesh::MeshReceiveContext context{Device(1), Incarnation(1), 42, 7, false};
    const std::array<std::uint8_t, 3> payload{{1, 2, 3}};
    Mesh::PrimitiveReceiveDisposition disposition = Mesh::PrimitiveReceiveDisposition::Malformed;
    assert(registry.Dispatch(
               descriptor.Family,
               4,
               context,
               Mesh::PrimitivePayloadView{payload.data(), payload.size()},
               disposition) == Mesh::PrimitiveDispatchResult::Dispatched);
    assert(disposition == Mesh::PrimitiveReceiveDisposition::Accepted);
    assert(first.Count == 1);
    assert(first.LastVersion == 4);
    assert(first.LastSize == payload.size());
    assert(first.LastContext.Source == context.Source);

    assert(registry.Dispatch(
               descriptor.Family,
               5,
               context,
               Mesh::PrimitivePayloadView{payload.data(), payload.size()},
               disposition) == Mesh::PrimitiveDispatchResult::UnsupportedVersion);
    assert(disposition == Mesh::PrimitiveReceiveDisposition::UnsupportedVersion);
    assert(first.Count == 1);

    assert(registry.Dispatch(
               static_cast<Primitive::PrimitiveFamilyId>(Primitive::FamilyIds::ApplicationPrivateFirst + 1),
               1,
               context,
               Mesh::PrimitivePayloadView{nullptr, 0},
               disposition) == Mesh::PrimitiveDispatchResult::UnsupportedFamily);

    assert(registry.Unregister(firstHandle));
    assert(!registry.Unregister(firstHandle));

    Mesh::PrimitiveReceiverHandle replacement{};
    assert(registry.Register(descriptor, second, replacement) ==
           Mesh::PrimitiveReceiverRegistrationResult::Registered);
    assert(replacement);
    assert(replacement.Slot == firstHandle.Slot);
    assert(replacement.Generation != firstHandle.Generation);

    Mesh::PrimitiveReceiverHandle invalid{};
    Mesh::PrimitiveReceiverDescriptor invalidDescriptor{};
    assert(registry.Register(invalidDescriptor, first, invalid) ==
           Mesh::PrimitiveReceiverRegistrationResult::Invalid);
    return 0;
}
