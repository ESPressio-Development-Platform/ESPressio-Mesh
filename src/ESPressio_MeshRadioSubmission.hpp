#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioRuntime.hpp>
#include <ESPressio_RadioServiceProfile.hpp>

#include "ESPressio_MeshRelayCapacity.hpp"
#include "ESPressio_MeshTypes.hpp"

namespace ESPressio::Mesh {

struct MeshRadioSubmissionResult final {
    Radio::RadioSchedulerStatus Status{Radio::RadioSchedulerStatus::InvalidConfiguration};
    Radio::RadioTransferId TransferId{0};

    constexpr explicit operator bool() const noexcept {
        return Status==Radio::RadioSchedulerStatus::Success&&TransferId!=0;
    }
};

/// <summary>Fixed family-opaque Mesh -> managed Radio submission thunk.</summary>
struct MeshRadioSubmissionTarget final {
    void* Context{nullptr};
    MeshRadioSubmissionResult (*Submit)(
        void*,Radio::RadioPeerHandle,MeshRelayServiceClass,std::uint64_t,
        const std::uint8_t*,std::size_t) noexcept{nullptr};

    constexpr explicit operator bool() const noexcept { return Context!=nullptr&&Submit!=nullptr; }

    MeshRadioSubmissionResult SubmitPeer(
        Radio::RadioPeerHandle peer,
        MeshRelayServiceClass service,
        std::uint64_t expiryMonotonicNanoseconds,
        const std::uint8_t* bytes,
        std::size_t byteCount) const noexcept {
        if(!*this||!peer||!IsMeshRelayServiceClass(service)||expiryMonotonicNanoseconds==0||bytes==nullptr||byteCount==0)
            return {};
        return Submit(Context,peer,service,expiryMonotonicNanoseconds,bytes,byteCount);
    }
};

/// <summary>Fixed Mesh-owned resolver from a local RadioIdentifier to its final Radio contention domain.</summary>
struct MeshRadioDomainResolver final {
    void* Context{nullptr};
    Radio::RadioContentionDomainId (*Resolve)(void*,RadioIdentifier) noexcept{nullptr};

    constexpr explicit operator bool() const noexcept { return Context!=nullptr&&Resolve!=nullptr; }
    Radio::RadioContentionDomainId ResolveLocalRadio(RadioIdentifier radio) const noexcept {
        return *this&&radio!=0U?Resolve(Context,radio):Radio::RadioContentionDomainId{};
    }
};

constexpr Radio::RadioServiceClass ToRadioServiceClass(MeshRelayServiceClass service) noexcept {
    switch(service){
        case MeshRelayServiceClass::Infrastructure:return Radio::RadioServiceClass::Infrastructure;
        case MeshRelayServiceClass::Clock:return Radio::RadioServiceClass::Clock;
        case MeshRelayServiceClass::Critical:return Radio::RadioServiceClass::Critical;
        case MeshRelayServiceClass::Responsive:return Radio::RadioServiceClass::Responsive;
        case MeshRelayServiceClass::Convergent:return Radio::RadioServiceClass::Convergent;
        case MeshRelayServiceClass::BestEffort:return Radio::RadioServiceClass::BestEffort;
    }
    return Radio::RadioServiceClass::Invalid;
}

template<class TRadioRuntime>
MeshRadioSubmissionTarget MakeMeshRadioSubmissionTarget(TRadioRuntime& runtime) noexcept {
    return MeshRadioSubmissionTarget{
        &runtime,
        [](void* context,Radio::RadioPeerHandle peer,MeshRelayServiceClass service,
           std::uint64_t expiry,const std::uint8_t* bytes,std::size_t byteCount) noexcept -> MeshRadioSubmissionResult {
            const auto radioService=ToRadioServiceClass(service);
            if(!Radio::IsValidRadioServiceClass(radioService)) return {};
            const Radio::RadioServiceProfile profile{
                radioService,
                service==MeshRelayServiceClass::Clock
                    ? Radio::RadioDeadlineTreatment::Promotable
                    : Radio::RadioDeadlineTreatment::ExpiryOnly,
                Radio::RadioDirectLinkEvidenceRequirement::TransmissionCompletion
            };
            const Radio::RadioTransferTiming timing{
                expiry,
                service==MeshRelayServiceClass::Clock?expiry:0
            };
            const auto submitted=static_cast<TRadioRuntime*>(context)->SubmitPeer(peer,profile,timing,bytes,byteCount);
            return {submitted.Status,submitted.TransferId};
        }
    };
}

template<class TRadioRegistry>
MeshRadioDomainResolver MakeMeshRadioDomainResolver(TRadioRegistry& registry) noexcept {
    return MeshRadioDomainResolver{
        &registry,
        [](void* context,RadioIdentifier identifier) noexcept -> Radio::RadioContentionDomainId {
            auto* radio=static_cast<TRadioRegistry*>(context)->Resolve(identifier);
            return radio!=nullptr?radio->ContentionDomain():Radio::RadioContentionDomainId{};
        }
    };
}

} // namespace ESPressio::Mesh
