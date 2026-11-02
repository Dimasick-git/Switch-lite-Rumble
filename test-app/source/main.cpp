#include <switch.h>
#include <cstdio>
#include <sys/stat.h>
#include "../../source/debug_protocol.hpp"
#include "vibration_client.hpp"

namespace {
constexpr const char* TestLog = "sdmc:/config/ryz-lite-rumble/test-client.log";
bool report_saved = false;
void trace(const char* stage, Result rc, u32 detail = 0) {
    if (FILE* file = std::fopen(TestLog, "a")) {
        std::fprintf(file, "%s result=%08X detail=%08X\n", stage, rc, detail);
        std::fclose(file);
        report_saved = true;
    }
}
}

int main(int, char**) {
    consoleInit(nullptr);
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/ryz-lite-rumble", 0777);
    // One report per launch; no per-frame SD writes.
    if (FILE* file = std::fopen(TestLog, "w")) std::fclose(file);
    trace("test-client 0.20.0 launched", 0);
    u64 module_pid = 0, host_program = 0;
    Result module_rc = pmdmntInitialize();
    if (R_SUCCEEDED(module_rc)) {
        module_rc = pmdmntGetProcessId(&module_pid, 0x4200000000525652ULL);
        u64 self_pid = 0;
        Result host_rc = svcGetProcessId(&self_pid, CUR_PROCESS_HANDLE);
        if (R_SUCCEEDED(host_rc)) host_rc = pmdmntGetProgramId(&host_program, self_pid);
        if (FILE* file = std::fopen(TestLog, "a")) {
            std::fprintf(file, "host-program result=%08X id=%016llX\n", host_rc,
                static_cast<unsigned long long>(host_program));
            std::fclose(file);
        }
        pmdmntExit();
    }
    trace("module-process", module_rc, static_cast<u32>(module_pid));
    // Query presence first: never wait for an absent diagnostic service.
    Service status_service{};
    ryz::rumble::DebugStatus debug{};
    const SmServiceName status_name = smEncodeName("ryz:rum");
    u8 present = 0;
    Result status_rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, status_name, present);
    if (R_SUCCEEDED(status_rc) && present) {
        status_rc = smGetService(&status_service, "ryz:rum");
        if (R_SUCCEEDED(status_rc)) status_rc = serviceDispatchOut(&status_service, 0, debug);
    } else if (R_SUCCEEDED(status_rc)) {
        status_rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }
    trace("status-service", status_rc, debug.flags);
    // Exercise the same child-controller transport AM uses, including an
    // unhandled command that must forward through a registered MITM session.
    // Read only: do not change the user's hardware or virtual volume here.
    if (hosversionAtLeast(19,0,0)) {
        Service controller{},child{};
        Handle controller_handle=INVALID_HANDLE;
        Result rc=smGetServiceOriginal(&controller_handle,smEncodeName("audctl"));
        if (R_SUCCEEDED(rc)) serviceCreate(&controller,controller_handle);
        trace("audctl-fresh-session",rc);
        if (R_SUCCEEDED(rc)) {
            rc=serviceDispatch(&controller,5000,.out_num_objects=1,.out_objects=&child);
            trace("audctl-child-5000",rc);
            if (R_SUCCEEDED(rc)) {
                u32 target=0;s32 volume=0;
                rc=serviceDispatchOut(&child,32,target);
                trace("audctl-child-forward-32",rc,target);
                const u32 speaker=AudioTarget_Speaker;
                rc=serviceDispatchInOut(&child,0,speaker,volume);
                trace("audctl-child-volume-0",rc,static_cast<u32>(volume));
            }
        }
        serviceClose(&child);serviceClose(&controller);
    }
    VibrationClient vibration;
    const Result fresh_hid_rc = vibration.Open();
    trace("fresh-sm-hid-session", fresh_hid_rc);
    ryz::rumble::ClientStatus clients{};
    ryz::rumble::SignalStatus signal{};
    ryz::rumble::VolumeStatus volume{};
    ryz::rumble::VolumeUiStatus ui{};
    Result ui_rc=status_rc;
    Result volume_rc=status_rc;
    Result clients_rc = serviceIsActive(&status_service) ? serviceDispatchOut(&status_service, 1, clients) : status_rc;
    trace("client-status", clients_rc, static_cast<u32>(clients.accepted));
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    HidVibrationDeviceHandle handles[2]{};
    unsigned device_mode = 0;
    const char* mode_names[]{"Handheld", "Joy-Con pair", "Pro Controller"};
    Result initialized = 0, info_rc = 0, mounted_rc = 0, permitted_rc = 0;
    HidVibrationDeviceInfo info{}; bool mounted = false, permitted = false;
    auto initialize_devices = [&] {
        handles[0] = {}; handles[1] = {}; info = {}; mounted = false; permitted = false;
        initialized = vibration.Initialize(handles, device_mode);
        info_rc = vibration.GetInfo(handles[0], &info);
        mounted_rc = vibration.IsMounted(handles[0], &mounted);
        permitted_rc = vibration.IsPermitted(&permitted);
        trace(mode_names[device_mode], initialized);
        trace("handle-left", 0, handles[0].type_value);
        trace("handle-right", 0, handles[1].type_value);
        trace("device-info", info_rc, info.type);
        trace("mounted", mounted_rc, mounted);
        trace("permitted", permitted_rc, permitted);
    };
    initialize_devices();
    const HidVibrationValue on{0.8f, 160.f, 0.4f, 320.f}, off{};
    HidVibrationValue values[2]{};
    Result last = initialized;
    bool active = false; u64 stop_tick = 0;
    u64 checked = armGetSystemTick();
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 keys = padGetButtonsDown(&pad);
        if (keys & HidNpadButton_Plus) break;
        if (keys & HidNpadButton_Y) {
            values[0] = values[1] = off;
            trace("mode-stop", vibration.SendValues(handles, values, 2));
            active = false;
            device_mode = (device_mode + 1) % 3;
            initialize_devices(); last = initialized;
        }
        if (keys & (HidNpadButton_A | HidNpadButton_X | HidNpadButton_L | HidNpadButton_R)) {
            values[0] = (keys & HidNpadButton_R) ? off : on;
            values[1] = (keys & HidNpadButton_L) ? off : on;
            if (keys & HidNpadButton_X) {
                last = vibration.SendValue(handles[0], &values[0]);
                if (R_SUCCEEDED(last)) last = vibration.SendValue(handles[1], &values[1]);
            } else { last = vibration.SendValues(handles, values, 2); }
            trace((keys & HidNpadButton_X) ? "send-single-201" : "send-batch-206", last, static_cast<u32>(keys));
            active = true;
            stop_tick = armGetSystemTick() + armNsToTicks(500'000'000ULL);
        }
        if ((keys & HidNpadButton_B) || (active && armGetSystemTick() >= stop_tick)) {
            values[0] = values[1] = off;
            last = vibration.SendValues(handles, values, 2);
            trace("stop-batch-206", last);
            active = false;
        }
        if (serviceIsActive(&status_service) && armTicksToNs(armGetSystemTick() - checked) >= 1'000'000'000ULL) {
            status_rc = serviceDispatchOut(&status_service, 0, debug);
            clients_rc = serviceDispatchOut(&status_service, 1, clients);
            const Result signal_rc = serviceDispatchOut(&status_service, 2, signal);
            volume_rc = serviceDispatchOut(&status_service, 3, volume);
            ui_rc = serviceDispatchOut(&status_service, 4, ui);
            if (FILE* file = std::fopen(TestLog, "a")) {
                std::fprintf(file, "snapshot status=%08X flags=%X sessions=%u seen=%llu published=%llu hook=%08X audio=%08X queries=%llu accepted=%llu query=%016llX session=%016llX\n",
                    status_rc, debug.flags, debug.clients,
                    static_cast<unsigned long long>(debug.seen), static_cast<unsigned long long>(debug.rendered),
                    debug.mitm_result, debug.audio_error,
                    static_cast<unsigned long long>(clients.queries), static_cast<unsigned long long>(clients.accepted),
                    static_cast<unsigned long long>(clients.last_query_game), static_cast<unsigned long long>(clients.last_session_game));
                std::fprintf(file, "signal rc=%08X game=%016llX nonzero=%llu zero=%llu input_peak=%u/1000 pcm_peak=%u\n",
                    signal_rc, static_cast<unsigned long long>(signal.program),
                    static_cast<unsigned long long>(signal.nonzero), static_cast<unsigned long long>(signal.zero),
                    signal.input_peak_permille, signal.pcm_peak);
                std::fprintf(file, "volume rc=%08X flags=%X game=%016llX gain=%u target=%u error=%08X keys=%u changes=%llu\n",
                    volume_rc,volume.flags,static_cast<unsigned long long>(volume.program),volume.gain_permille,
                    volume.target_volume,volume.last_error,volume.key_mask,static_cast<unsigned long long>(volume.changes));
                std::fprintf(file,"volume-ui rc=%08X reads=%llu writes=%llu clients=%u am=%u registration=%08X step=%u\n",ui_rc,static_cast<unsigned long long>(ui.reads),static_cast<unsigned long long>(ui.writes),ui.clients,ui.am_clients,ui.registration,ui.step);
                std::fclose(file);
            }
            checked = armGetSystemTick();
        }
        std::printf("\x1b[2J\x1b[HRyz Lite rumble 0.20.0: real HID IPC test\n\n"
            "Module lookup: %08X PID=%llu\n"
            "Host program: %016llX\n"
            "Fresh SM HID: %08X | Y: %s\n"
            "Client queries=%llu accepted=%llu sessions=%u\n"
            "Status: %08X flags=%X (Lite=1 On=2 Audio=4 Thread=8)\n"
            "HID hook: %08X Last audio error: %08X\n"
            "Seen=%llu Published=%llu Handle=%08X\n"
            "Last command game: %016llX\n"
            "Probe count=%u last result=%08X\n"
            "Input peak=%u/1000 PCM peak=%u nonzero=%llu\n"
            "Volume rc=%08X flags=%X Game=%u%% HW=%u\n"
            "Vol error=%08X keys=%u changes=%llu\n"
            "UI rc=%08X AM=%u step=%u reads=%llu writes=%llu\n"
            "SD report: %s\n"
            "Init: %08X\nInfo: %08X type=%u position=%u\n"
            "Mounted: %08X %s\nPermitted: %08X %s\n\n"
            "A: batch command 206, both sides, 500 ms\n"
            "X: single command 201, both sides, 500 ms\n"
            "L/R: left/right only, 500 ms\nB: stop\nPLUS: exit\n\n"
            "Last command result: %08X\nState: %s\n\n"
            "Client report: /config/ryz-lite-rumble/test-client.log\n"
            "Module log: /config/ryz-lite-rumble/runtime.log\n",
            module_rc, static_cast<unsigned long long>(module_pid), static_cast<unsigned long long>(host_program), fresh_hid_rc, mode_names[device_mode],
            static_cast<unsigned long long>(clients.queries), static_cast<unsigned long long>(clients.accepted), debug.clients,
            status_rc, debug.flags, debug.mitm_result, debug.audio_error,
            static_cast<unsigned long long>(debug.seen), static_cast<unsigned long long>(debug.rendered),
            debug.handle, static_cast<unsigned long long>(debug.command_game), debug.reserved[0], debug.reserved[1], signal.input_peak_permille, signal.pcm_peak, static_cast<unsigned long long>(signal.nonzero), volume_rc, volume.flags, volume.gain_permille/10,
            volume.target_volume, volume.last_error, volume.key_mask, static_cast<unsigned long long>(volume.changes), ui_rc,ui.am_clients,ui.step,static_cast<unsigned long long>(ui.reads),static_cast<unsigned long long>(ui.writes), report_saved ? "saved" : "WRITE FAILED",
            initialized, info_rc, static_cast<unsigned>(info.type), static_cast<unsigned>(info.position),
            mounted_rc, mounted ? "yes" : "no", permitted_rc, permitted ? "yes" : "no", last, active ? "on" : "stopped");
        consoleUpdate(nullptr);
    }
    values[0] = values[1] = off;
    trace("exit-stop", vibration.SendValues(handles, values, 2));
    serviceClose(&status_service);
    consoleExit(nullptr);
    return 0;
}
