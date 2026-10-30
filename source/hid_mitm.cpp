#include "hid_mitm.hpp"
#include "audctl_mitm.hpp"
#include <cstdio>
#include <cstring>

namespace ams::ryzrumble {
// Extra native probes must preserve the incoming IPC message. In particular,
// a later ResultShouldForwardToSession must see the original command.
namespace {
struct SavedMessage {
    alignas(16) u8 bytes[sf::hipc::TlsMessageBufferSize];
    SavedMessage() { std::memcpy(bytes, armGetTls(), sizeof(bytes)); }
    ~SavedMessage() { std::memcpy(armGetTls(), bytes, sizeof(bytes)); }
};
u64 RoutingTime() { return armTicksToNs(armGetSystemTick()) / 1'000'000ULL; }
}
::Result Hid::ProbeMounted(bool* mounted, u32 handle, u64 aruid) {
    SavedMessage saved;
    CmifRequestFormat format{};
    format.object_id = m_forward_service->object_id;
    format.request_id = 211;
    format.data_size = sizeof(ryz::rumble::DeviceQuery);
    format.server_pointer_size = m_forward_service->pointer_buffer_size;
    format.send_pid = true;
    auto request = cmifMakeRequest(armGetTls(), format);
    const ryz::rumble::DeviceQuery input{handle, 0, aruid};
    std::memcpy(request.data, &input, sizeof(input));
    // Atmosphere's MITM forwarding tag retains the game's PID, rather than
    // substituting our sysmodule PID in a PID-bearing HID request.
    const u64 pid = 0xFFFE000000000000ULL | (m_client_info.process_id.value & 0x0000FFFFFFFFFFFFULL);
    std::memcpy(static_cast<u8*>(armGetTls()) + sizeof(HipcHeader) + sizeof(HipcSpecialHeader), &pid, sizeof(pid));
    ::Result rc = svcSendSyncRequest(m_forward_service->session);
    if (R_SUCCEEDED(rc)) {
        void* data = nullptr;
        rc = serviceParseResponse(m_forward_service.get(), sizeof(u8), &data, 0, nullptr, SfOutHandleAttrs{}, nullptr);
        if (R_SUCCEEDED(rc)) *mounted = (*static_cast<const u8*>(data) & 1) != 0;
    }
    ObserveProbe(rc);
    return rc;
}
ryz::rumble::Route Hid::RouteFor(u32 handle, u64 aruid) {
    ryz::rumble::Route route = ryz::rumble::Route::Native;
    const u64 now = RoutingTime();
    if (routing.Lookup(handle, aruid, now, route)) return route;
    bool mounted = true;
    if (R_SUCCEEDED(ProbeMounted(&mounted, handle, aruid))) {
        routing.Record(handle, aruid, now, mounted);
        return mounted ? ryz::rumble::Route::Native : ryz::rumble::Route::Emulated;
    }
    // Failed probes do not justify consuming a native controller's command.
    return ryz::rumble::Route::Native;
}
Result Hid::GetInfo(sf::Out<ryz::rumble::DeviceInfo> out, u32 handle) {
    if (!IsLite() || !ryz::rumble::IsRenderable(handle)) R_THROW(sm::mitm::ResultShouldForwardToSession());
    ryz::rumble::DeviceInfo info{};
    if (!ryz::rumble::IsVirtual(handle)) {
        ::Result rc;
        { SavedMessage saved; rc = serviceDispatchInOut(m_forward_service.get(), 200, handle, info); }
        if (R_SUCCEEDED(rc) && info.type != 0) { out.SetValue(info); R_SUCCEED(); }
    }
    out.SetValue({1, ryz::rumble::Side(handle) + 1});
    R_SUCCEED();
}
Result Hid::SendValue(u32 handle, ryz::rumble::Value value, sf::ClientAppletResourceUserId aruid) {
    ObserveCommand(m_client_info.program_id.value, handle);
    if (!IsLite() || !ryz::rumble::IsRenderable(handle) || !ryz::rumble::Valid(value))
        R_THROW(sm::mitm::ResultShouldForwardToSession());
    const auto route = RouteFor(handle, aruid.GetValue().value);
    if (Enabled() && !Publish(m_client_info.process_id.value, m_client_info.program_id.value, &handle, &value, 1))
        R_THROW(sm::mitm::ResultShouldForwardToSession());
    if (route == ryz::rumble::Route::Native) R_THROW(sm::mitm::ResultShouldForwardToSession());
    R_SUCCEED();
}
Result Hid::GetActual(sf::Out<ryz::rumble::Value> out, u32 handle, sf::ClientAppletResourceUserId aruid) {
    if (!IsLite() || !ryz::rumble::IsRenderable(handle) || RouteFor(handle, aruid.GetValue().value) == ryz::rumble::Route::Native)
        R_THROW(sm::mitm::ResultShouldForwardToSession());
    out.SetValue(Enabled() ? Actual(m_client_info.process_id.value, handle) : ryz::rumble::Value{});
    R_SUCCEED();
}
Result Hid::IsMounted(sf::Out<bool> out, u32 handle, sf::ClientAppletResourceUserId aruid) {
    if (!IsLite() || !ryz::rumble::IsRenderable(handle)) R_THROW(sm::mitm::ResultShouldForwardToSession());
    if (!ryz::rumble::IsVirtual(handle)) {
        bool mounted = false;
        const ::Result rc = ProbeMounted(&mounted, handle, aruid.GetValue().value);
        if (R_FAILED(rc)) R_RETURN(rc);
        routing.Record(handle, aruid.GetValue().value, RoutingTime(), mounted);
    }
    // A real motor or our virtual Lite actuator is available, even while muted.
    out.SetValue(true);
    R_SUCCEED();
}
Result Hid::CreateList(sf::Out<sf::SharedPointer<ipc::IActiveList>> out) {
    auto service = std::shared_ptr<::Service>(new ::Service{}, [](::Service* s) { serviceClose(s); delete s; });
    R_TRY(serviceDispatch(m_forward_service.get(), 203, .out_num_objects = 1, .out_objects = service.get()));
    out.SetValue(sf::CreateSharedObjectEmplaced<ipc::IActiveList, ActiveList>(std::move(service)));
    R_SUCCEED();
}
Result Hid::SendValues(u64 aruid, const sf::InPointerArray<u32>& handles, const sf::InPointerArray<ryz::rumble::Value>& values) {
    const size_t count = handles.GetSize();
    if (count) ObserveCommand(m_client_info.program_id.value, handles[0]);
    if (!IsLite() || count == 0 || count > 16 || count != values.GetSize())
        R_THROW(sm::mitm::ResultShouldForwardToSession());
    bool renderable_found = false;
    for (size_t i = 0; i < count; ++i) if (ryz::rumble::IsRenderable(handles[i])) {
        if (!ryz::rumble::Valid(values[i])) R_THROW(sm::mitm::ResultShouldForwardToSession());
        renderable_found = true;
    }
    if (!renderable_found) R_THROW(sm::mitm::ResultShouldForwardToSession());
    bool emulated[16]{}; size_t emulated_count = 0;
    for (size_t i = 0; i < count; ++i) {
        emulated[i] = RouteFor(handles[i], aruid) == ryz::rumble::Route::Emulated;
        emulated_count += emulated[i] ? 1 : 0;
    }
    if (Enabled() && !Publish(m_client_info.process_id.value, m_client_info.program_id.value, handles.GetPointer(), values.GetPointer(), count))
        R_THROW(sm::mitm::ResultShouldForwardToSession());
    if (!emulated_count) R_THROW(sm::mitm::ResultShouldForwardToSession());
    // Filter only emulated devices; preserve all mounted/native handles and ARUID.
    u32 native_handles[16]; ryz::rumble::Value native_values[16]; size_t native_count = 0;
    for (size_t i = 0; i < count; ++i) if (!emulated[i]) {
        native_handles[native_count] = handles[i]; native_values[native_count++] = values[i];
    }
    if (native_count) {
        R_RETURN(serviceDispatchIn(m_forward_service.get(), 206, aruid,
            .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_In, SfBufferAttr_HipcPointer | SfBufferAttr_In},
            .buffers = {{native_handles, native_count * sizeof(u32)}, {native_values, native_count * sizeof(ryz::rumble::Value)}}));
    }
    R_SUCCEED();
}

namespace {
struct Options {
    static constexpr size_t PointerBufferSize = 0x1000;
    static constexpr size_t MaxDomains = 4;
    static constexpr size_t MaxDomainObjects = 96;
    static constexpr bool CanDeferInvokeRequest = false;
    static constexpr bool CanManageMitmServers = true;
};
class Manager final : public sf::hipc::ServerManager<3, Options, 24> {
    Result OnNeedsToAccept(int port, Server* server) override {
        if (port == 1) R_RETURN(AcceptImpl(server,
            sf::CreateSharedObjectEmplaced<ipc::IStatus, StatusService>()));
        std::shared_ptr<::Service> forward;
        sm::MitmProcessInfo client;
        server->AcknowledgeMitmSession(&forward, &client);
        char text[112];
        std::snprintf(text, sizeof(text), "%s session game=%016llX pid=%llu", port==2?"audctl":"HID",
            static_cast<unsigned long long>(client.program_id.value),
            static_cast<unsigned long long>(client.process_id.value));
        Log(text);
        if(port==2) R_RETURN(AcceptMitmImpl(server,
            sf::CreateSharedObjectEmplaced<ipc::IAudctl, Audctl>(decltype(forward)(forward),client),forward));
        R_RETURN(AcceptMitmImpl(server,
            sf::CreateSharedObjectEmplaced<ipc::IHid, Hid>(decltype(forward)(forward), client), forward));
    }
};
Manager manager;
}
Result RegisterAudctlChild(sf::OutMoveHandle out,std::shared_ptr<::Service> child,const sm::MitmProcessInfo& client) {
    R_UNLESS(child && serviceIsActive(child.get()) && !serviceIsDomain(child.get()) &&
        child->pointer_buffer_size<=Options::PointerBufferSize,sf::ResultNotSupported());
    os::NativeHandle server,session;
    R_TRY(sf::hipc::CreateSession(&server,&session));
    auto close_server=SCOPE_GUARD {os::CloseNativeHandle(server);};
    auto close_client=SCOPE_GUARD {os::CloseNativeHandle(session);};
    const Result rc=manager.RegisterMitmSession(server,
        sf::cmif::ServiceObjectHolder(sf::CreateSharedObjectEmplaced<ipc::IAudctlSession,Audctl>(decltype(child)(child),client)),
        decltype(child)(child));
    Log("audctl child: MITM session registration",rc.GetValue());
    R_TRY(rc);
    close_server.Cancel();
    out.SetValue(session,false);
    close_client.Cancel();
    R_SUCCEED();
}
void RunServer() {
    const Result status_rc = manager.RegisterServer(1, sm::ServiceName::Encode("ryz:rum"), 2);
    Log("status service registration", status_rc.GetValue());
    const Result ui_rc=manager.RegisterMitmServer<Audctl>(2,sm::ServiceName::Encode("audctl"));
    SetVolumeUiResult(ui_rc.GetValue());Log("audctl MITM registration",ui_rc.GetValue());
    if(R_FAILED(ui_rc)) AMS_UNUSED(sm::mitm::ClearFutureMitm(sm::ServiceName::Encode("audctl")));
    const Result rc = manager.RegisterMitmServer<Hid>(0, sm::ServiceName::Encode("hid"));
    SetMitmResult(rc.GetValue());
    Log("HID MITM registration", rc.GetValue());
    if (R_FAILED(rc) && R_FAILED(status_rc) && R_FAILED(ui_rc)) return;
    // Install audctl before any worker requests it: never wait on our own
    // future-MITM declaration through a shared SM session.
    StartVolumeButtons();
    manager.LoopProcess();
}
}
