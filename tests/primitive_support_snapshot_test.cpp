#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_PrimitiveSupportSnapshot.hpp"

using namespace ESPressio;

class Receiver final : public Mesh::IPrimitiveReceiver {
public:
    Mesh::PrimitiveReceiveDisposition Receive(
        const Mesh::MeshReceiveContext&,
        Primitive::PrimitiveProtocolVersion,
        Mesh::PrimitivePayloadView
    ) noexcept override {
        return Mesh::PrimitiveReceiveDisposition::Accepted;
    }
};

static Primitive::ContractFingerprint Fingerprint(std::uint8_t seed) {
    Primitive::ContractFingerprint::Storage bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return Primitive::ContractFingerprint{bytes};
}

int main() {
    Mesh::PrimitiveReceiverRegistry<4> registry;
    Receiver first;
    Receiver second;
    Receiver hidden;

    Mesh::PrimitiveReceiverHandle h1{};
    Mesh::PrimitiveReceiverHandle h2{};
    Mesh::PrimitiveReceiverHandle hh{};

    const Mesh::PrimitiveReceiverDescriptor family3{
        static_cast<Primitive::PrimitiveFamilyId>(0x8003),
        {2, 4},
        Fingerprint(3),
        Mesh::PrimitiveReceiverExposure::Advertised
    };
    const Mesh::PrimitiveReceiverDescriptor family1{
        static_cast<Primitive::PrimitiveFamilyId>(0x8001),
        {1, 1},
        Fingerprint(1),
        Mesh::PrimitiveReceiverExposure::Advertised
    };
    const Mesh::PrimitiveReceiverDescriptor hiddenFamily{
        static_cast<Primitive::PrimitiveFamilyId>(0x8002),
        {1, 9},
        Fingerprint(2),
        Mesh::PrimitiveReceiverExposure::Hidden
    };

    // Deliberately register out of family order; snapshot order must be canonical rather than slot-order dependent.
    assert(registry.Register(family3, first, h1) == Mesh::PrimitiveReceiverRegistrationResult::Registered);
    assert(registry.Register(hiddenFamily, hidden, hh) == Mesh::PrimitiveReceiverRegistrationResult::Registered);
    assert(registry.Register(family1, second, h2) == Mesh::PrimitiveReceiverRegistrationResult::Registered);

    int enumerated = 0;
    registry.ForEachDescriptor([&](const Mesh::PrimitiveReceiverDescriptor&) { ++enumerated; });
    assert(enumerated == 3);

    const auto snapshot = Mesh::AdvertisedPrimitiveSupportSnapshot<4>::Capture(registry);
    assert(snapshot.Size() == 2);
    assert(!snapshot.Empty());
    assert(snapshot.At(0) != nullptr);
    assert(snapshot.At(0)->Family == static_cast<Primitive::PrimitiveFamilyId>(0x8001));
    assert(snapshot.At(1) != nullptr);
    assert(snapshot.At(1)->Family == static_cast<Primitive::PrimitiveFamilyId>(0x8003));
    assert(snapshot.At(2) == nullptr);

    const auto* support1 = snapshot.Find(static_cast<Primitive::PrimitiveFamilyId>(0x8001));
    assert(support1 != nullptr);
    assert(support1->Versions.Minimum == 1);
    assert(support1->Versions.Maximum == 1);
    assert(support1->Fingerprint == Fingerprint(1));

    const auto* support3 = snapshot.Find(static_cast<Primitive::PrimitiveFamilyId>(0x8003));
    assert(support3 != nullptr);
    assert(support3->Versions.Minimum == 2);
    assert(support3->Versions.Maximum == 4);
    assert(support3->Fingerprint == Fingerprint(3));

    // Hidden implementation support is dispatchable locally but never appears in authenticated profile input.
    assert(registry.FindDescriptor(static_cast<Primitive::PrimitiveFamilyId>(0x8002)) != nullptr);
    assert(snapshot.Find(static_cast<Primitive::PrimitiveFamilyId>(0x8002)) == nullptr);

    assert(registry.Unregister(h2));
    const auto afterRemoval = Mesh::AdvertisedPrimitiveSupportSnapshot<4>::Capture(registry);
    assert(afterRemoval.Size() == 1);
    assert(afterRemoval.At(0)->Family == static_cast<Primitive::PrimitiveFamilyId>(0x8003));

    return 0;
}
