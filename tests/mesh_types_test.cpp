#include <cassert>
#include <cstring>

#include "ESPressio_Mesh.hpp"

int main() {
    using namespace ESPressio::Mesh;

    static_assert(sizeof(MeshIdentifier) == 16);
    static_assert(sizeof(MembershipIncarnation) == 16);
    static_assert(sizeof(CanonicalName) == 33);
    static_assert(sizeof(MeshNodeAlias) == 2);
    static_assert(sizeof(RadioIdentifier) == 1);
    static_assert(sizeof(MeshMessageId) == 8);
    static_assert(sizeof(ProfileGeneration) == 8);
    static_assert(sizeof(TopologyGeneration) == 8);
    static_assert(sizeof(RemainingHopLimit) == 1);
    static_assert(sizeof(CapabilityMask) == 8);

    MeshIdentifier invalidMesh;
    MembershipIncarnation invalidIncarnation;
    assert(!static_cast<bool>(invalidMesh));
    assert(!static_cast<bool>(invalidIncarnation));

    MeshIdentifier::Storage meshBytes{};
    meshBytes[15] = 1;
    const MeshIdentifier mesh(meshBytes);
    assert(static_cast<bool>(mesh));

    CanonicalName name;
    assert(CanonicalName::TryCreate("Node A", 6, name));
    assert(name.IsValid());
    assert(name.Length() == 6);
    assert(std::memcmp(name.Bytes().data(), "Node A", 6) == 0);

    CanonicalName rejected;
    assert(!CanonicalName::TryCreate(nullptr, 1, rejected));
    assert(!CanonicalName::TryCreate("", 0, rejected));
    assert(!CanonicalName::TryCreate(" Leading", 8, rejected));
    assert(!CanonicalName::TryCreate("Trailing ", 9, rejected));
    const char control[] = {'B', '\n'};
    assert(!CanonicalName::TryCreate(control, sizeof(control), rejected));

    static_assert(Limits::MaxMeshNodes == 32);
    static_assert(Limits::MaxRadiosPerNode == 4);
    static_assert(Limits::MaxGroupsPerNode == 8);
    static_assert(Limits::MaxPrimitiveReceivers == 8);
    static_assert(Limits::MaxActiveApplicationTransmissions == 8);
    static_assert(Limits::MaxTopologyLinks == 96);
    static_assert(Limits::MaxRouteHops == 16);
    static_assert(Limits::DefaultHopLimit == 16);
    static_assert(Limits::MaxRouteCacheEntries == 32);
    static_assert(Limits::MaxActiveInboundDeliveries == 8);
    static_assert(Limits::MaxPendingNeighbourCandidates == 8);
    static_assert(Limits::MaxActiveLivenessProbes == 4);
    static_assert(Limits::MaxActiveInboundAuthentications == 4);
    static_assert(Limits::MaxRecipientsPerTransmission == 32);
    static_assert(Limits::MaxMembershipTombstones == 64);
    static_assert(Limits::DeduplicationWindowBits == 128);
    static_assert(Limits::MaxSameRouteAttempts == 3);
    static_assert(Limits::MaxRoutesAttempted == 4);

    return 0;
}
