#pragma once
#include "runtime.hpp"
#include "volume_notification.hpp"
namespace ams::ryzrumble {
struct NotificationSource {char name[16];};
}
#define RYZ_RECEIVER_METHODS(C,H) \
 AMS_SF_METHOD_INFO(C,H,0,Result,AddSource,(ams::ryzrumble::NotificationSource source),(source)) \
 AMS_SF_METHOD_INFO(C,H,1,Result,RemoveSource,(ams::ryzrumble::NotificationSource source),(source)) \
 AMS_SF_METHOD_INFO(C,H,2,Result,GetEvent,(sf::OutCopyHandle out),(out)) \
 AMS_SF_METHOD_INFO(C,H,3,Result,Receive,(sf::Out<ryz::rumble::OverlayNotification> out),(out)) \
 AMS_SF_METHOD_INFO(C,H,4,Result,ReceiveWithTick,(sf::Out<ryz::rumble::TimedOverlayNotification> out),(out))
AMS_SF_DEFINE_INTERFACE(ams::ryzrumble::ipc, INotificationReceiver, RYZ_RECEIVER_METHODS, 0x52594e52)
#define RYZ_NOTIFICATION_METHODS(C,H) \
 AMS_SF_METHOD_INFO(C,H,0,Result,OpenReceiver,(sf::Out<sf::SharedPointer<ams::ryzrumble::ipc::INotificationReceiver>> out),(out))
AMS_SF_DEFINE_MITM_INTERFACE(ams::ryzrumble::ipc, INotificationService, RYZ_NOTIFICATION_METHODS, 0x52594e53)
namespace ams::ryzrumble {
void ObserveOverlayVolume(bool patched);
class NotificationReceiver {
    std::shared_ptr<::Service> native;
    bool overlay=false;
    static bool IsOverlay(const NotificationSource& source) {
        constexpr char expected[16]="overlay";
        return std::memcmp(source.name,expected,sizeof(expected))==0;
    }
public:
    explicit NotificationReceiver(std::shared_ptr<::Service>&& service):native(std::move(service)){}
    Result AddSource(NotificationSource source) {
        R_TRY(serviceDispatchIn(native.get(),0,source));
        if(IsOverlay(source))overlay=true;
        R_SUCCEED();
    }
    Result RemoveSource(NotificationSource source) {
        R_TRY(serviceDispatchIn(native.get(),1,source));
        if(IsOverlay(source))overlay=false;
        R_SUCCEED();
    }
    Result GetEvent(sf::OutCopyHandle out) {
        Handle handle=INVALID_HANDLE;
        R_TRY(serviceDispatch(native.get(),2,.out_handle_attrs={SfOutHandleAttr_HipcCopy},.out_handles=&handle));
        out.SetValue(handle,true);R_SUCCEED();
    }
    Result Receive(sf::Out<ryz::rumble::OverlayNotification> out) {
        ryz::rumble::OverlayNotification message{};
        R_TRY(serviceDispatchOut(native.get(),3,message));
        s32 step=0;
        if(overlay && message.tag==1004) {
            const bool patched=GetGameUiVolume(step) && ryz::rumble::PatchSpeakerVolume(message,step);
            ObserveOverlayVolume(patched);
        }
        out.SetValue(message);R_SUCCEED();
    }
    Result ReceiveWithTick(sf::Out<ryz::rumble::TimedOverlayNotification> out) {
        ryz::rumble::TimedOverlayNotification message{};
        R_TRY(serviceDispatchOut(native.get(),4,message));
        s32 step=0;
        if(overlay && GetGameUiVolume(step)) {
            const bool patched=ryz::rumble::PatchTimedSpeakerVolume(message,step);
            if(patched)ObserveOverlayVolume(true);
        }
        out.SetValue(message);R_SUCCEED();
    }
};
static_assert(ipc::IsINotificationReceiver<NotificationReceiver>);
class Notifications : public sf::MitmServiceImplBase {
public:
    using MitmServiceImplBase::MitmServiceImplBase;
    static bool ShouldMitm(const sm::MitmProcessInfo& client) {
        const u64 id=client.program_id.value;
        return IsLite() && (client.override_status.IsHbl() || (id>=0x0100000000001000ULL && id<0x0100000000002000ULL));
    }
    Result OpenReceiver(sf::Out<sf::SharedPointer<ipc::INotificationReceiver>> out) {
        auto native=std::shared_ptr<::Service>(new ::Service{},[](::Service* s){serviceClose(s);delete s;});
        R_TRY(serviceDispatch(m_forward_service.get(),0,.out_num_objects=1,.out_objects=native.get()));
        const sf::cmif::DomainObjectId id{serviceGetObjectId(native.get())};
        out.SetValue(sf::CreateSharedObjectEmplaced<ipc::INotificationReceiver,NotificationReceiver>(std::move(native)),id);
        R_SUCCEED();
    }
};
static_assert(ipc::IsINotificationService<Notifications>);
static_assert(sf::impl::CommandMetaInfo<sf::Out<ryz::rumble::OverlayNotification>>::OutDataSize==0x80);
static_assert(sf::impl::CommandMetaInfo<sf::Out<ryz::rumble::TimedOverlayNotification>>::OutDataSize==0x88);
using TimedNotificationMeta=sf::impl::CommandMetaInfo<sf::Out<ryz::rumble::OverlayNotification>,sf::Out<s64>>;
static_assert(TimedNotificationMeta::OutDataOffsets[0]==0 && TimedNotificationMeta::OutDataOffsets[1]==0x80);
}
