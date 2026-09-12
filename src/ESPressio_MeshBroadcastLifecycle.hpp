#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_PrimitiveAdmission.hpp>
#include <ESPressio_PrimitiveFamilyRegistry.hpp>
#include <ESPressio_PrimitiveTypes.hpp>

#include "ESPressio_DeduplicationWindow.hpp"
#include "ESPressio_PrimitiveReceiverRegistry.hpp"

namespace ESPressio::Mesh {

enum class MeshBroadcastNetworkDisposition : std::uint8_t {
    NewlySeen=0,
    SeenNotForwarded,
    Forwarded,
    TooOld,
    Invalid,
    SourceCapacityUnavailable
};

enum class MeshBroadcastForwardCommitResult : std::uint8_t {
    Committed=0,
    AlreadyForwarded,
    NotSeen,
    TooOld,
    Invalid,
    SourceNotFound
};

/// <summary>Bounded authenticated per-source Seen/Forwarded history independent of local family admission.</summary>
template<std::size_t TSourceCapacity,std::size_t TWindowBits=Limits::DeduplicationWindowBits>
class MeshBroadcastNetworkStateTable final {
    static_assert(TSourceCapacity>0,"Broadcast network-state source capacity must be non-zero");
    struct Slot final {
        System::DeviceIdentifier Source{};
        MembershipIncarnation Incarnation{};
        DeduplicationWindow<TWindowBits> Seen{};
        DeduplicationWindow<TWindowBits> Forwarded{};
        bool Occupied{false};
    };
    std::array<Slot,TSourceCapacity> _slots{};

    Slot* Find(const System::DeviceIdentifier& source,const MembershipIncarnation& incarnation) noexcept {
        for(auto& slot:_slots)
            if(slot.Occupied&&slot.Source==source&&slot.Incarnation==incarnation) return &slot;
        return nullptr;
    }
    const Slot* Find(const System::DeviceIdentifier& source,const MembershipIncarnation& incarnation) const noexcept {
        for(const auto& slot:_slots)
            if(slot.Occupied&&slot.Source==source&&slot.Incarnation==incarnation) return &slot;
        return nullptr;
    }
    Slot* FindOrCreate(const System::DeviceIdentifier& source,const MembershipIncarnation& incarnation) noexcept {
        if(auto* existing=Find(source,incarnation)) return existing;
        for(auto& slot:_slots){
            if(slot.Occupied) continue;
            slot.Source=source;slot.Incarnation=incarnation;slot.Seen.Reset();slot.Forwarded.Reset();slot.Occupied=true;
            return &slot;
        }
        return nullptr;
    }
public:
    MeshBroadcastNetworkDisposition CommitAuthenticatedSeen(
        const System::DeviceIdentifier& source,
        const MembershipIncarnation& incarnation,
        MeshMessageId messageId) noexcept {
        if(!source||!incarnation||messageId==0) return MeshBroadcastNetworkDisposition::Invalid;
        auto* slot=FindOrCreate(source,incarnation);
        if(slot==nullptr) return MeshBroadcastNetworkDisposition::SourceCapacityUnavailable;
        const auto seen=slot->Seen.Classify(messageId);
        if(seen==DeduplicationDisposition::TooOld) return MeshBroadcastNetworkDisposition::TooOld;
        if(seen==DeduplicationDisposition::Invalid) return MeshBroadcastNetworkDisposition::Invalid;
        if(seen==DeduplicationDisposition::Unseen){
            (void)slot->Seen.Commit(messageId);
            return MeshBroadcastNetworkDisposition::NewlySeen;
        }
        const auto forwarded=slot->Forwarded.Classify(messageId);
        return forwarded==DeduplicationDisposition::Duplicate
            ? MeshBroadcastNetworkDisposition::Forwarded
            : MeshBroadcastNetworkDisposition::SeenNotForwarded;
    }

    MeshBroadcastForwardCommitResult CommitForwarded(
        const System::DeviceIdentifier& source,
        const MembershipIncarnation& incarnation,
        MeshMessageId messageId) noexcept {
        if(!source||!incarnation||messageId==0) return MeshBroadcastForwardCommitResult::Invalid;
        auto* slot=Find(source,incarnation);
        if(slot==nullptr) return MeshBroadcastForwardCommitResult::SourceNotFound;
        const auto seen=slot->Seen.Classify(messageId);
        if(seen==DeduplicationDisposition::TooOld) return MeshBroadcastForwardCommitResult::TooOld;
        if(seen!=DeduplicationDisposition::Duplicate) return MeshBroadcastForwardCommitResult::NotSeen;
        const auto forwarded=slot->Forwarded.Commit(messageId);
        if(forwarded==DeduplicationDisposition::Unseen) return MeshBroadcastForwardCommitResult::Committed;
        if(forwarded==DeduplicationDisposition::Duplicate) return MeshBroadcastForwardCommitResult::AlreadyForwarded;
        return forwarded==DeduplicationDisposition::TooOld
            ? MeshBroadcastForwardCommitResult::TooOld
            : MeshBroadcastForwardCommitResult::Invalid;
    }

    MeshBroadcastNetworkDisposition Classify(
        const System::DeviceIdentifier& source,
        const MembershipIncarnation& incarnation,
        MeshMessageId messageId) const noexcept {
        if(!source||!incarnation||messageId==0) return MeshBroadcastNetworkDisposition::Invalid;
        const auto* slot=Find(source,incarnation);
        if(slot==nullptr) return MeshBroadcastNetworkDisposition::NewlySeen;
        const auto seen=slot->Seen.Classify(messageId);
        if(seen==DeduplicationDisposition::TooOld) return MeshBroadcastNetworkDisposition::TooOld;
        if(seen==DeduplicationDisposition::Unseen) return MeshBroadcastNetworkDisposition::NewlySeen;
        if(seen!=DeduplicationDisposition::Duplicate) return MeshBroadcastNetworkDisposition::Invalid;
        return slot->Forwarded.Classify(messageId)==DeduplicationDisposition::Duplicate
            ? MeshBroadcastNetworkDisposition::Forwarded
            : MeshBroadcastNetworkDisposition::SeenNotForwarded;
    }

    void Forget(const System::DeviceIdentifier& source,const MembershipIncarnation& incarnation) noexcept {
        if(auto* slot=Find(source,incarnation)) *slot={};
    }
    void Clear() noexcept { _slots={}; }
};

enum class MeshDeferredLocalStoreResult : std::uint8_t {
    Stored=0,
    AlreadyRetained,
    ResourceUnavailable,
    PayloadTooLarge,
    Expired,
    Invalid
};

enum class MeshDeferredLocalServiceResult : std::uint8_t {
    NoWork=0,
    Admitted,
    RemainsDeferred,
    Terminal,
    Expired
};

/// <summary>One completely-owned immutable local-family retry record.</summary>
template<std::size_t TMaximumPayloadBytes>
struct MeshDeferredLocalRecord final {
    System::DeviceIdentifier Source{};
    MembershipIncarnation SourceIncarnation{};
    MeshMessageId MessageId{0};
    RemainingHopLimit RemainingHops{0};
    Primitive::PrimitiveFamilyId Family{Primitive::FamilyIds::Invalid};
    Primitive::PrimitiveProtocolVersion Version{0};
    std::uint64_t ExpiryMonotonicNanoseconds{0};
    std::uint64_t ObservedAdmissionGeneration{0};
    std::uint16_t PayloadBytes{0};
    std::array<std::uint8_t,TMaximumPayloadBytes> Payload{};
    bool Occupied{false};
};

/// <summary>
/// Fixed DeferredLocal ownership. Service is invoked only by a lifecycle/capacity wake with a changed admission
/// generation; this class creates no timer, polling loop or network retry. Duplicate store cannot reset expiry or bytes.
/// </summary>
template<std::size_t TCapacity,std::size_t TMaximumPayloadBytes>
class MeshDeferredLocalTable final {
    static_assert(TCapacity>0&&TMaximumPayloadBytes>0,"DeferredLocal capacity must be finite and non-zero");
    std::array<MeshDeferredLocalRecord<TMaximumPayloadBytes>,TCapacity> _records{};
    std::size_t _cursor{0};

    MeshDeferredLocalRecord<TMaximumPayloadBytes>* Find(
        const System::DeviceIdentifier& source,const MembershipIncarnation& incarnation,MeshMessageId messageId) noexcept {
        for(auto& record:_records)
            if(record.Occupied&&record.Source==source&&record.SourceIncarnation==incarnation&&record.MessageId==messageId)
                return &record;
        return nullptr;
    }
public:
    static constexpr std::size_t Capacity=TCapacity;
    static constexpr std::size_t MaximumPayloadBytes=TMaximumPayloadBytes;

    MeshDeferredLocalStoreResult Store(
        const MeshReceiveContext& context,
        Primitive::PrimitiveFamilyId family,
        Primitive::PrimitiveProtocolVersion version,
        PrimitivePayloadView payload,
        std::uint64_t expiryMonotonicNanoseconds,
        std::uint64_t observedAdmissionGeneration,
        std::uint64_t monotonicNowNanoseconds) noexcept {
        if(!context.IsValid()||!Primitive::FamilyIds::IsUsable(family)||family==Primitive::FamilyIds::MeshControl||
           version==0||!payload.IsValid()||payload.Size==0||expiryMonotonicNanoseconds==0)
            return MeshDeferredLocalStoreResult::Invalid;
        if(expiryMonotonicNanoseconds<=monotonicNowNanoseconds) return MeshDeferredLocalStoreResult::Expired;
        if(payload.Size>TMaximumPayloadBytes||payload.Size>std::numeric_limits<std::uint16_t>::max())
            return MeshDeferredLocalStoreResult::PayloadTooLarge;
        if(Find(context.Source,context.SourceIncarnation,context.DeliveryMessageId)!=nullptr)
            return MeshDeferredLocalStoreResult::AlreadyRetained;
        for(auto& record:_records){
            if(record.Occupied) continue;
            record.Source=context.Source;
            record.SourceIncarnation=context.SourceIncarnation;
            record.MessageId=context.DeliveryMessageId;
            record.RemainingHops=context.RemainingHops;
            record.Family=family;
            record.Version=version;
            record.ExpiryMonotonicNanoseconds=expiryMonotonicNanoseconds;
            record.ObservedAdmissionGeneration=observedAdmissionGeneration;
            record.PayloadBytes=static_cast<std::uint16_t>(payload.Size);
            for(std::size_t i=0;i<payload.Size;++i) record.Payload[i]=payload.Data[i];
            record.Occupied=true;
            return MeshDeferredLocalStoreResult::Stored;
        }
        return MeshDeferredLocalStoreResult::ResourceUnavailable;
    }

    MeshDeferredLocalServiceResult ServiceOne(
        PrimitiveReceiverRegistry<Limits::MaxPrimitiveReceivers>& receivers,
        std::uint64_t currentAdmissionGeneration,
        std::uint64_t monotonicNowNanoseconds,
        Primitive::PrimitiveAdmissionDisposition& disposition) noexcept {
        disposition=Primitive::PrimitiveAdmissionDisposition::Malformed;
        for(std::size_t visited=0;visited<TCapacity;++visited){
            const auto index=(_cursor+visited)%TCapacity;
            auto& record=_records[index];
            if(!record.Occupied) continue;
            if(record.ExpiryMonotonicNanoseconds<=monotonicNowNanoseconds){
                record={};_cursor=(index+1)%TCapacity;return MeshDeferredLocalServiceResult::Expired;
            }
            if(record.ObservedAdmissionGeneration==currentAdmissionGeneration) continue;
            const MeshReceiveContext context{record.Source,record.SourceIncarnation,record.MessageId,record.RemainingHops,true};
            const auto dispatch=receivers.Dispatch(record.Family,record.Version,context,
                {record.Payload.data(),record.PayloadBytes},disposition);
            record.ObservedAdmissionGeneration=currentAdmissionGeneration;
            _cursor=(index+1)%TCapacity;
            if(dispatch!=PrimitiveDispatchResult::Dispatched ||
               disposition==Primitive::PrimitiveAdmissionDisposition::Unsupported ||
               disposition==Primitive::PrimitiveAdmissionDisposition::Rejected ||
               disposition==Primitive::PrimitiveAdmissionDisposition::Malformed){
                record={};return MeshDeferredLocalServiceResult::Terminal;
            }
            if(Primitive::EstablishesDestinationAdmission(disposition)){
                record={};return MeshDeferredLocalServiceResult::Admitted;
            }
            if(Primitive::IsAdmissionRetryCandidate(disposition)) return MeshDeferredLocalServiceResult::RemainsDeferred;
            record={};return MeshDeferredLocalServiceResult::Terminal;
        }
        return MeshDeferredLocalServiceResult::NoWork;
    }

    std::size_t Expire(std::uint64_t monotonicNowNanoseconds) noexcept {
        std::size_t released=0;
        for(auto& record:_records){if(record.Occupied&&record.ExpiryMonotonicNanoseconds<=monotonicNowNanoseconds){record={};++released;}}
        return released;
    }
    void Clear() noexcept { _records={};_cursor=0; }
};

} // namespace ESPressio::Mesh
