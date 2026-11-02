#pragma once
#include "runtime.hpp"

#define RYZ_AUDCTL_VOLUME_METHODS(C,H) \
 AMS_SF_METHOD_INFO(C,H,0,Result,GetTargetVolume,(sf::Out<s32> out,u32 target),(out,target)) \
 AMS_SF_METHOD_INFO(C,H,1,Result,SetTargetVolume,(u32 target,s32 volume),(target,volume)) \
 AMS_SF_METHOD_INFO(C,H,4,Result,IsTargetMute,(sf::Out<bool> out,u32 target),(out,target)) \
 AMS_SF_METHOD_INFO(C,H,5,Result,SetTargetMute,(u32 mute,u32 target),(mute,target))
AMS_SF_DEFINE_MITM_INTERFACE(ams::ryzrumble::ipc, IAudctlSession, RYZ_AUDCTL_VOLUME_METHODS, 0x52594144)
#define RYZ_AUDCTL_METHODS(C,H) \
 RYZ_AUDCTL_VOLUME_METHODS(C,H) \
 AMS_SF_METHOD_INFO(C,H,5000,Result,CreateController,(sf::OutMoveHandle out),(out))
AMS_SF_DEFINE_MITM_INTERFACE(ams::ryzrumble::ipc, IAudctl, RYZ_AUDCTL_METHODS, 0x52594143)

// Observe every child request without guessing its ABI or touching its TLS
// message. The stock dispatch table still validates and forwards the request.
namespace ams::sf::cmif {
template<> struct ServiceDispatchTraits<ams::ryzrumble::ipc::IAudctlSession> {
    static constexpr auto BaseTable=ams::ryzrumble::ipc::IAudctlSession::s_CmifServiceDispatchTable<ams::ryzrumble::ipc::IAudctlSession>;
    class Table : public impl::ServiceDispatchTableBase {
    public:
        Result ProcessMessageForMitm(ServiceDispatchContext& ctx,const PointerAndSize& raw) const {
            if(raw.GetSize()>=sizeof(CmifInHeader)) {
                const auto* header=static_cast<const CmifInHeader*>(raw.GetPointer());
                if(header->magic==CMIF_IN_HEADER_MAGIC) {
                    const size_t words=(raw.GetSize()-sizeof(CmifInHeader))/sizeof(u32);
                    const auto* data=reinterpret_cast<const u32*>(header+1);
                    ams::ryzrumble::ObserveAudctlWire(header->command_id,words,words?data[0]:0,words>1?data[1]:0);
                }
            }
            R_RETURN(BaseTable.ProcessMessageForMitm(ctx,raw));
        }
    };
    static constexpr Table DispatchTable{};
    static constexpr ServiceDispatchMeta Meta{&DispatchTable,&impl::ServiceDispatchTableBase::ProcessMessageForMitm<Table>};
};
}

namespace ams::ryzrumble {
Result RegisterAudctlChild(sf::OutMoveHandle out,std::shared_ptr<::Service> child,const sm::MitmProcessInfo& client);
class Audctl : public sf::MitmServiceImplBase {
    bool am_client;
public:
    Audctl(std::shared_ptr<::Service>&& service,const sm::MitmProcessInfo& client)
        : MitmServiceImplBase(std::move(service),client),am_client(client.program_id.value==0x0100000000000023ULL) {
        OpenVolumeUiSession(am_client);
    }
    ~Audctl() { CloseVolumeUiSession(am_client); }
    static bool ShouldMitm(const sm::MitmProcessInfo& client) {
        const u64 id=client.program_id.value;
        return IsLite() && (client.override_status.IsHbl() || id==0x0100000000000023ULL || (id>=0x0100000000001000ULL && id<0x0100000000002000ULL));
    }
    Result GetTargetVolume(sf::Out<s32> out,u32 target) {
        s32 value=0;
        const bool served=target==AudioTarget_Speaker && GetGameUiVolume(value);
        ObserveVolumeUiCall(0,target,value,served);
        if (!served) R_THROW(sm::mitm::ResultShouldForwardToSession());
        out.SetValue(value); R_SUCCEED();
    }
    Result SetTargetVolume(u32 target,s32 value) {
        const bool served=target==AudioTarget_Speaker && SetGameUiVolume(value);
        ObserveVolumeUiCall(1,target,value,served);
        if (!served) R_THROW(sm::mitm::ResultShouldForwardToSession());
        R_SUCCEED();
    }
    Result IsTargetMute(sf::Out<bool> out,u32 target) {
        s32 value=0;
        if (target!=AudioTarget_Speaker || !GetGameUiVolume(value)) R_THROW(sm::mitm::ResultShouldForwardToSession());
        out.SetValue(value==0); R_SUCCEED();
    }
    Result SetTargetMute(u32 mute,u32 target) {
        if (target!=AudioTarget_Speaker || mute>1 || !SetGameUiMute(mute!=0)) R_THROW(sm::mitm::ResultShouldForwardToSession());
        R_SUCCEED();
    }
    Result CreateController(sf::OutMoveHandle out);
};
static_assert(ipc::IsIAudctl<Audctl>);
static_assert(ipc::IsIAudctlSession<Audctl>);
inline Result Audctl::CreateController(sf::OutMoveHandle out) {
    // A domain response needs an object ID, not a moved session handle. Leave
    // that transport native until there is a domain-aware output processor.
    if (serviceIsDomain(m_forward_service.get())) R_THROW(sm::mitm::ResultShouldForwardToSession());
    Log("audctl child: native open begin");
    auto child=std::shared_ptr<::Service>(new ::Service{},[](::Service* service) {serviceClose(service);delete service;});
    const ::Result rc=serviceDispatch(m_forward_service.get(),5000,.out_num_objects=1,.out_objects=child.get());
    ObserveVolumeUiCall(5000,0,static_cast<s32>(rc),R_SUCCEEDED(rc));
    Log("audctl child: native open result",rc);
    R_TRY(rc);
    // sf::Out<SharedPointer<...>> uses RegisterSession, which cannot forward
    // unknown MITM commands and aborts in ForwardRequest. Register explicitly
    // with the native forward service before returning the new client handle.
    R_RETURN(RegisterAudctlChild(out,std::move(child),m_client_info));
}
using AudctlChildMeta=sf::impl::CommandMetaInfo<sf::OutMoveHandle>;
static_assert(AudctlChildMeta::NumOutMoveHandles==1 && AudctlChildMeta::NumOutObjects==0);
}
