#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_LocalMeshIdentityLifecycleCoordinator.hpp"

using namespace ESPressio;

class FakeRadio final : public Radio::IRadio {
public:
    explicit FakeRadio(std::uint8_t address) : _address(Radio::RadioAddress::FromBytes(&address, 1)) {}
    bool Start() override { return true; }
    void Stop() noexcept override {}
    bool IsStarted() const noexcept override { return true; }
    Radio::RadioCapabilities Capabilities() const noexcept override { return {}; }
    Radio::RadioAddress LocalAddress() const noexcept override { return _address; }
    Radio::RadioSendResult Send(const Radio::RadioAddress&, const std::uint8_t*, std::size_t) override {
        return Radio::RadioSendResult::Accepted();
    }
    void SetReceiver(Radio::IRadioReceiver*) noexcept override {}
    void SetWorkSignal(Radio::IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

private:
    Radio::RadioAddress _address{};
    Radio::RadioObserverSubscriptions _observers{};
};

static Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    std::array<std::uint8_t, Mesh::MembershipIncarnation::Size> bytes{};
    bytes.back() = tail;
    return Mesh::MembershipIncarnation{bytes};
}

int main() {
    Mesh::MeshMessageIdGenerator messageIds;
    Mesh::MeshRadioRegistry<2> radios;
    Mesh::LocalMeshIdentityLifecycleCoordinator<2> lifecycle{messageIds, radios};

    assert(!lifecycle.IsActive());
    assert(lifecycle.StartNewIncarnation({}) == Mesh::LocalMeshIdentityLifecycleResult::InvalidIncarnation);

    assert(lifecycle.StartNewIncarnation(Incarnation(1)) ==
           Mesh::LocalMeshIdentityLifecycleResult::StartedNewIncarnation);
    assert(lifecycle.IsActive());
    assert(lifecycle.Incarnation() == Incarnation(1));

    FakeRadio radioA{0xA1};
    Mesh::RadioIdentifier radioId = 0;
    assert(radios.Register(radioA, radioId) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(radioId == 1U);

    Mesh::MeshMessageId messageId = 0;
    assert(messageIds.TryIssue(messageId));
    assert(messageId == 1U);
    assert(messageIds.RestoreHighWater(9U));
    assert(lifecycle.RestoreAuthenticatedContinuation(Incarnation(1), 8U) ==
           Mesh::LocalMeshIdentityLifecycleResult::HighWaterRegression);
    assert(messageIds.LastIssued() == 9U);
    assert(radios.IdentifierOf(radioA) == 1U);

    assert(lifecycle.RestoreAuthenticatedContinuation(Incarnation(2), 10U) ==
           Mesh::LocalMeshIdentityLifecycleResult::IncarnationConflict);
    assert(lifecycle.StartNewIncarnation(Incarnation(1)) ==
           Mesh::LocalMeshIdentityLifecycleResult::IncarnationConflict);

    assert(lifecycle.StartNewIncarnation(Incarnation(2)) ==
           Mesh::LocalMeshIdentityLifecycleResult::StartedNewIncarnation);
    assert(lifecycle.Incarnation() == Incarnation(2));
    assert(messageIds.LastIssued() == 0U);
    assert(radios.Size() == 0U);
    assert(radios.IdentifierOf(radioA) == 0U);

    assert(radios.Register(radioA, radioId) == Mesh::MeshRadioRegistrationResult::Registered);
    assert(radioId == 1U);
    assert(messageIds.TryIssue(messageId));
    assert(messageId == 1U);

    Mesh::MeshMessageIdGenerator restoredIds;
    Mesh::MeshRadioRegistry<1> restoredRadios;
    Mesh::LocalMeshIdentityLifecycleCoordinator<1> restored{restoredIds, restoredRadios};
    assert(restored.RestoreAuthenticatedContinuation(Incarnation(7), 42U) ==
           Mesh::LocalMeshIdentityLifecycleResult::RestoredContinuation);
    assert(restored.Incarnation() == Incarnation(7));
    assert(restoredIds.LastIssued() == 42U);
    assert(restoredIds.TryIssue(messageId));
    assert(messageId == 43U);

    return 0;
}
