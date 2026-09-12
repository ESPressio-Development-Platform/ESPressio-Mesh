#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_PrimitiveAdmission.hpp>
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
    Primitive::PrimitiveAdmissionDisposition Receive(
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
    Primitive::PrimitiveAdmissionDisposition NextDisposition{Primitive::PrimitiveAdmissionDisposition::Accepted};
};

int main() {
    using Primitive::PrimitiveAdmissionDisposition;
    static_assert(Primitive::EstablishesDestinationAdmission(PrimitiveAdmissionDisposition::Accepted));
    static_assert(Primitive::EstablishesDestinationAdmission(PrimitiveAdmissionDisposition::AlreadyAccepted));
    static_assert(!Primitive::EstablishesDestinationAdmission(PrimitiveAdmissionDisposition::TemporarilyUnavailable));
    static_assert(!Primitive::EstablishesDestinationAdmission(PrimitiveAdmissionDisposition::ResourceUnavailable));
    static_assert(Primitive::IsAdmissionRetryCandidate(PrimitiveAdmissionDisposition::TemporarilyUnavailable));
    static_assert(Primitive::IsAdmissionRetryCandidate(PrimitiveAdmissionDisposition::ResourceUnavailable));
    static_assert(!Primitive::IsAdmissionRetryCandidate(PrimitiveAdmissionDisposition::AlreadyAccepted));

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

    Mesh::PrimitiveReceiverHandle duplicate{};
    assert(registry.Register(descriptor, second, duplicate) ==
           Mesh::PrimitiveReceiverRegistrationResult::FamilyAlreadyRegistered);

    const Mesh::MeshReceiveContext context{Device(1), Incarnation(1), 42, 7, false};
    const std::array<std::uint8_t, 3> payload{{1, 2, 3}};
    PrimitiveAdmissionDisposition disposition = PrimitiveAdmissionDisposition::Malformed;

    const std::array<PrimitiveAdmissionDisposition, 7> allResults{{
        PrimitiveAdmissionDisposition::Accepted,
        PrimitiveAdmissionDisposition::AlreadyAccepted,
        PrimitiveAdmissionDisposition::TemporarilyUnavailable,
        PrimitiveAdmissionDisposition::ResourceUnavailable,
        PrimitiveAdmissionDisposition::Unsupported,
        PrimitiveAdmissionDisposition::Rejected,
        PrimitiveAdmissionDisposition::Malformed
    }};
    for (const auto expected : allResults) {
        first.NextDisposition = expected;
        assert(registry.Dispatch(
                   descriptor.Family, 4, context,
                   Mesh::PrimitivePayloadView{payload.data(), payload.size()}, disposition) ==
               Mesh::PrimitiveDispatchResult::Dispatched);
        assert(disposition == expected);
    }
    assert(first.Count == static_cast<int>(allResults.size()));
    assert(first.LastVersion == 4);
    assert(first.LastSize == payload.size());
    assert(first.LastContext.Source == context.Source);

    assert(registry.Dispatch(
               descriptor.Family, 5, context,
               Mesh::PrimitivePayloadView{payload.data(), payload.size()}, disposition) ==
           Mesh::PrimitiveDispatchResult::UnsupportedVersion);
    assert(disposition == PrimitiveAdmissionDisposition::Unsupported);

    assert(registry.Dispatch(
               static_cast<Primitive::PrimitiveFamilyId>(Primitive::FamilyIds::ApplicationPrivateFirst + 1),
               1, context, Mesh::PrimitivePayloadView{nullptr, 0}, disposition) ==
           Mesh::PrimitiveDispatchResult::UnsupportedFamily);
    assert(disposition == PrimitiveAdmissionDisposition::Unsupported);

    const Mesh::MeshReceiveContext invalidContext{};
    assert(registry.Dispatch(
               descriptor.Family, 4, invalidContext,
               Mesh::PrimitivePayloadView{payload.data(), payload.size()}, disposition) ==
           Mesh::PrimitiveDispatchResult::Invalid);
    assert(disposition == PrimitiveAdmissionDisposition::Malformed);

    assert(registry.Unregister(firstHandle));
    assert(!registry.Unregister(firstHandle));

    Mesh::PrimitiveReceiverHandle replacement{};
    assert(registry.Register(descriptor, second, replacement) ==
           Mesh::PrimitiveReceiverRegistrationResult::Registered);
    assert(replacement);
    assert(replacement.Slot == firstHandle.Slot);
    assert(replacement.Generation != firstHandle.Generation);

    return 0;
}
