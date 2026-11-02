#pragma once
#include <stratosphere.hpp>
#include "runtime.hpp"
#include "handle_routing.hpp"
#include "client_policy.hpp"

#define RYZ_ACTIVE_METHODS(C,H) AMS_SF_METHOD_INFO(C,H,0,Result,Activate,(u32 handle),(handle))
AMS_SF_DEFINE_INTERFACE(ams::ryzrumble::ipc, IActiveList, RYZ_ACTIVE_METHODS, 0x52595A41)

#define RYZ_HID_METHODS(C,H) \
 AMS_SF_METHOD_INFO(C,H,200,Result,GetInfo,(sf::Out<ryz::rumble::DeviceInfo> out, u32 handle),(out,handle)) \
 AMS_SF_METHOD_INFO(C,H,201,Result,SendValue,(u32 handle, ryz::rumble::Value value, sf::ClientAppletResourceUserId aruid),(handle,value,aruid)) \
 AMS_SF_METHOD_INFO(C,H,202,Result,GetActual,(sf::Out<ryz::rumble::Value> out, u32 handle, sf::ClientAppletResourceUserId aruid),(out,handle,aruid)) \
 AMS_SF_METHOD_INFO(C,H,203,Result,CreateList,(sf::Out<sf::SharedPointer<ams::ryzrumble::ipc::IActiveList>> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,205,Result,IsPermitted,(sf::Out<bool> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,206,Result,SendValues,(u64 aruid, const sf::InPointerArray<u32> &handles, const sf::InPointerArray<ryz::rumble::Value> &values),(aruid,handles,values)) \
 AMS_SF_METHOD_INFO(C,H,211,Result,IsMounted,(sf::Out<bool> out, u32 handle, sf::ClientAppletResourceUserId aruid),(out,handle,aruid))
AMS_SF_DEFINE_MITM_INTERFACE(ams::ryzrumble::ipc, IHid, RYZ_HID_METHODS, 0x52595A48)
#define RYZ_STATUS_METHODS(C,H) \
 AMS_SF_METHOD_INFO(C,H,0,Result,GetStatus,(sf::Out<ryz::rumble::DebugStatus> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,1,Result,GetClients,(sf::Out<ryz::rumble::ClientStatus> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,2,Result,GetSignal,(sf::Out<ryz::rumble::SignalStatus> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,3,Result,GetVolume,(sf::Out<ryz::rumble::VolumeStatus> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,4,Result,GetVolumeUi,(sf::Out<ryz::rumble::VolumeUiStatus> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,5,Result,GetAudioTransport,(sf::Out<ryz::rumble::AudioTransportStatus> out),(out))
AMS_SF_DEFINE_INTERFACE(ams::ryzrumble::ipc, IStatus, RYZ_STATUS_METHODS, 0x52595A53)

namespace ams::ryzrumble {
class StatusService {
public:
    Result GetStatus(sf::Out<ryz::rumble::DebugStatus> out) { out.SetValue(GetDebugStatus()); R_SUCCEED(); }
    Result GetClients(sf::Out<ryz::rumble::ClientStatus> out) { out.SetValue(GetClientStatus()); R_SUCCEED(); }
    Result GetSignal(sf::Out<ryz::rumble::SignalStatus> out) { out.SetValue(GetSignalStatus()); R_SUCCEED(); }
    Result GetVolume(sf::Out<ryz::rumble::VolumeStatus> out) { out.SetValue(GetVolumeStatus()); R_SUCCEED(); }
    Result GetVolumeUi(sf::Out<ryz::rumble::VolumeUiStatus> out) { out.SetValue(GetVolumeUiStatus()); R_SUCCEED(); }
    Result GetAudioTransport(sf::Out<ryz::rumble::AudioTransportStatus> out) { out.SetValue(GetAudioTransportStatus()); R_SUCCEED(); }
};
class ActiveList {
    std::shared_ptr<::Service> forward;
public:
    explicit ActiveList(std::shared_ptr<::Service>&& service) : forward(std::move(service)) {}
    Result Activate(u32 handle) {
        if (IsLite() && ryz::rumble::IsVirtual(handle)) R_SUCCEED();
        const ::Result rc = serviceDispatchIn(forward.get(), 0, handle);
        // Valid HD-rumble handles may have no physical actuator on Lite.
        // Let the virtual device complete activation instead of failing init.
        if (R_FAILED(rc) && IsLite() && ryz::rumble::IsRenderable(handle)) R_SUCCEED();
        R_RETURN(rc);
    }
};

class Hid : public sf::MitmServiceImplBase {
    ryz::rumble::HandleRouting routing;
    ::Result ProbeMounted(bool* mounted, u32 handle, u64 aruid);
    ryz::rumble::Route RouteFor(u32 handle, u64 aruid);
public:
    Hid(std::shared_ptr<::Service>&& service, const sm::MitmProcessInfo& client)
        : MitmServiceImplBase(std::move(service), client) { SessionOpened(client.process_id.value, client.program_id.value); }
    ~Hid() { SessionClosed(m_client_info.process_id.value); }
    static bool ShouldMitm(const sm::MitmProcessInfo& client) {
        // Acquire game sessions even while feedback is muted. Otherwise
        // enabling it later cannot replace the game's already-open HID handle.
        const bool accepted = ryz::rumble::ShouldHookClient(client.program_id.value, client.override_status.IsHbl(), IsLite());
        ObserveClient(client.program_id.value, accepted);
        return accepted;
    }
    Result GetInfo(sf::Out<ryz::rumble::DeviceInfo> out, u32 handle);
    Result SendValue(u32 handle, ryz::rumble::Value value, sf::ClientAppletResourceUserId aruid);
    Result GetActual(sf::Out<ryz::rumble::Value> out, u32 handle, sf::ClientAppletResourceUserId aruid);
    Result CreateList(sf::Out<sf::SharedPointer<ipc::IActiveList>> out);
    Result SendValues(u64 aruid, const sf::InPointerArray<u32>& handles, const sf::InPointerArray<ryz::rumble::Value>& values);
    Result IsMounted(sf::Out<bool> out, u32 handle, sf::ClientAppletResourceUserId aruid);
    Result IsPermitted(sf::Out<bool> out) {
        if (!IsLite()) R_THROW(sm::mitm::ResultShouldForwardToSession());
        out.SetValue(true); R_SUCCEED();
    }
};
static_assert(ipc::IsIHid<Hid>);
static_assert(ipc::IsIActiveList<ActiveList>);
using SingleMeta = sf::impl::CommandMetaInfo<u32, ryz::rumble::Value, sf::ClientAppletResourceUserId>;
static_assert(SingleMeta::InDataSize == 32 && SingleMeta::InDataOffsets[0] == 0 &&
              SingleMeta::InDataOffsets[1] == 4 && SingleMeta::InDataOffsets[2] == 24 && SingleMeta::HasInProcessIdHolder);
using QueryMeta = sf::impl::CommandMetaInfo<u32, sf::ClientAppletResourceUserId>;
static_assert(QueryMeta::InDataSize == 16 && QueryMeta::InDataOffsets[1] == 8 && QueryMeta::HasInProcessIdHolder);
using BatchMeta = sf::impl::CommandMetaInfo<u64, const sf::InPointerArray<u32>&, const sf::InPointerArray<ryz::rumble::Value>&>;
static_assert(BatchMeta::InDataSize == 8 && BatchMeta::NumInHipcPointerBuffers == 2 && !BatchMeta::HasInProcessIdHolder);
void RunServer();
}
