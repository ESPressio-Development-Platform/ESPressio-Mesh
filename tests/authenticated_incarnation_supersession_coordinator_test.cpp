#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_AuthenticatedIncarnationSupersessionCoordinator.hpp>

#include "mesh_test_cryptographic_provider.hpp"

using namespace ESPressio;

namespace {

System::DeviceIdentifier Device(std::uint8_t tail) {
    System::DeviceIdentifier::Storage bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t tail) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes[15] = tail;
    return Mesh::MembershipIncarnation{bytes};
}

Mesh::MeshSecuritySessionIdentifier SessionIdentifier(std::uint8_t tail) {
    Mesh::MeshSecuritySessionIdentifier identifier{};
    identifier.Value[15] = tail;
    return identifier;
}

struct Fixture final {
    Mesh::AuthenticatedMembershipTable<2U> Memberships;
    Mesh::MembershipTombstoneTable<4U> Tombstones;
    Mesh::DefaultMeshLivenessPolicy LivenessPolicy{100U, 200U};
    Mesh::MembershipLivenessTracker<2U> Liveness{Memberships, LivenessPolicy};
    Mesh::MembershipRetentionCoordinator<2U, 4U> Retention{Memberships, Tombstones};
    Mesh::MembershipLifecycleCoordinator<2U, 4U> Lifecycle{Memberships, Liveness, Retention};
    Mesh::MeshSecuritySessionTable<2U> Sessions;
    Mesh::AuthenticatedDirectPeerBindingTable<4U> Bindings;
    TestCryptographicProvider Provider;
    Mesh::AuthenticatedIncarnationSupersessionCoordinator<2U, 4U, 2U, 4U> Supersession{
        Memberships, Lifecycle, Sessions, Bindings, Provider};

    const System::DeviceIdentifier Peer = Device(2U);
    const Mesh::MembershipIncarnation Old = Incarnation(10U);
    const Mesh::MembershipIncarnation Replacement = Incarnation(11U);

    void EstablishOldIncarnation() {
        assert(Memberships.UpsertAuthenticated(
                   Peer, Old, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) ==
               Mesh::AuthenticatedMembershipInsertResult::Inserted);
        assert(Liveness.ObserveAuthenticatedEvidence(Peer, Old, 1'000U));

        Mesh::MeshSecuritySessionRecordHandle installed{};
        assert(Sessions.Install(
            Peer,
            Old,
            SessionIdentifier(1U),
            Mesh::MeshSecuritySessionHandle{0U, 1U},
            Provider,
            installed));
        assert(installed);

        assert(Bindings.Bind({Peer, Old, 1U, Radio::RadioPeerHandle{0U, 1U}}) ==
               Mesh::DirectPeerBindingResult::Bound);
        assert(Bindings.Bind({Peer, Old, 2U, Radio::RadioPeerHandle{1U, 1U}}) ==
               Mesh::DirectPeerBindingResult::Bound);
    }
};

void TestReleaseFailureIsNonMutating() {
    Fixture fixture;
    fixture.EstablishOldIncarnation();
    fixture.Provider.PermitRelease = false;

    const auto result = fixture.Supersession.PrepareAuthenticatedReplacement(
        fixture.Peer, fixture.Replacement, 1'100U);
    assert(result == Mesh::AuthenticatedIncarnationSupersessionResult::SessionReleaseFailed);
    assert(fixture.Provider.Releases == 1U);

    assert(fixture.Memberships.FindExact(fixture.Peer, fixture.Old) != nullptr);
    assert(fixture.Sessions.Find(fixture.Peer, fixture.Old));
    assert(fixture.Bindings.HasNeighbour(fixture.Peer, fixture.Old));
    assert(fixture.Bindings.Size() == 2U);
    assert(fixture.Tombstones.FindRetained(fixture.Peer, fixture.Old) == nullptr);
    assert(fixture.Liveness.EvidenceFor(fixture.Peer, fixture.Old) != nullptr);
}

void TestSuccessfulSupersessionRetiresExactOldExecutionState() {
    Fixture fixture;
    fixture.EstablishOldIncarnation();

    const auto result = fixture.Supersession.PrepareAuthenticatedReplacement(
        fixture.Peer, fixture.Replacement, 1'100U);
    assert(result == Mesh::AuthenticatedIncarnationSupersessionResult::Superseded);
    assert(fixture.Provider.Releases == 1U);

    assert(!fixture.Sessions.Find(fixture.Peer, fixture.Old));
    assert(!fixture.Bindings.HasNeighbour(fixture.Peer, fixture.Old));
    assert(fixture.Bindings.Size() == 0U);
    assert(fixture.Memberships.FindExact(fixture.Peer, fixture.Old) == nullptr);
    assert(fixture.Liveness.EvidenceFor(fixture.Peer, fixture.Old) == nullptr);

    const auto* tombstone = fixture.Tombstones.FindRetained(fixture.Peer, fixture.Old);
    assert(tombstone != nullptr);
    assert(tombstone->Disposition == Mesh::MembershipTombstoneDisposition::SupersededIncarnation);

    // The coordinator only prepares room for the already-authenticated replacement. It must not grant membership
    // authority to that replacement; the normal admission transaction still owns that later commit.
    assert(fixture.Memberships.FindExact(fixture.Peer, fixture.Replacement) == nullptr);
    assert(!fixture.Sessions.Find(fixture.Peer, fixture.Replacement));
}

void TestSameOrAbsentIncarnationIsNoOp() {
    Fixture fixture;
    assert(fixture.Supersession.PrepareAuthenticatedReplacement(
               fixture.Peer, fixture.Replacement, 1'000U) ==
           Mesh::AuthenticatedIncarnationSupersessionResult::NoSupersessionRequired);

    fixture.EstablishOldIncarnation();
    assert(fixture.Supersession.PrepareAuthenticatedReplacement(
               fixture.Peer, fixture.Old, 1'100U) ==
           Mesh::AuthenticatedIncarnationSupersessionResult::NoSupersessionRequired);
    assert(fixture.Provider.Releases == 0U);
    assert(fixture.Memberships.FindExact(fixture.Peer, fixture.Old) != nullptr);
    assert(fixture.Sessions.Find(fixture.Peer, fixture.Old));
    assert(fixture.Bindings.Size() == 2U);
}

void TestExactBindingRemovalDoesNotTouchDifferentIncarnation() {
    Mesh::AuthenticatedDirectPeerBindingTable<4U> bindings;
    const auto peer = Device(3U);
    const auto oldIncarnation = Incarnation(20U);
    const auto otherIncarnation = Incarnation(21U);

    assert(bindings.Bind({peer, oldIncarnation, 1U, Radio::RadioPeerHandle{0U, 1U}}) ==
           Mesh::DirectPeerBindingResult::Bound);
    assert(bindings.Bind({peer, otherIncarnation, 2U, Radio::RadioPeerHandle{1U, 1U}}) ==
           Mesh::DirectPeerBindingResult::Bound);
    assert(bindings.RemoveNeighbour(peer, oldIncarnation) == 1U);
    assert(!bindings.HasNeighbour(peer, oldIncarnation));
    assert(bindings.HasNeighbour(peer, otherIncarnation));
    assert(bindings.Size() == 1U);
}

} // namespace

int main() {
    TestReleaseFailureIsNonMutating();
    TestSuccessfulSupersessionRetiresExactOldExecutionState();
    TestSameOrAbsentIncarnationIsNoOp();
    TestExactBindingRemovalDoesNotTouchDifferentIncarnation();
    return 0;
}
