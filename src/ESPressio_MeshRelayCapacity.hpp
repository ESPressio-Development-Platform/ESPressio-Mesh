#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ESPressio_Synchronization.hpp>

namespace ESPressio::Mesh {

/// <summary>Closed neutral Mesh relay service taxonomy; ordering matches the locked A1/A2/Radio classes.</summary>
enum class MeshRelayServiceClass : std::uint8_t {
    Infrastructure=0, Clock=1, Critical=2, Responsive=3, Convergent=4, BestEffort=5
};
inline constexpr std::size_t MeshRelayServiceClassCount=6;

/// <summary>Independent relay capacity direction.</summary>
enum class MeshRelayDirection : std::uint8_t { Inbound=0, Outbound=1 };

/// <summary>Six private domains plus opportunistic SharedOverflow and inbound-only quarantine.</summary>
enum class MeshRelayCapacityDomainKind : std::uint8_t {
    InfrastructurePrivate=0, ClockPrivate=1, CriticalPrivate=2, ResponsivePrivate=3,
    ConvergentPrivate=4, BestEffortPrivate=5, SharedOverflow=6, UntrustedIngress=7
};

constexpr MeshRelayCapacityDomainKind MeshRelayPrivateDomainFor(MeshRelayServiceClass service) noexcept {
    return static_cast<MeshRelayCapacityDomainKind>(static_cast<std::uint8_t>(service));
}
constexpr bool IsMeshRelayServiceClass(MeshRelayServiceClass service) noexcept {
    return static_cast<std::uint8_t>(service)<MeshRelayServiceClassCount;
}

enum class MeshRelayResourceStatus : std::uint8_t {
    Success=0, Busy, Exhausted, TooLarge, InvalidLease, InvalidLength,
    AlreadyCommitted, GenerationExhausted, InvalidConfiguration
};

struct MeshRelayByteView final {
    const std::uint8_t* Data{nullptr};
    std::size_t Size{0};
    constexpr explicit operator bool() const noexcept { return Data!=nullptr || Size==0; }
};
struct MeshRelayMutableByteView final {
    std::uint8_t* Data{nullptr};
    std::size_t Capacity{0};
    constexpr explicit operator bool() const noexcept { return Data!=nullptr; }
};
struct MeshRelayWorkspaceView final {
    std::uint8_t* Data{nullptr};
    std::size_t Capacity{0};
    constexpr explicit operator bool() const noexcept { return Data!=nullptr || Capacity==0; }
};

struct MeshRelayByteIdentity final {
    std::uint16_t ClassIndex{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t SlotIndex{std::numeric_limits<std::uint16_t>::max()};
    std::uint64_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return ClassIndex!=std::numeric_limits<std::uint16_t>::max() &&
               SlotIndex!=std::numeric_limits<std::uint16_t>::max() && Generation!=0;
    }
};

struct MeshRelayRecordIdentity final {
    MeshRelayDirection Direction{MeshRelayDirection::Inbound};
    MeshRelayCapacityDomainKind Domain{MeshRelayCapacityDomainKind::InfrastructurePrivate};
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint64_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return Slot!=std::numeric_limits<std::uint16_t>::max() && Generation!=0;
    }
};

struct MeshRelayCapacityWakeTarget final {
    void* Context{nullptr};
    void (*Wake)(void*) noexcept{nullptr};
};

struct MeshRelayByteClassShape final {
    std::size_t SlotBytes{0};
    std::size_t SlotCount{0};
};

template<std::size_t TSlotBytes,std::size_t TSlotCount>
struct MeshRelayByteClass final {
    static_assert(TSlotBytes>0 && TSlotCount>0,"Mesh relay byte classes must be finite and non-zero");
    static constexpr std::size_t SlotBytes=TSlotBytes;
    static constexpr std::size_t SlotCount=TSlotCount;
};

class MeshRelayByteLease final {
    void* _owner{nullptr};
    bool (*_release)(void*,MeshRelayByteIdentity) noexcept{nullptr};
    std::uint8_t* _data{nullptr};
    std::size_t _capacity{0};
    std::size_t _length{0};
    MeshRelayByteIdentity _identity{};
    bool _sealed{false};

    template<class...> friend class MeshRelayByteArena;
    MeshRelayByteLease(void* owner,bool (*release)(void*,MeshRelayByteIdentity) noexcept,
                       std::uint8_t* data,std::size_t capacity,MeshRelayByteIdentity identity) noexcept
        :_owner(owner),_release(release),_data(data),_capacity(capacity),_identity(identity){}
public:
    MeshRelayByteLease() noexcept=default;
    MeshRelayByteLease(const MeshRelayByteLease&)=delete;
    MeshRelayByteLease& operator=(const MeshRelayByteLease&)=delete;
    MeshRelayByteLease(MeshRelayByteLease&& other) noexcept { *this=std::move(other); }
    MeshRelayByteLease& operator=(MeshRelayByteLease&& other) noexcept {
        if(this==&other)return *this;
        Reset();
        _owner=std::exchange(other._owner,nullptr);_release=std::exchange(other._release,nullptr);
        _data=std::exchange(other._data,nullptr);_capacity=std::exchange(other._capacity,0);
        _length=std::exchange(other._length,0);_identity=std::exchange(other._identity,{});
        _sealed=std::exchange(other._sealed,false);return *this;
    }
    ~MeshRelayByteLease(){Reset();}
    explicit operator bool() const noexcept{return _owner&&_release&&bool(_identity);}
    std::size_t Capacity()const noexcept{return _capacity;}
    std::size_t Length()const noexcept{return _length;}
    bool IsCommitted()const noexcept{return _sealed;}
    MeshRelayMutableByteView MutableView()noexcept{return (!_sealed&&*this)?MeshRelayMutableByteView{_data,_capacity}:MeshRelayMutableByteView{};}
    MeshRelayByteView View()const noexcept{return (_sealed&&*this)?MeshRelayByteView{_data,_length}:MeshRelayByteView{};}
    MeshRelayResourceStatus Commit(std::size_t length)noexcept{
        if(!*this)return MeshRelayResourceStatus::InvalidLease;
        if(_sealed)return MeshRelayResourceStatus::AlreadyCommitted;
        if(length>_capacity)return MeshRelayResourceStatus::InvalidLength;
        _length=length;_sealed=true;return MeshRelayResourceStatus::Success;
    }
    bool Reset()noexcept{
        if(!_owner||!_release||!_identity){_owner=nullptr;_release=nullptr;_data=nullptr;_capacity=0;_length=0;_identity={};_sealed=false;return false;}
        auto* owner=_owner;auto release=_release;auto identity=_identity;
        _owner=nullptr;_release=nullptr;_data=nullptr;_capacity=0;_length=0;_identity={};_sealed=false;
        return release(owner,identity);
    }
};

namespace Detail {
template<class TClass>
class MeshRelayByteClassStorage final {
    struct Slot final { std::array<std::uint8_t,TClass::SlotBytes> Bytes{};std::uint64_t Generation{0};bool Occupied{false}; };
    std::array<Slot,TClass::SlotCount> _slots{};
    System::Synchronization::Mutex _mutex;
public:
    void Initialize()noexcept{std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}
    MeshRelayResourceStatus TryAcquire(std::uint16_t classIndex,MeshRelayByteIdentity& identity,std::uint8_t*& data)noexcept{
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return MeshRelayResourceStatus::Busy;
        bool generationBlocked=false;
        for(std::size_t i=0;i<_slots.size();++i){auto& slot=_slots[i];if(slot.Occupied)continue;
            if(slot.Generation==std::numeric_limits<std::uint64_t>::max()){generationBlocked=true;continue;}
            ++slot.Generation;slot.Occupied=true;identity={classIndex,static_cast<std::uint16_t>(i),slot.Generation};data=slot.Bytes.data();return MeshRelayResourceStatus::Success;}
        return generationBlocked?MeshRelayResourceStatus::GenerationExhausted:MeshRelayResourceStatus::Exhausted;
    }
    bool Release(std::uint16_t slot,std::uint64_t generation)noexcept{
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);if(slot>=_slots.size())return false;
        auto& value=_slots[slot];if(!value.Occupied||value.Generation!=generation)return false;value.Occupied=false;return true;
    }
};
template<class First,class...Rest>
struct MeshRelayStrictAscending:std::bool_constant<((First::SlotBytes<Rest::SlotBytes)&&...)&&MeshRelayStrictAscending<Rest...>::value>{};
template<class Last>struct MeshRelayStrictAscending<Last>:std::true_type{};
}

template<class...TClasses>
class MeshRelayByteArena final {
    static_assert(sizeof...(TClasses)>0,"Mesh relay byte arena requires at least one class");
    static_assert(Detail::MeshRelayStrictAscending<TClasses...>::value,"Mesh relay byte classes must be strictly ascending");
    std::tuple<Detail::MeshRelayByteClassStorage<TClasses>...> _classes{};
    template<std::size_t Index>
    MeshRelayResourceStatus AcquireAt(std::size_t requested,MeshRelayByteLease& output,MeshRelayResourceStatus previous)noexcept{
        if constexpr(Index==sizeof...(TClasses))return previous;
        else {using C=std::tuple_element_t<Index,std::tuple<TClasses...>>;if(requested>C::SlotBytes)return AcquireAt<Index+1>(requested,output,previous);
            auto& storage=std::get<Index>(_classes);MeshRelayByteIdentity identity{};std::uint8_t* data=nullptr;
            const auto status=storage.TryAcquire(static_cast<std::uint16_t>(Index),identity,data);
            if(status==MeshRelayResourceStatus::Success){output=MeshRelayByteLease(this,&ReleaseThunk,data,C::SlotBytes,identity);return status;}
            if(status==MeshRelayResourceStatus::Busy)return status;
            if(status==MeshRelayResourceStatus::GenerationExhausted)previous=status;
            return AcquireAt<Index+1>(requested,output,previous);}
    }
    static bool ReleaseThunk(void* owner,MeshRelayByteIdentity identity)noexcept{return static_cast<MeshRelayByteArena*>(owner)->Release(identity);}
    template<std::size_t Index=0> bool ReleaseAt(MeshRelayByteIdentity identity)noexcept{
        if constexpr(Index==sizeof...(TClasses))return false;
        else if(Index==identity.ClassIndex)return std::get<Index>(_classes).Release(identity.SlotIndex,identity.Generation);
        else return ReleaseAt<Index+1>(identity);
    }
public:
    void Initialize()noexcept{std::apply([](auto&...c){(c.Initialize(),...);},_classes);}
    static constexpr std::size_t ClassCount=sizeof...(TClasses);
    static constexpr std::size_t LargestSlotBytes()noexcept{return std::tuple_element_t<sizeof...(TClasses)-1,std::tuple<TClasses...>>::SlotBytes;}
    static constexpr std::array<MeshRelayByteClassShape,ClassCount> Shapes()noexcept{return {{{TClasses::SlotBytes,TClasses::SlotCount}...}};}
    MeshRelayResourceStatus TryAcquire(std::size_t requested,MeshRelayByteLease& output)noexcept{
        if(output)return MeshRelayResourceStatus::InvalidLease;if(requested==0)return MeshRelayResourceStatus::InvalidLength;
        if(requested>LargestSlotBytes())return MeshRelayResourceStatus::TooLarge;
        return AcquireAt<0>(requested,output,MeshRelayResourceStatus::Exhausted);
    }
    bool Release(MeshRelayByteIdentity identity)noexcept{return identity&&identity.ClassIndex<ClassCount?ReleaseAt(identity):false;}
};

/// <summary>Move-only ownership of one complete relay record + bytes + workspace from one Q1 domain.</summary>
class MeshRelayCapacityBundle final {
    void* _owner{nullptr};
    bool (*_releaseRecord)(void*,MeshRelayRecordIdentity) noexcept{nullptr};
    MeshRelayRecordIdentity _record{};
    std::uint8_t* _workspace{nullptr};
    std::size_t _workspaceBytes{0};
public:
    MeshRelayByteLease Bytes{};
    MeshRelayCapacityBundle()noexcept=default;
    MeshRelayCapacityBundle(const MeshRelayCapacityBundle&)=delete;
    MeshRelayCapacityBundle& operator=(const MeshRelayCapacityBundle&)=delete;
    MeshRelayCapacityBundle(MeshRelayCapacityBundle&& other)noexcept{*this=std::move(other);}
    MeshRelayCapacityBundle& operator=(MeshRelayCapacityBundle&& other)noexcept{
        if(this==&other)return *this;Reset();Bytes=std::move(other.Bytes);_owner=std::exchange(other._owner,nullptr);
        _releaseRecord=std::exchange(other._releaseRecord,nullptr);_record=std::exchange(other._record,{});
        _workspace=std::exchange(other._workspace,nullptr);_workspaceBytes=std::exchange(other._workspaceBytes,0);return *this;
    }
    ~MeshRelayCapacityBundle(){Reset();}
    explicit operator bool()const noexcept{return _owner&&_releaseRecord&&bool(_record)&&bool(Bytes);}
    bool IsCommitted()const noexcept{return *this&&Bytes.IsCommitted();}
    MeshRelayRecordIdentity Identity()const noexcept{return _record;}
    MeshRelayWorkspaceView Workspace()noexcept{return *this?MeshRelayWorkspaceView{_workspace,_workspaceBytes}:MeshRelayWorkspaceView{};}
    bool Reset()noexcept{
        const bool had=static_cast<bool>(*this);Bytes.Reset();if(_owner&&_releaseRecord&&_record)_releaseRecord(_owner,_record);
        _owner=nullptr;_releaseRecord=nullptr;_record={};_workspace=nullptr;_workspaceBytes=0;return had;
    }
private:
    template<std::size_t,std::size_t,class>friend class MeshRelayCapacityDomain;
    void BindRecord(void* owner,bool(*release)(void*,MeshRelayRecordIdentity)noexcept,MeshRelayRecordIdentity identity,std::uint8_t* workspace,std::size_t workspaceBytes)noexcept{
        _owner=owner;_releaseRecord=release;_record=identity;_workspace=workspace;_workspaceBytes=workspaceBytes;
    }
};

template<std::size_t TRecordCount,std::size_t TWorkspaceBytes,class TByteArena>
class MeshRelayCapacityDomain final {
    static_assert(TRecordCount>0&&TRecordCount<std::numeric_limits<std::uint16_t>::max(),"Mesh relay record count invalid");
    struct RecordSlot final {std::array<std::uint8_t,TWorkspaceBytes> Workspace{};std::uint64_t Generation{0};bool Occupied{false};};
    std::array<RecordSlot,TRecordCount> _records{};TByteArena _bytes{};System::Synchronization::Mutex _mutex;
    MeshRelayCapacityWakeTarget _wake{};std::uint64_t _releaseGeneration{0};
    static bool ReleaseThunk(void* owner,MeshRelayRecordIdentity identity)noexcept{return static_cast<MeshRelayCapacityDomain*>(owner)->ReleaseRecord(identity);}
    bool ReleaseRecord(MeshRelayRecordIdentity identity)noexcept{
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);if(identity.Slot>=_records.size())return false;
        auto& slot=_records[identity.Slot];if(!slot.Occupied||slot.Generation!=identity.Generation)return false;slot.Occupied=false;
        for(auto& b:slot.Workspace)b=0;if(_releaseGeneration!=std::numeric_limits<std::uint64_t>::max())++_releaseGeneration;
        if(_wake.Wake)_wake.Wake(_wake.Context);return true;
    }
public:
    static constexpr std::size_t RecordCount=TRecordCount;
    static constexpr std::size_t WorkspaceBytes=TWorkspaceBytes;
    using ByteArena=TByteArena;
    void Initialize(MeshRelayCapacityWakeTarget wake={})noexcept{_wake=wake;_bytes.Initialize();std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}
    std::uint64_t ReleaseGeneration()const noexcept{return _releaseGeneration;}
    MeshRelayResourceStatus TryAcquire(MeshRelayDirection direction,MeshRelayCapacityDomainKind kind,std::size_t byteCount,MeshRelayCapacityBundle& output)noexcept{
        if(output)return MeshRelayResourceStatus::InvalidLease;
        std::uint16_t recordIndex=std::numeric_limits<std::uint16_t>::max();std::uint64_t generation=0;std::uint8_t* workspace=nullptr;
        {
            std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return MeshRelayResourceStatus::Busy;
            bool generationBlocked=false;for(std::size_t i=0;i<_records.size();++i){auto& slot=_records[i];if(slot.Occupied)continue;
                if(slot.Generation==std::numeric_limits<std::uint64_t>::max()){generationBlocked=true;continue;}
                ++slot.Generation;slot.Occupied=true;recordIndex=static_cast<std::uint16_t>(i);generation=slot.Generation;workspace=slot.Workspace.data();break;}
            if(recordIndex==std::numeric_limits<std::uint16_t>::max())return generationBlocked?MeshRelayResourceStatus::GenerationExhausted:MeshRelayResourceStatus::Exhausted;
        }
        MeshRelayByteLease bytes;const auto status=_bytes.TryAcquire(byteCount,bytes);
        if(status!=MeshRelayResourceStatus::Success){ReleaseRecord({direction,kind,recordIndex,generation});return status;}
        output.Bytes=std::move(bytes);output.BindRecord(this,&ReleaseThunk,{direction,kind,recordIndex,generation},workspace,TWorkspaceBytes);return MeshRelayResourceStatus::Success;
    }
};

template<MeshRelayDirection TDirection,class TInfrastructure,class TClock,class TCritical,class TResponsive,
         class TConvergent,class TBestEffort,class TShared,class TUntrusted=void>
class MeshRelayCapacityPlane final {
    TInfrastructure _infrastructure{};TClock _clock{};TCritical _critical{};TResponsive _responsive{};
    TConvergent _convergent{};TBestEffort _bestEffort{};TShared _shared{};
    std::conditional_t<std::is_void_v<TUntrusted>,std::array<std::uint8_t,0>,TUntrusted> _untrusted{};
    template<class TDomain> MeshRelayResourceStatus Acquire(TDomain& domain,MeshRelayCapacityDomainKind kind,std::size_t bytes,MeshRelayCapacityBundle& output)noexcept{
        return domain.TryAcquire(TDirection,kind,bytes,output);
    }
public:
    static_assert(TDirection==MeshRelayDirection::Inbound||std::is_void_v<TUntrusted>,"Only inbound relay planes may own UntrustedIngress");
    void Initialize(MeshRelayCapacityWakeTarget wake={})noexcept{
        _infrastructure.Initialize(wake);_clock.Initialize(wake);_critical.Initialize(wake);_responsive.Initialize(wake);
        _convergent.Initialize(wake);_bestEffort.Initialize(wake);_shared.Initialize(wake);
        if constexpr(!std::is_void_v<TUntrusted>)_untrusted.Initialize(wake);
    }
    MeshRelayResourceStatus TryAcquireTrusted(MeshRelayServiceClass service,std::size_t bytes,MeshRelayCapacityBundle& output)noexcept{
        if(!IsMeshRelayServiceClass(service))return MeshRelayResourceStatus::InvalidConfiguration;
        MeshRelayResourceStatus status=MeshRelayResourceStatus::InvalidConfiguration;
        switch(service){
            case MeshRelayServiceClass::Infrastructure:status=Acquire(_infrastructure,MeshRelayCapacityDomainKind::InfrastructurePrivate,bytes,output);break;
            case MeshRelayServiceClass::Clock:status=Acquire(_clock,MeshRelayCapacityDomainKind::ClockPrivate,bytes,output);break;
            case MeshRelayServiceClass::Critical:status=Acquire(_critical,MeshRelayCapacityDomainKind::CriticalPrivate,bytes,output);break;
            case MeshRelayServiceClass::Responsive:status=Acquire(_responsive,MeshRelayCapacityDomainKind::ResponsivePrivate,bytes,output);break;
            case MeshRelayServiceClass::Convergent:status=Acquire(_convergent,MeshRelayCapacityDomainKind::ConvergentPrivate,bytes,output);break;
            case MeshRelayServiceClass::BestEffort:status=Acquire(_bestEffort,MeshRelayCapacityDomainKind::BestEffortPrivate,bytes,output);break;
        }
        if(status==MeshRelayResourceStatus::Success||status==MeshRelayResourceStatus::Busy||status==MeshRelayResourceStatus::TooLarge)return status;
        return Acquire(_shared,MeshRelayCapacityDomainKind::SharedOverflow,bytes,output);
    }
    MeshRelayResourceStatus TryAcquireUntrusted(std::size_t bytes,MeshRelayCapacityBundle& output)noexcept{
        if constexpr(std::is_void_v<TUntrusted>)return MeshRelayResourceStatus::InvalidConfiguration;
        else return Acquire(_untrusted,MeshRelayCapacityDomainKind::UntrustedIngress,bytes,output);
    }
};

} // namespace ESPressio::Mesh
