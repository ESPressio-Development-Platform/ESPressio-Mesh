#include <cassert>
#include <cstdint>

#include <ESPressio_DirectPeerBindings.hpp>
#include <ESPressio_ForwardingTransition.hpp>

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
    AuthenticatedDirectPeerBindingTable<2> bindings;
    const auto local = Device(1);
    const auto remote = Device(2);
    const auto incarnation1 = Incarnation(10);
    const auto incarnation2 = Incarnation(11);
    const ESPressio::Radio::RadioPeerHandle peer1{0, 1};
    const ESPressio::Radio::RadioPeerHandle peer2{1, 1};

    assert(bindings.Bind({remote, incarnation1, 1, peer1}) == DirectPeerBindingResult::Bound);
    assert(bindings.Size() == 1U);
    assert(bindings.Resolve(1, remote, incarnation1) != nullptr);
    assert(bindings.Resolve(1, remote, incarnation2) == nullptr);

    TopologyLinkIdentity hop{local, 1, remote, 1};
    assert(bindings.ResolveNextHop(hop, local, incarnation1) != nullptr);
    assert(bindings.ResolveNextHop(hop, Device(3), incarnation1) == nullptr);

    assert(bindings.Bind({remote, incarnation2, 1, peer2}) == DirectPeerBindingResult::Replaced);
    assert(bindings.Size() == 1U);
    assert(bindings.Resolve(1, remote, incarnation1) == nullptr);
    const auto* current = bindings.Resolve(1, remote, incarnation2);
    assert(current != nullptr && current->Peer == peer2);

    RemainingHopLimit remaining = 2;
    // Planning/admission itself does not touch RemainingHopLimit. Only the explicit successful transition does.
    assert(remaining == 2U);
    assert(CommitSuccessfulForwardingTransition(remaining) == ForwardingTransitionResult::Committed);
    assert(remaining == 1U);
    assert(CommitSuccessfulForwardingTransition(remaining) == ForwardingTransitionResult::Committed);
    assert(remaining == 0U);
    assert(CommitSuccessfulForwardingTransition(remaining) == ForwardingTransitionResult::HopLimitExhausted);
    assert(remaining == 0U);

    assert(bindings.RemovePeer(peer2));
    assert(bindings.Size() == 0U);
    return 0;
}
