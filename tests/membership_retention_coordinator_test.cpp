#include <cassert>
#include <cstdint>

#include <ESPressio_Mesh.hpp>

using namespace ESPressio::Mesh;

static ESPressio::System::DeviceIdentifier Device(std::uint8_t value) {
    ESPressio::System::DeviceIdentifier::Storage bytes{};
    bytes[15] = value;
    return ESPressio::System::DeviceIdentifier(bytes);
}

static MembershipIncarnation Incarnation(std::uint8_t value) {
    MembershipIncarnation::Storage bytes{};
    bytes[15] = value;
    return MembershipIncarnation(bytes);
}

int main() {
    using Memberships = AuthenticatedMembershipTable<2>;
    using Tombstones = MembershipTombstoneTable<2>;
    using Retention = MembershipRetentionCoordinator<2, 2>;

    Memberships memberships;
    Tombstones tombstones;
    Retention retention(memberships, tombstones);

    const auto deviceA = Device(1);
    const auto incarnationA = Incarnation(1);
    assert(memberships.UpsertAuthenticated(
        deviceA,
        incarnationA,
        MembershipState::Active,
        ReachabilityState::Reachable
    ) == AuthenticatedMembershipInsertResult::Inserted);

    assert(retention.RecordAuthoritativeLeave(deviceA, incarnationA, 1000) ==
           MembershipRetirementResult::Retired);
    assert(memberships.FindExact(deviceA, incarnationA) == nullptr);
    const auto* leave = tombstones.FindRetained(deviceA, incarnationA);
    assert(leave != nullptr);
    assert(leave->Disposition == MembershipTombstoneDisposition::AuthoritativeLeave);

    const auto deviceB = Device(2);
    const auto incarnationB = Incarnation(2);
    assert(memberships.UpsertAuthenticated(
        deviceB,
        incarnationB,
        MembershipState::Active,
        ReachabilityState::Unreachable
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(retention.RecordLocallyForgotten(deviceB, incarnationB, 2000) ==
           MembershipRetirementResult::Retired);
    const auto* forgotten = tombstones.FindRetained(deviceB, incarnationB);
    assert(forgotten != nullptr);
    assert(forgotten->Disposition == MembershipTombstoneDisposition::LocallyForgotten);

    // Saturation may evict compact history but never consumes or blocks active-member capacity.
    const auto deviceC = Device(3);
    const auto incarnationC = Incarnation(3);
    assert(memberships.UpsertAuthenticated(
        deviceC,
        incarnationC,
        MembershipState::Joining
    ) == AuthenticatedMembershipInsertResult::Inserted);
    assert(retention.RecordSupersededIncarnation(deviceC, incarnationC, 3000) ==
           MembershipRetirementResult::Retired);
    assert(tombstones.Size() == 2);
    assert(tombstones.FindRetained(deviceC, incarnationC) != nullptr);

    // With a full two-entry table, LocallyForgotten is the deterministic first eviction class.
    assert(tombstones.FindRetained(deviceB, incarnationB) == nullptr);
    assert(tombstones.FindRetained(deviceA, incarnationA) != nullptr);

    assert(retention.RecordAuthoritativeLeave(deviceA, Incarnation(9), 4000) ==
           MembershipRetirementResult::MembershipNotFound);

    return 0;
}
