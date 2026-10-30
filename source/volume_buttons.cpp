#include "runtime.hpp"
#include "volume_buttons.hpp"
#include "volume_rocker.hpp"
#include "volume_checkpoint.hpp"
#include "volume_ui_scale.hpp"
#include <atomic>
#include <cmath>
#include <cstring>

namespace ams::ryzrumble {
namespace {
constexpr const char* Checkpoint = "sdmc:/config/ryz-lite-rumble/volume-session.ini";
constexpr const char* LevelFile = "sdmc:/config/ryz-lite-rumble/volume-level.ini";
std::atomic<bool> requested{true};
std::atomic<u32> flags{0}, level{1000}, target_level{0}, tune_api{0}, error{0}, keys{0};
std::atomic<u64> active_pid{0}, active_program{0}, changes{0};
constexpr u32 NoUiRequest=UINT32_MAX;
std::atomic<u32> ui_request{NoUiRequest},ui_clients{0},am_clients{0},ui_registration{UINT32_MAX},last_nonzero{1000};
std::atomic<u64> ui_reads{0},ui_writes{0},last_ui_write{0};
std::atomic<u64> ui_calls{0},ui_served{0},ui_children{0};
std::atomic<u32> raw_button_keys{0};
std::atomic<u64> last_button_held{0},ui_button_echoes{0};
std::atomic<u32> ui_command{0},ui_target{0};
std::atomic<s32> ui_value{0};
os::ThreadType worker;
alignas(os::MemoryPageSize) u8 stack[0x8000];
u64 Now() { return armTicksToNs(armGetSystemTick()) / 1'000'000ULL; }
bool Read(const char* path, char* text, size_t capacity) {
    fs::FileHandle file;
    if (R_FAILED(fs::OpenFile(&file,path,fs::OpenMode_Read))) return false;
    size_t size=0; const Result rc=fs::ReadFile(&size,file,0,text,capacity-1);
    fs::CloseFile(file); text[size]=0; return R_SUCCEEDED(rc);
}
bool Write(const char* path, const char* text) {
    const size_t size=std::strlen(text);
    AMS_UNUSED(fs::CreateFile(path,0));
    fs::FileHandle file;
    if (R_FAILED(fs::OpenFile(&file,path,fs::OpenMode_Write))) return false;
    Result rc=fs::SetFileSize(file,static_cast<s64>(size));
    if (R_SUCCEEDED(rc)) rc=fs::WriteFile(file,0,text,size,fs::WriteOption::Flush);
    fs::CloseFile(file); return R_SUCCEEDED(rc);
}
void Fail(u32 rc, const char* stage) { error.store(rc); Log(stage,rc); }
using SystemLevel=ryz::rumble::VolumeCheckpoint;
bool ReadCheckpoint(SystemLevel& value,s32 maximum) {
    char text[128];
    return Read(Checkpoint,text,sizeof(text)) && ryz::rumble::DecodeVolumeCheckpoint(text,maximum,value);
}
bool SaveCheckpoint(const SystemLevel& value) {
    char text[128];
    return ryz::rumble::EncodeVolumeCheckpoint(text,sizeof(text),value) && Write(Checkpoint,text);
}
::Result Restore(const SystemLevel& value) {
    // Restore the pre-mode speaker state, including mute. Do not change the
    // headphone target or force the active audio device to speakers.
    ::Result rc=audctlSetTargetVolume(AudioTarget_Speaker,value.speaker);
    const ::Result master_rc=audctlSetSystemOutputMasterVolume(value.master);
    const ::Result mute_rc=audctlSetTargetMute(AudioTarget_Speaker,value.mute);
    if (R_SUCCEEDED(rc)) rc=master_rc;
    if (R_SUCCEEDED(rc)) rc=mute_rc;
    if (R_SUCCEEDED(rc)) AMS_UNUSED(fs::DeleteFile(Checkpoint));
    return rc;
}
::Result App(u64& pid,u64& program) {
    pid=program=0;
    ::Result rc=pmdmntInitialize();
    if (R_FAILED(rc)) return rc;
    rc=pmdmntGetApplicationProcessId(&pid);
    if (R_SUCCEEDED(rc)) rc=pmdmntGetProgramId(&program,pid);
    pmdmntExit(); return rc;
}
struct Tune {
    Service service{};
    ~Tune() { Close(); }
    void Close() { serviceClose(&service); tune_api.store(0); }
    ::Result Open() {
        if (serviceIsActive(&service)) return 0;
        Handle handle=INVALID_HANDLE;
        ::Result rc=svcConnectToNamedPort(&handle,"tune");
        if (R_FAILED(rc)) return rc;
        serviceCreate(&service,handle);
        u32 api=0; rc=serviceDispatchOut(&service,5000,api);
        if (R_FAILED(rc) || api<1) { Close(); return R_FAILED(rc)?rc:MAKERESULT(Module_Libnx,LibnxError_BadInput); }
        tune_api.store(api); return 0;
    }
    ::Result Get(float& gain) {
        const ::Result rc=serviceDispatchOut(&service,12,gain);
        if (R_FAILED(rc)) return rc;
        return std::isfinite(gain) && gain>=0 && gain<=1.5f ? 0 : MAKERESULT(Module_Libnx,LibnxError_BadInput);
    }
    ::Result Set(float gain) { return serviceDispatchIn(&service,13,gain); }
};
::Result OpenPad(::GpioPadSession& pad,u32 device,GpioPadName legacy) {
    // Read-only sessions avoid taking ownership of the native button IRQ.
    ::Result rc=gpioOpenSession2(&pad,device,1);
    if (R_FAILED(rc)) rc=gpioOpenSession(&pad,legacy);
    return rc;
}
::Result ReadKeys(::GpioPadSession& up,::GpioPadSession& down,u32& keys) {
    ::GpioValue up_value=GpioValue_High,down_value=GpioValue_High;
    ::Result rc=gpioPadGetValue(&up,&up_value);
    if (R_SUCCEEDED(rc)) rc=gpioPadGetValue(&down,&down_value);
    keys=(up_value==GpioValue_Low?1u:0u)|(down_value==GpioValue_Low?2u:0u);
    return rc;
}
void Main(void*) {
    flags.store(1);
    unsigned wanted=1000; bool have_level=false;
    { char text[64]; u32 stored=0; if (Read(LevelFile,text,sizeof(text)) && ryz::rumble::ParseCheckpointUInt(text,"gain",10,stored) && stored<=1000) { wanted=stored; have_level=true; } else wanted=1000; }
    level.store(wanted);
    for (;;) {
        ::Result rc=audctlInitialize();
        if (R_FAILED(rc)) { Fail(rc,"volume buttons: audctl unavailable"); os::SleepThread(TimeSpan::FromSeconds(2)); continue; }
        auto close_control=SCOPE_GUARD { audctlExit(); };
        s32 maximum=0;
        rc=audctlGetTargetVolumeMax(&maximum);
        if (R_FAILED(rc) || maximum<=0) { Fail(R_FAILED(rc)?rc:MAKERESULT(Module_Libnx,LibnxError_BadInput),"volume buttons: target range"); os::SleepThread(TimeSpan::FromSeconds(2)); continue; }
        SystemLevel recovered;
        if (ReadCheckpoint(recovered,maximum)) {
            rc=Restore(recovered);
            if (R_FAILED(rc)) { Fail(rc,"volume buttons: recovery failed"); os::SleepThread(TimeSpan::FromSeconds(2)); continue; }
            Log("volume buttons: restored previous session");
        }
        rc=gpioInitialize();
        if (R_FAILED(rc)) { Fail(rc,"volume buttons: gpio unavailable"); os::SleepThread(TimeSpan::FromSeconds(2)); continue; }
        auto close_gpio=SCOPE_GUARD { gpioExit(); };
        ::GpioPadSession up{},down{};
        auto close_pads=SCOPE_GUARD { gpioPadClose(&up); gpioPadClose(&down); };
        rc=OpenPad(up,0x35000002,GpioPadName_ButtonVolUp);
        if (R_SUCCEEDED(rc)) rc=OpenPad(down,0x35000003,GpioPadName_ButtonVolDown);
        if (R_FAILED(rc)) { Fail(rc,"volume buttons: open pads"); os::SleepThread(TimeSpan::FromSeconds(2)); continue; }
        flags.store(3); Log("volume buttons: GPIO ready");
        Tune tune; SystemLevel saved; float game_gain=1;
        // While the same game keeps running (headphones, mode switched off,
        // error) give it back its own Tune gain. Otherwise it stays attenuated
        // by the speaker level folded into Tune on activation.
        const auto leave=[&](bool same_game) {
            const ::Result restore_rc=Restore(saved);
            if (same_game && serviceIsActive(&tune.service)) AMS_UNUSED(tune.Set(game_gain));
            return restore_rc;
        };
        bool active=false,dirty=false;
        u64 pid=0,program=0,candidate=0,stable_since=0,last_app=0,last_context=0,last_external=0,last_change=0,armed_at=0;
        u32 raw_keys=0,stable_keys=0; u64 raw_since=0;
        AudioTarget output=AudioTarget_Invalid;
        u64 traced_pid=0,last_ui_log=0,logged_calls=0;
        ryz::rumble::VolumeRocker rocker;
        for (;;) {
            const u64 now=Now();
            if (now-last_ui_log>=1000 && ui_calls.load()!=logged_calls) {
                logged_calls=ui_calls.load();last_ui_log=now;
                char report[240];
                util::SNPrintf(report,sizeof(report),"volume UI: calls=%llu served=%llu child=%llu clients=%u am=%u reads=%llu writes=%llu step=%u last=%u target=%u value=%d echoes=%llu",
                    static_cast<unsigned long long>(logged_calls),static_cast<unsigned long long>(ui_served.load()),
                    static_cast<unsigned long long>(ui_children.load()),ui_clients.load(),am_clients.load(),
                    static_cast<unsigned long long>(ui_reads.load()),static_cast<unsigned long long>(ui_writes.load()),
                    ryz::rumble::VolumeGainStep(level.load(),target_level.load()),ui_command.load(),ui_target.load(),ui_value.load(),
                    static_cast<unsigned long long>(ui_button_echoes.load()));
                Log(report);
            }
            u32 reading=0;
            rc=ReadKeys(up,down,reading);
            if (R_FAILED(rc)) { Fail(rc,"volume buttons: read GPIO"); if (active) leave(true); break; }
            raw_button_keys.store(reading);
            if (reading) last_button_held.store(now);
            if (reading!=raw_keys) { raw_keys=reading; raw_since=now; }
            if (now-raw_since>=15) stable_keys=raw_keys;
            keys.store(stable_keys);
            if (now-last_app>=200) {
                const ::Result app_rc=App(pid,program);
                if (R_FAILED(app_rc)) pid=program=0;
                if (candidate!=pid) { candidate=pid; stable_since=now; }
                last_app=now;
            }
            if (now-last_context>=50) {
                if (R_FAILED(audctlGetActiveOutputTarget(&output))) output=AudioTarget_Invalid;
                last_context=now;
            }
            const bool usable=requested.load() && Enabled() && pid!=0 && output==AudioTarget_Speaker;
            if (active && (!usable || active_pid.load()!=pid)) {
                ui_request.store(NoUiRequest);
                rc=leave(active_pid.load()==pid); active=false; active_pid.store(0); active_program.store(0);
                if (R_FAILED(rc)) { Fail(rc,"volume buttons: restore failed"); break; }
                Log("volume buttons: ordinary volume restored");
            }
            if (!active && !usable) tune.Close();
            if (!active && usable && now-stable_since>=500) {
                const bool trace_entry=traced_pid!=pid;
                traced_pid=pid;
                if (trace_entry) Log("volume arm: Tune open begin");
                rc=tune.Open();
                if (trace_entry) Log("volume arm: Tune open result",rc);
                float initial=1;
                if (R_SUCCEEDED(rc)) rc=tune.Get(initial);
                if (trace_entry) Log("volume arm: Tune gain read",rc);
                if (R_FAILED(rc)) { error.store(rc); tune.Close(); os::SleepThread(TimeSpan::FromMilliSeconds(200)); continue; }
                rc=audctlGetTargetVolume(&saved.speaker,AudioTarget_Speaker);
                if (R_SUCCEEDED(rc)) rc=audctlGetSystemOutputMasterVolume(&saved.master);
                if (R_SUCCEEDED(rc)) rc=audctlIsTargetMute(&saved.mute,AudioTarget_Speaker);
                if (R_FAILED(rc) || !std::isfinite(saved.master) || saved.master<0 || saved.master>3) {
                    error.store(R_FAILED(rc)?rc:MAKERESULT(Module_Libnx,LibnxError_BadInput)); os::SleepThread(TimeSpan::FromMilliSeconds(200)); continue;
                }
                if (!have_level) wanted=static_cast<unsigned>(std::lround(std::clamp(initial*saved.master*saved.speaker/maximum,0.f,1.f)*1000));
                if (saved.mute && !have_level) wanted=0;
                if (trace_entry) Log("volume arm: saving allocation-free checkpoint");
                if (!SaveCheckpoint(saved)) { Fail(MAKERESULT(Module_Libnx,LibnxError_IoError),"volume buttons: cannot save recovery state"); os::SleepThread(TimeSpan::FromSeconds(1)); continue; }
                // Mute the game before raising the shared output. Give Tune's
                // process-volume worker time to apply its existing setter.
                if (trace_entry) Log("volume arm: checkpoint saved, mute game begin");
                rc=tune.Set(0);
                if (trace_entry) Log("volume arm: mute request result",rc);
                if (R_SUCCEEDED(rc)) os::SleepThread(TimeSpan::FromMilliSeconds(40));
                if (trace_entry) Log("volume arm: pin output begin");
                if (R_SUCCEEDED(rc)) rc=audctlSetSystemOutputMasterVolume(1.f);
                if (R_SUCCEEDED(rc)) rc=audctlSetTargetVolume(AudioTarget_Speaker,maximum);
                if (R_SUCCEEDED(rc)) rc=audctlSetTargetMute(AudioTarget_Speaker,false);
                if (R_SUCCEEDED(rc)) rc=tune.Set(wanted/1000.f);
                if (R_FAILED(rc)) { Restore(saved); tune.Set(initial); Fail(rc,"volume buttons: activation failed"); tune.Close(); os::SleepThread(TimeSpan::FromSeconds(1)); continue; }
                active=true; have_level=true; dirty=true; last_change=now; armed_at=now; game_gain=initial;
                active_pid.store(pid); active_program.store(program); level.store(wanted);
                if (wanted) last_nonzero.store(wanted);
                rocker.Reset((stable_keys&1)!=0,(stable_keys&2)!=0); error.store(0);
                Log("volume buttons: game via Tune, speaker fixed at maximum");
            }
            if (active) {
                // Native audctl writes are absolute user-volume requests and
                // must reach Tune before slower file/config maintenance.
                const u32 ui=ui_request.exchange(NoUiRequest);
                u32 held=reading;
                if (ui!=NoUiRequest && !held && R_SUCCEEDED(ReadKeys(up,down,held)) && held) {
                    // The echo can land after the GPIO sample above but before
                    // this exchange. A press always precedes its echo, so a
                    // fresh sample tells them apart; without it the echo was
                    // applied and the rocker then added a second step.
                    raw_button_keys.store(held); last_button_held.store(now);
                }
                const bool physical=ryz::rumble::PhysicalVolumeOwns(held,last_button_held.load(),now);
                if (ui!=NoUiRequest && physical) {
                    // Also catches an echo queued just before this GPIO poll.
                    level.store(wanted);ui_button_echoes.fetch_add(1);
                }
                if (ui!=NoUiRequest && !physical) {
                    rc=tune.Set(ui/1000.f);
                    if (R_FAILED(rc)) { leave(true); Fail(rc,"volume UI: Tune write failed"); tune.Close(); break; }
                    wanted=ui; changes.fetch_add(1);dirty=true;last_change=now;
                }
                // An AM session does not prove that it sends volume commands.
                // Always run the physical rocker; suppress its UI echoes above.
                const int direction=rocker.Tick((stable_keys&1)!=0,(stable_keys&2)!=0,now);
                if (direction) {
                    const unsigned next=ryz::rumble::VolumeRocker::Apply(wanted,direction,static_cast<unsigned>(maximum));
                    rc=tune.Set(next/1000.f);
                    if (R_FAILED(rc)) { leave(true); Fail(rc,"volume buttons: Tune write failed"); tune.Close(); break; }
                    wanted=next; level.store(wanted); changes.fetch_add(1); dirty=true; last_change=now;
                    if (wanted) last_nonzero.store(wanted);
                }
                // The native OS still receives these buttons. Pin only the
                // speaker target; never force headphones/BT to full volume.
                s32 actual=maximum;
                rc=audctlGetTargetVolume(&actual,AudioTarget_Speaker);
                if (R_SUCCEEDED(rc) && actual!=maximum) rc=audctlSetTargetVolume(AudioTarget_Speaker,maximum);
                target_level.store(R_SUCCEEDED(rc)?static_cast<u32>(maximum):0);
                if (now-last_external>=200) {
                    float master=1; bool mute=false;
                    if (R_SUCCEEDED(rc)) rc=audctlGetSystemOutputMasterVolume(&master);
                    if (R_SUCCEEDED(rc) && std::fabs(master-1.f)>.001f) rc=audctlSetSystemOutputMasterVolume(1.f);
                    if (R_SUCCEEDED(rc)) rc=audctlIsTargetMute(&mute,AudioTarget_Speaker);
                    if (R_SUCCEEDED(rc) && mute) rc=audctlSetTargetMute(AudioTarget_Speaker,false);
                    float external=wanted/1000.f;
                    if (R_SUCCEEDED(rc)) rc=tune.Get(external);
                    if (R_SUCCEEDED(rc) && ui_request.load()==NoUiRequest && std::fabs(external-wanted/1000.f)>.005f) {
                        if (now-armed_at<2000) {
                            // Tune resets the title gain when it notices a new
                            // title. Landing after activation, that reset is not
                            // a user change: keep the game level, not full volume.
                            rc=tune.Set(wanted/1000.f);
                        } else {
                            wanted=static_cast<unsigned>(std::lround(std::clamp(external,0.f,1.f)*1000));
                            level.store(wanted); dirty=true; last_change=now;
                            if (wanted) last_nonzero.store(wanted);
                        }
                    }
                    last_external=now;
                }
                if (R_FAILED(rc)) { leave(true); Fail(rc,"volume buttons: control error"); tune.Close(); break; }
            }
            if (dirty && stable_keys==0 && now-last_change>=500) {
                char text[64]; util::SNPrintf(text,sizeof(text),"gain=%u\n",wanted);
                if (Write(LevelFile,text)) {
                    dirty=false;
                    char report[112]; util::SNPrintf(report,sizeof(report),"volume buttons: game=%016llX gain=%u/1000 target=%d",
                        static_cast<unsigned long long>(active_program.load()),wanted,maximum);
                    Log(report);
                }
            }
            flags.store(1u|2u|(serviceIsActive(&tune.service)?4u:0u)|(active?8u:0u)|
                (output==AudioTarget_Speaker?16u:0u)|(requested.load()?32u:0u));
            os::SleepThread(TimeSpan::FromMilliSeconds(active?2:50));
        }
        active_pid.store(0); active_program.store(0); flags.store(1);
        os::SleepThread(TimeSpan::FromSeconds(1));
    }
}
}
void SetVolumeButtonsEnabled(bool value) { requested.store(value); }
void OpenVolumeUiSession(bool am) { ui_clients.fetch_add(1);if(am)am_clients.fetch_add(1); }
void CloseVolumeUiSession(bool am) { ui_clients.fetch_sub(1);if(am)am_clients.fetch_sub(1); }
void SetVolumeUiResult(unsigned int value) {ui_registration.store(value);}
void ObserveVolumeUiCall(unsigned int command,unsigned int target,signed int value,bool served) {
    ui_command.store(command);ui_target.store(target);ui_value.store(value);
    if(served)ui_served.fetch_add(1);
    if(command==5000 && served)ui_children.fetch_add(1);
    ui_calls.fetch_add(1);
}
bool GetGameUiVolume(signed int& value) {
    const u32 maximum=target_level.load();
    if (!(flags.load()&8) || !requested.load() || !Enabled() || !active_pid.load() || !maximum || maximum>100) return false;
    value=static_cast<s32>(ryz::rumble::VolumeGainStep(level.load(),maximum));ui_reads.fetch_add(1);return true;
}
bool SetGameUiVolume(signed int value) {
    s32 current=0;if(!GetGameUiVolume(current))return false;
    const u32 maximum=target_level.load();if(value<0 || static_cast<u32>(value)>maximum)return false;
    if(ryz::rumble::PhysicalVolumeOwns(raw_button_keys.load(),last_button_held.load(),Now())) {
        ui_writes.fetch_add(1);ui_button_echoes.fetch_add(1);return true;
    }
    const u32 gain=ryz::rumble::VolumeStepGain(static_cast<u32>(value),maximum);
    if(gain)last_nonzero.store(gain);
    level.store(gain);ui_request.store(gain);ui_writes.fetch_add(1);last_ui_write.store(Now());return true;
}
bool SetGameUiMute(bool mute) {
    s32 current=0;if(!GetGameUiVolume(current))return false;
    if(ryz::rumble::PhysicalVolumeOwns(raw_button_keys.load(),last_button_held.load(),Now())) {
        ui_writes.fetch_add(1);ui_button_echoes.fetch_add(1);return true;
    }
    const u32 before=level.load();
    if(mute && before)last_nonzero.store(before);
    const u32 gain=mute?0:before?before:last_nonzero.load();
    level.store(gain);ui_request.store(gain);ui_writes.fetch_add(1);last_ui_write.store(Now());return true;
}
ryz::rumble::VolumeUiStatus GetVolumeUiStatus() {
    return {ui_reads.load(),ui_writes.load(),ui_clients.load(),am_clients.load(),ui_registration.load(),
        ryz::rumble::VolumeGainStep(level.load(),target_level.load())};
}
ryz::rumble::VolumeStatus GetVolumeStatus() {
    return {active_pid.load(),active_program.load(),flags.load(),level.load(),target_level.load(),tune_api.load(),error.load(),keys.load(),changes.load()};
}
void StartVolumeButtons() {
    if (!IsLite()) return;
    const Result rc=os::CreateThread(&worker,Main,nullptr,stack,sizeof(stack),os::DefaultThreadPriority,3);
    Log("volume buttons: create worker",rc.GetValue());
    if (R_SUCCEEDED(rc)) { os::SetThreadNamePointer(&worker,"RyzVolumeButtons"); os::StartThread(&worker); }
    else error.store(rc.GetValue());
}
}
