#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ESPressio_MeshV1BroadcastCoordinator.hpp"

using namespace ESPressio;

namespace {

System::DeviceIdentifier Device(std::uint8_t value) {
    System::DeviceIdentifier::Storage bytes{};
    bytes.back()=value;
    return System::DeviceIdentifier{bytes};
}

Mesh::MembershipIncarnation Incarnation(std::uint8_t value) {
    Mesh::MembershipIncarnation::Storage bytes{};
    bytes.back()=value;
    return Mesh::MembershipIncarnation{bytes};
}

Mesh::MeshIdentifier MeshId(std::uint8_t value) {
    Mesh::MeshIdentifier::Storage bytes{};
    bytes.back()=value;
    return Mesh::MeshIdentifier{bytes};
}

Mesh::MeshSecuritySessionIdentifier SessionId(std::uint8_t value) {
    Mesh::MeshSecuritySessionIdentifier id{};
    id.Value.fill(value);
    return id;
}

class Provider final : public Mesh::IMeshV1CryptographicProvider {
public:
    bool GenerateEphemeralKey(Mesh::MeshEphemeralKeyHandle&,Mesh::MeshEphemeralPublicKey&) noexcept override {
        return false;
    }

    bool GenerateHandshakeNonce(Mesh::MeshHandshakeNonce&) noexcept override {
        return false;
    }

    bool Hash(const std::uint8_t* bytes,std::size_t size,Mesh::MeshSecurityDigest& digest) noexcept override {
        if(!bytes||!size) return false;
        digest={};
        for(std::size_t i=0;i<size;++i)
            digest.Value[i%digest.Value.size()]^=bytes[i];
        digest.Value[0]^=0x5AU;
        return true;
    }

    bool SignIdentityDigest(
        const System::DeviceIdentifier& device,
        const Mesh::MeshSecurityDigest& digest,
        Mesh::MeshIdentitySignature& signature) noexcept override {
        if(!device||!digest) return false;
        signature={};
        std::memcpy(signature.Value.data(),digest.Value.data(),digest.Value.size());
        return true;
    }

    Mesh::MeshIdentityVerificationResult VerifyRegisteredIdentityDigest(
        const System::DeviceIdentifier&,
        const Mesh::MeshSecurityDigest&,
        const Mesh::MeshIdentitySignature&) noexcept override {
        return Mesh::MeshIdentityVerificationResult::Verified;
    }

    bool DeriveSession(
        Mesh::MeshEphemeralKeyHandle,
        const Mesh::MeshEphemeralPublicKey&,
        const Mesh::MeshIdentifier&,
        const Mesh::MeshSecurityChannelBinding&,
        const System::DeviceIdentifier&,
        const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&,
        const System::DeviceIdentifier&,
        const Mesh::MembershipIncarnation&,
        const Mesh::MeshHandshakeNonce&,
        const Mesh::MeshSecurityDigest&,
        Mesh::MeshSecuritySessionRole,
        Mesh::MeshSecuritySessionHandle&,
        Mesh::MeshSecuritySessionIdentifier&) noexcept override {
        return false;
    }

    bool Seal(
        Mesh::MeshSecuritySessionHandle,
        Mesh::MeshSecurityTrafficPurpose,
        std::uint64_t,
        const std::uint8_t*,
        std::size_t,
        const std::uint8_t* plaintext,
        std::size_t plaintextBytes,
        std::uint8_t* ciphertext,
        Mesh::MeshAuthenticationTag& tag) noexcept override {
        if(!plaintext||!plaintextBytes||!ciphertext) return false;
        std::memcpy(ciphertext,plaintext,plaintextBytes);
        tag.Value.fill(0xA5U);
        return true;
    }

    bool Open(
        Mesh::MeshSecuritySessionHandle,
        Mesh::MeshSecurityTrafficPurpose,
        std::uint64_t,
        const std::uint8_t*,
        std::size_t,
        const std::uint8_t*,
        std::size_t,
        const Mesh::MeshAuthenticationTag&,
        std::uint8_t*) noexcept override {
        return false;
    }

    bool ReleaseEphemeralKey(Mesh::MeshEphemeralKeyHandle) noexcept override {
        return true;
    }

    bool ReleaseSession(Mesh::MeshSecuritySessionHandle) noexcept override {
        return true;
    }

    void ResetForControlledShutdown() noexcept override {}
};

class Receiver final : public Mesh::IPrimitiveReceiver {
public:
    unsigned Calls=0;

    Primitive::PrimitiveAdmissionDisposition Receive(
        const Mesh::MeshReceiveContext&,
        Primitive::PrimitiveProtocolVersion,
        Mesh::PrimitivePayloadView) noexcept override {
        ++Calls;
        return Primitive::PrimitiveAdmissionDisposition::Accepted;
    }
};

struct RadioCapture final {
    unsigned Calls=0;

    static Mesh::MeshRadioSubmissionResult Submit(
        void* context,
        Radio::RadioPeerHandle,
        Mesh::MeshRelayServiceClass,
        std::uint64_t,
        const std::uint8_t*,
        std::size_t) noexcept {
        auto& self=*static_cast<RadioCapture*>(context);
        ++self.Calls;
        return {true,1};
    }

    Mesh::MeshRadioSubmissionTarget Target() noexcept {
        return {this,&Submit};
    }
};

struct Policy final {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::NoRemoteEvidence;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=500'000'000ULL;
    static constexpr std::uint16_t MaximumAttempts=1;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=0;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=0;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=0;
};

} // namespace

int main() {
    const auto local=Device(1);
    const auto remote=Device(2);
    const auto localInc=Incarnation(1);
    const auto remoteInc=Incarnation(2);

    Mesh::AuthenticatedMembershipTable<2> memberships;
    assert(memberships.UpsertAuthenticated(
        remote,remoteInc,Mesh::MembershipState::Active,Mesh::ReachabilityState::Reachable)==
        Mesh::AuthenticatedMembershipInsertResult::Inserted);

    Mesh::AuthenticatedDirectPeerBindingTable<2> bindings;
    const Radio::RadioPeerHandle peer{1,1};
    assert(bindings.Bind({remote,remoteInc,1,peer})==Mesh::DirectPeerBindingResult::Bound);

    Provider provider;
    Mesh::MeshSecuritySessionTable<2> sessions;
    Mesh::MeshSecuritySessionRecordHandle sessionHandle{};
    assert(sessions.Install(
        remote,remoteInc,SessionId(3),Mesh::MeshSecuritySessionHandle{1,1},provider,sessionHandle));

    Mesh::PrimitiveReceiverRegistry<Mesh::Limits::MaxPrimitiveReceivers> receivers;
    Receiver localReceiver;
    Mesh::PrimitiveReceiverHandle receiverHandle{};
    assert(receivers.Register(
        {Primitive::FamilyIds::Event,{1,1},{},Mesh::PrimitiveReceiverExposure::Advertised},
        localReceiver,receiverHandle)==Mesh::PrimitiveReceiverRegistrationResult::Registered);

    RadioCapture radio;
    Mesh::MeshV1FrameWorkspace<512,512> workspace;
    Mesh::MeshMessageIdGenerator ids;
    Mesh::MeshV1BroadcastCoordinator<512,512,2,2,2,2> coordinator(
        memberships,bindings,sessions,provider,receivers,radio.Target(),workspace,ids,
        MeshId(9),local,localInc);

    Mesh::MeshBroadcastFanoutPlan<2> plan;
    assert(plan.TryAdd({remote,remoteInc,1,peer}));
    constexpr auto policy=Mesh::MakeMeshBroadcastSubmissionPolicy<Policy>(
        Primitive::FamilyIds::Event,Mesh::MeshRelayServiceClass::BestEffort);
    const std::array<std::uint8_t,2> payloadBytes{{7,8}};
    const auto submitted=coordinator.Submit(
        policy,{Primitive::FamilyIds::Event,1},
        Mesh::ApplicationPayload::Borrowed(payloadBytes.data(),payloadBytes.size()),
        1'000'000'000ULL,100,2,plan);

    assert(submitted.Disposition==Mesh::MeshV1BroadcastDisposition::Completed);
    assert(radio.Calls==1);
    assert(localReceiver.Calls==0);
    return 0;
}
