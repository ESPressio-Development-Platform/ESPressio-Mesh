#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_MeshDestinationResolver.hpp>

using namespace ESPressio;

namespace {
System::DeviceIdentifier Device(std::uint8_t value) {
    System::DeviceIdentifier::Storage bytes{};
    bytes.back() = value;
    return System::DeviceIdentifier{bytes};
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes.back() = value;
    return Mesh::MembershipIncarnation{bytes};
}

Mesh::GroupIdentifier Group(std::uint8_t value) {
    Mesh::GroupIdentifier::Storage bytes{};
    bytes.back() = value;
    return Mesh::GroupIdentifier{bytes};
}

Mesh::CanonicalName Name(char value) {
    Mesh::CanonicalName name;
    const std::array<char, 2> bytes{{'N', value}};
    assert(Mesh::CanonicalName::TryCreate(bytes.data(), bytes.size(), name));
    return name;
}

Mesh::MeshNodeProfile Profile(
    char name,
    Mesh::MeshNodeAlias alias,
    Mesh::CapabilityMask capabilities,
    Mesh::ProfileGeneration generation,
    const Mesh::GroupIdentifier* groups,
    std::size_t groupCount
) {
    Mesh::MeshNodeProfile profile;
    assert(Mesh::MeshNodeProfile::TryCreate(
        Name(name), alias, capabilities, generation, groups, groupCount, profile));
    return profile;
}
} // namespace

int main() {
    static_assert(sizeof(Mesh::GroupIdentifier) == 16U);
    assert(!Mesh::GroupIdentifier{});
    const auto groupA = Group(1U);
    const auto groupB = Group(2U);
    const std::array<Mesh::GroupIdentifier, 2> unsortedGroups{{groupB, groupA}};
    const auto canonical = Profile('A', 1U, 0x03U, 1U, unsortedGroups.data(), unsortedGroups.size());
    assert(canonical.GroupCount() == 2U);
    assert(*canonical.GroupAt(0U) == groupA && *canonical.GroupAt(1U) == groupB);
    const std::array<Mesh::GroupIdentifier, 2> duplicateGroups{{groupA, groupA}};
    Mesh::MeshNodeProfile rejected;
    assert(!Mesh::MeshNodeProfile::TryCreate(
        Name('X'), 9U, 0U, 1U, duplicateGroups.data(), duplicateGroups.size(), rejected));

    Mesh::AuthenticatedMembershipTable<4> memberships;
    const auto device1 = Device(1U);
    const auto device2 = Device(2U);
    const auto device3 = Device(3U);
    const auto device4 = Device(4U);
    const auto incarnation1 = Incarnation(1U);
    const auto incarnation2 = Incarnation(2U);
    const auto incarnation3 = Incarnation(3U);
    const auto incarnation4 = Incarnation(4U);
    assert(memberships.UpsertAuthenticated(
        device3, incarnation3, Mesh::MembershipState::Joining, Mesh::ReachabilityState::Reachable) ==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(memberships.UpsertAuthenticated(
        device1, incarnation1, Mesh::MembershipState::Active, Mesh::ReachabilityState::Unreachable) ==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(memberships.UpsertAuthenticated(
        device4, incarnation4, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) ==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);
    assert(memberships.UpsertAuthenticated(
        device2, incarnation2, Mesh::MembershipState::Active, Mesh::ReachabilityState::Reachable) ==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);

    const std::array<Mesh::GroupIdentifier, 1> onlyA{{groupA}};
    assert(memberships.ApplyAuthenticatedProfile(
        device1, incarnation1, Profile('1', 1U, 0x03U, 1U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::Applied);
    assert(memberships.ApplyAuthenticatedProfile(
        device2, incarnation2, Profile('2', 2U, 0x01U, 1U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::Applied);
    assert(memberships.ApplyAuthenticatedProfile(
        device3, incarnation3, Profile('3', 3U, 0x03U, 1U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::Applied);
    assert(memberships.ApplyAuthenticatedProfile(
        device4, incarnation4, Profile('4', 4U, 0x02U, 1U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::Applied);

    // Equal generations are idempotent only for the exact profile; lower generations and authority conflicts fail.
    const auto profile1 = Profile('1', 1U, 0x03U, 1U, onlyA.data(), onlyA.size());
    assert(memberships.ApplyAuthenticatedProfile(device1, incarnation1, profile1) ==
        Mesh::AuthenticatedProfileUpdateResult::Unchanged);
    const auto profile1Generation2 = Profile('1', 1U, 0x03U, 2U, onlyA.data(), onlyA.size());
    assert(memberships.ApplyAuthenticatedProfile(device1, incarnation1, profile1Generation2) ==
        Mesh::AuthenticatedProfileUpdateResult::Applied);
    assert(memberships.ApplyAuthenticatedProfile(device1, incarnation1, profile1) ==
        Mesh::AuthenticatedProfileUpdateResult::StaleGeneration);
    assert(memberships.ApplyAuthenticatedProfile(
        device1, incarnation1, Profile('1', 1U, 0x01U, 2U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::ConflictingGeneration);
    assert(memberships.ApplyAuthenticatedProfile(
        device1, incarnation1, profile1Generation2) ==
        Mesh::AuthenticatedProfileUpdateResult::Unchanged);
    assert(memberships.ApplyAuthenticatedProfile(
        device4, incarnation4, Profile('4', 1U, 0x02U, 2U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::ConflictingAlias);
    assert(memberships.ApplyAuthenticatedProfile(
        device4, incarnation4, Profile('1', 4U, 0x02U, 2U, onlyA.data(), onlyA.size())) ==
        Mesh::AuthenticatedProfileUpdateResult::ConflictingCanonicalName);

    // Group saturation is all-or-nothing; a larger composition receives a canonical exact-recipient snapshot.
    Mesh::MeshDestinationResolver<4, 2> smallResolver(memberships);
    Mesh::FrozenMeshRecipientSet<2> smallRecipients;
    assert(smallResolver.ResolveGroup(groupA, smallRecipients) ==
        Mesh::MeshDestinationResolutionDisposition::ResourceUnavailable);
    assert(smallRecipients.Empty());

    Mesh::MeshDestinationResolver<4, 4> resolver(memberships);
    Mesh::FrozenMeshRecipientSet<4> recipients;
    assert(resolver.ResolveGroup(groupA, recipients) ==
        Mesh::MeshDestinationResolutionDisposition::Resolved);
    assert(recipients.Size() == 3U);
    assert(recipients.At(0U)->Device == device1);
    assert(recipients.At(1U)->Device == device2);
    assert(recipients.At(2U)->Device == device4);
    assert(recipients.At(0U)->Incarnation == incarnation1);

    // CapabilitySelector requires all requested bits, ignores non-Active profiles, and is independent of reachability.
    assert(resolver.ResolveCapabilitySelector(0x02U, recipients) ==
        Mesh::MeshDestinationResolutionDisposition::Resolved);
    assert(recipients.Size() == 2U);
    assert(recipients.At(0U)->Device == device1 && recipients.At(1U)->Device == device4);
    assert(resolver.ResolveCapabilitySelector(0U, recipients) ==
        Mesh::MeshDestinationResolutionDisposition::Invalid);
    assert(recipients.Empty());
    assert(resolver.ResolveGroup(groupB, recipients) ==
        Mesh::MeshDestinationResolutionDisposition::NoRecipients);

    return 0;
}
