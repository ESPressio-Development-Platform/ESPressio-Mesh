#include <array>
#include <cassert>
#include <cstdint>

#include <ESPressio_MeshSecurityAuthority.hpp>

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

class TestSecurityAuthority final : public Mesh::IMeshSecurityAuthority {
public:
    Mesh::MeshAuthenticationDisposition Disposition{Mesh::MeshAuthenticationDisposition::Pending};
    Mesh::AuthenticatedMeshIdentity Established{};

    Mesh::MeshAuthenticationDisposition EvaluateCandidate(
        const Mesh::MeshSecurityCandidateContext& candidate,
        std::uint64_t nowMilliseconds,
        Mesh::AuthenticatedMeshIdentity& identity
    ) noexcept override {
        identity = {};
        if (!candidate || nowMilliseconds == 0U) return Mesh::MeshAuthenticationDisposition::Invalid;
        if (Disposition == Mesh::MeshAuthenticationDisposition::Authenticated) identity = Established;
        return Disposition;
    }
};

class TestAdmissionPolicy final : public Mesh::IMeshAdmissionPolicy {
public:
    Mesh::MeshAdmissionDisposition Decision{Mesh::MeshAdmissionDisposition::Admit};

    Mesh::MeshAdmissionDisposition EvaluateAdmission(const Mesh::MeshAdmissionContext& context) const noexcept override {
        return context.IsValid() ? Decision : Mesh::MeshAdmissionDisposition::Invalid;
    }
};

int main() {
    const Mesh::MeshSecurityCandidateContext candidate{
        Mesh::NeighbourCandidateHandle{0, 1},
        1,
        Radio::RadioPeerHandle{2, 3},
        Mesh::UntrustedMembershipClaim{Device(1), Incarnation(1)}
    };
    assert(candidate);

    TestSecurityAuthority security;
    Mesh::AuthenticatedMeshIdentity identity{};

    // Pending work establishes no authority and must not leak the untrusted claim as authenticated identity.
    assert(security.EvaluateCandidate(candidate, 100, identity) == Mesh::MeshAuthenticationDisposition::Pending);
    assert(!identity);

    // Authenticated output may deliberately differ from the candidate's untrusted claim.
    security.Disposition = Mesh::MeshAuthenticationDisposition::Authenticated;
    security.Established = {Device(9), Incarnation(9)};
    assert(security.EvaluateCandidate(candidate, 101, identity) == Mesh::MeshAuthenticationDisposition::Authenticated);
    assert(identity);
    assert(identity.Device == Device(9));
    assert(identity.Incarnation == Incarnation(9));
    assert(identity.Device != candidate.Claim.Device);

    TestAdmissionPolicy admission;
    const Mesh::MeshAdmissionContext admissionContext{candidate, identity, 3, 32};
    assert(admission.EvaluateAdmission(admissionContext) == Mesh::MeshAdmissionDisposition::Admit);

    // Admission is independently injectable and occurs only after valid authenticated identity exists.
    admission.Decision = Mesh::MeshAdmissionDisposition::Reject;
    assert(admission.EvaluateAdmission(admissionContext) == Mesh::MeshAdmissionDisposition::Reject);
    assert(admission.EvaluateAdmission({}) == Mesh::MeshAdmissionDisposition::Invalid);

    // Invalid local context is rejected without defining any cryptographic or wire behavior.
    Mesh::MeshSecurityCandidateContext invalid{};
    identity = security.Established;
    assert(security.EvaluateCandidate(invalid, 100, identity) == Mesh::MeshAuthenticationDisposition::Invalid);
    assert(!identity);

    return 0;
}
