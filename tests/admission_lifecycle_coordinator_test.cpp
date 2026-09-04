#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_AdmissionLifecycleCoordinator.hpp>

using namespace ESPressio::Mesh;

static ESPressio::System::DeviceIdentifier Device(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return ESPressio::System::DeviceIdentifier{bytes};
}

static MembershipIncarnation Incarnation(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return MembershipIncarnation{bytes};
}

class SecurityAuthority final : public IMeshSecurityAuthority {
public:
    MeshAuthenticationDisposition Next{MeshAuthenticationDisposition::Authenticated};
    AuthenticatedMeshIdentity Identity{Device(9), Incarnation(9)};
    unsigned Calls{0};

    MeshAuthenticationDisposition EvaluateCandidate(
        const MeshSecurityCandidateContext& candidate,
        std::uint64_t,
        AuthenticatedMeshIdentity& identity
    ) noexcept override {
        ++Calls;
        assert(candidate);
        identity = Next == MeshAuthenticationDisposition::Authenticated ? Identity : AuthenticatedMeshIdentity{};
        return Next;
    }
};

class AdmissionPolicy final : public IMeshAdmissionPolicy {
public:
    MeshAdmissionDisposition Next{MeshAdmissionDisposition::Admit};
    mutable unsigned Calls{0};
    mutable AuthenticatedMeshIdentity LastIdentity{};

    MeshAdmissionDisposition EvaluateAdmission(const MeshAdmissionContext& context) const noexcept override {
        ++Calls;
        assert(context.IsValid());
        LastIdentity = context.Identity;
        return Next;
    }
};

static NeighbourCandidateHandle Observe(
    PendingNeighbourCandidateTable<3>& candidates,
    std::uint16_t peerSlot,
    std::uint8_t claimTail,
    std::uint64_t now
) {
    NeighbourCandidateHandle handle{};
    assert(candidates.Observe(
               1,
               ESPressio::Radio::RadioPeerHandle{peerSlot, 1},
               UntrustedMembershipClaim{Device(claimTail), Incarnation(claimTail)},
               now,
               handle) == PendingCandidateInsertResult::Inserted);
    return handle;
}

int main() {
    PendingNeighbourCandidateTable<3> candidates;
    InboundAuthenticationReservationTable<1> authentications;
    AuthenticatedMembershipTable<2> memberships;
    AdmissionPromotionCoordinator<3, 1, 2> promotion(candidates, authentications, memberships);
    SecurityAuthority security;
    AdmissionPolicy admission;
    AdmissionLifecycleCoordinator<3, 1, 2> lifecycle(
        candidates, authentications, memberships, promotion, security, admission);

    // Pending security work retains its expensive-work reservation and Authenticating state.
    const auto pending = Observe(candidates, 0, 1, 100);
    security.Next = MeshAuthenticationDisposition::Pending;
    assert(lifecycle.Evaluate(pending, 110) == AdmissionLifecycleResult::AuthenticationPending);
    assert(authentications.Contains(pending));
    assert(candidates.Resolve(pending)->State == MembershipState::Authenticating);

    // A second candidate cannot consume authentication capacity while the first remains pending.
    const auto blocked = Observe(candidates, 1, 2, 120);
    assert(lifecycle.Evaluate(blocked, 130) == AdmissionLifecycleResult::AuthenticationResourceUnavailable);
    assert(candidates.Resolve(blocked)->State == MembershipState::Discovered);

    // Retryable security releases expensive work but preserves discovery state.
    security.Next = MeshAuthenticationDisposition::Retryable;
    assert(lifecycle.Evaluate(pending, 140) == AdmissionLifecycleResult::Retryable);
    assert(!authentications.Contains(pending));
    assert(candidates.Resolve(pending)->State == MembershipState::Discovered);

    // Authenticated identity, not untrusted claim, is the only identity seen by admission and membership.
    security.Next = MeshAuthenticationDisposition::Authenticated;
    security.Identity = {Device(9), Incarnation(9)};
    admission.Next = MeshAdmissionDisposition::Admit;
    AuthenticatedDirectPeerBinding binding{};
    assert(lifecycle.Evaluate(pending, 150, &binding) == AdmissionLifecycleResult::PromotedToValidating);
    assert(candidates.Resolve(pending) == nullptr);
    assert(!authentications.Contains(pending));
    assert(admission.LastIdentity.Device == Device(9));
    assert(admission.LastIdentity.Incarnation == Incarnation(9));
    assert(memberships.FindExact(Device(9), Incarnation(9)) != nullptr);
    assert(memberships.FindExact(Device(1), Incarnation(1)) == nullptr);
    assert(binding.IsValid());
    assert(binding.Neighbour == Device(9));
    assert(binding.Incarnation == Incarnation(9));

    // Admission defer returns a separately authenticated candidate to Discovered for a later complete attempt.
    const auto deferred = blocked;
    security.Identity = {Device(10), Incarnation(10)};
    admission.Next = MeshAdmissionDisposition::Defer;
    assert(lifecycle.Evaluate(deferred, 160) == AdmissionLifecycleResult::AdmissionDeferred);
    assert(candidates.Resolve(deferred) != nullptr);
    assert(candidates.Resolve(deferred)->State == MembershipState::Discovered);
    assert(!authentications.Contains(deferred));
    assert(memberships.FindDevice(Device(10)) == nullptr);

    // Definitive admission rejection removes pre-auth state and creates no membership.
    const auto rejected = Observe(candidates, 2, 3, 170);
    security.Identity = {Device(11), Incarnation(11)};
    admission.Next = MeshAdmissionDisposition::Reject;
    assert(lifecycle.Evaluate(rejected, 180) == AdmissionLifecycleResult::Rejected);
    assert(candidates.Resolve(rejected) == nullptr);
    assert(!authentications.Contains(rejected));
    assert(memberships.FindDevice(Device(11)) == nullptr);

    // A security implementation claiming Authenticated without a valid identity is rejected as invalid.
    const auto invalidIdentity = Observe(candidates, 2, 4, 190);
    security.Next = MeshAuthenticationDisposition::Authenticated;
    security.Identity = {};
    admission.Next = MeshAdmissionDisposition::Admit;
    assert(lifecycle.Evaluate(invalidIdentity, 200) == AdmissionLifecycleResult::Invalid);
    assert(candidates.Resolve(invalidIdentity) == nullptr);
    assert(!authentications.Contains(invalidIdentity));

    return 0;
}
