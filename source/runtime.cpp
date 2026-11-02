#include "runtime.hpp"
#include "config_value.hpp"
#include "speaker_response.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace ams::ryzrumble {
namespace {
constexpr const char* ConfigPath = "sdmc:/config/ryz-lite-rumble/config.ini";
constexpr const char* LogPath = "sdmc:/config/ryz-lite-rumble/runtime.log";
std::mutex state_mutex, log_mutex;
ryz::rumble::State state;
std::atomic<bool> lite{false}, enabled{true};
std::atomic<unsigned> gain_percent{200}, watchdog_ms{1000};
std::atomic<bool> boost_weak_signals{true};
std::atomic<u64> commands{0}, last_program{0};
std::atomic<u64> seen_commands{0}, seen_program{0};
std::atomic<u32> seen_handle{0};
std::atomic<u32> probe_count{0}, last_probe_result{0};
std::atomic<u64> session_program{0};
std::atomic<u64> client_queries{0}, client_accepted{0}, queried_program{0};
std::atomic<u64> signal_program{0}, nonzero_updates{0}, zero_updates{0};
std::atomic<u64> handheld_nonzero{0}, pad_nonzero{0};
std::atomic<u32> last_nonzero_handle{0};
std::atomic<u32> input_peak{0}, pcm_peak{0};
std::atomic<u32> last_audio_error{0}, mitm_result{0xFFFFFFFFu};
std::atomic<bool> audio_started{false}, thread_started{false};
std::atomic<u64> submitted_frames{0}, played_frames{0}, slow_refills{0};
std::atomic<u32> stream_gain{UINT32_MAX}, process_gain{UINT32_MAX}, first_process_gain{UINT32_MAX};
std::atomic<u32> process_result{UINT32_MAX}, max_refill_ms{0}, wait_timeouts{0};
std::atomic<unsigned> active_clients{0};
struct Client { u64 process = 0; unsigned references = 0; } clients[16];
os::ThreadType audio_thread;
alignas(os::MemoryPageSize) u8 audio_stack[0x8000];
constexpr unsigned BufferCount = 3;
alignas(0x1000) s16 samples[BufferCount][2048];
AudioOutBuffer buffers[BufferCount]{};
constexpr unsigned Frames = 480;

u64 Now() { return armTicksToNs(armGetSystemTick()) / 1'000'000ULL; }
void RaisePeak(std::atomic<u32>& target, u32 value) {
    u32 current = target.load();
    while (current < value && !target.compare_exchange_weak(current, value)) {}
}
void MeasurePcm(const s16* pcm, size_t samples_count) {
    u32 peak = 0;
    for (size_t i=0;i<samples_count;++i) peak = std::max(peak, static_cast<u32>(std::abs(static_cast<int>(pcm[i]))));
    RaisePeak(pcm_peak, peak);
}
bool ReadText(const char* path, char* text, size_t capacity) {
    fs::FileHandle file;
    if (R_FAILED(fs::OpenFile(&file, path, fs::OpenMode_Read))) return false;
    size_t size = 0;
    const Result rc = fs::ReadFile(&size, file, 0, text, capacity - 1);
    fs::CloseFile(file);
    if (R_FAILED(rc)) return false;
    text[size] = 0;
    return true;
}
void ReloadConfig() {
    char text[4096];
    bool on = true;
    bool volume_buttons = true;
    if (ReadText(ConfigPath, text, sizeof(text))) {
        on = ryz::rumble::ConfigNumber(text, "rumble", "enabled", 1) != 0;
        volume_buttons = ryz::rumble::ConfigNumber(text, "rumble", "game_volume_buttons", 1) != 0;
        gain_percent.store(std::min(ryz::rumble::ConfigNumber(text, "rumble", "gain_percent", 200), 400u));
        boost_weak_signals.store(ryz::rumble::ConfigNumber(text, "rumble", "boost_weak_signals", 1) != 0);
        const unsigned timeout = ryz::rumble::ConfigNumber(text, "rumble", "watchdog_ms", 1000);
        watchdog_ms.store(timeout ? std::clamp(timeout, 100u, 10000u) : 0);
    }
    if (ReadText("sdmc:/config/ryazhahand/config.ini", text, sizeof(text)))
        on = on && ryz::rumble::ConfigNumber(text, "ryazhahand", "haptic_feedback", 1) != 0;
    const bool before=enabled.exchange(on);
    if(before!=on) Log(on?"haptics enabled; game volume mode allowed":"haptics disabled; restoring ordinary volume");
    SetVolumeButtonsEnabled(volume_buttons);
    if (!on) { std::scoped_lock lock(state_mutex); state.Clear(); }
}
ryz::rumble::Levels Snapshot(bool render = false) {
    if (!Enabled()) return {};
    std::scoped_lock lock(state_mutex);
    return ryz::rumble::SpeakerResponse(
        render ? state.RenderSnapshot(Now(), watchdog_ms.load(), gain_percent.load() / 100.f) :
                 state.Snapshot(Now(), watchdog_ms.load(), gain_percent.load() / 100.f), boost_weak_signals.load());
}

void ReportCommands(u64 now) {
    static u64 previous_queries = 0, client_reported = 0;
    const u64 queries = client_queries.load();
    if (queries != previous_queries && now - client_reported >= 1000) {
        char text[144];
        std::snprintf(text, sizeof(text), "client queries=%llu accepted=%llu last=%016llX",
            static_cast<unsigned long long>(queries), static_cast<unsigned long long>(client_accepted.load()),
            static_cast<unsigned long long>(queried_program.load()));
        Log(text); previous_queries = queries; client_reported = now;
    }
    // Only MaintainRuntime on the control worker writes these reports.
    static u64 reported = 0, previous_count = 0, previous_seen = 0;
    const u64 count = commands.load();
    const u64 seen = seen_commands.load();
    if ((count != previous_count || seen != previous_seen) && now - reported >= 1000) {
        char text[320];
        std::snprintf(text, sizeof(text), "seen=%llu accepted=%llu game=%016llX handle=%08X on=%u nz=%llu zero=%llu amp=%u/1000 pcm=%u audio=%u err=%08X boost=%u hh=%llu pad=%llu nz_handle=%08X",
            static_cast<unsigned long long>(seen), static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(seen_program.load()), seen_handle.load(), Enabled() ? 1u : 0u,
            static_cast<unsigned long long>(nonzero_updates.load()), static_cast<unsigned long long>(zero_updates.load()),
            input_peak.load(), pcm_peak.load(), audio_started.load() ? 1u : 0u,
            last_audio_error.load(), boost_weak_signals.load() ? 1u : 0u,
            static_cast<unsigned long long>(handheld_nonzero.load()),
            static_cast<unsigned long long>(pad_nonzero.load()), last_nonzero_handle.load());
        Log(text); previous_count = count; previous_seen = seen; reported = now;
    }
}

::Result OpenAudio() {
    const ::Result rc = audoutInitialize();
    if (R_FAILED(rc)) return rc;
    if (audoutGetSampleRate() != 48000 || audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16) {
        audoutExit();
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    // RyazhTune adjusts the foreground game's process volume. This output
    // belongs to our sysmodule PID and retains its own unity gain.
    const ::Result volume_rc = audoutSetAudioOutVolume(1.f);
    if (R_FAILED(volume_rc)) { audoutExit(); return volume_rc; }
    float actual = 0;
    const ::Result read_rc = audoutGetAudioOutVolume(&actual);
    stream_gain.store(R_SUCCEEDED(read_rc) && std::isfinite(actual) && actual >= 0 && actual <= 4 ?
        static_cast<u32>(std::lround(actual * 1000)) : UINT32_MAX);
    first_process_gain.store(UINT32_MAX);
    process_gain.store(UINT32_MAX); process_result.store(UINT32_MAX);
    submitted_frames.store(0); played_frames.store(0); slow_refills.store(0);
    max_refill_ms.store(0); wait_timeouts.store(0);
    return 0;
}
::Result PlayAudio() {
    const ::Result opened = OpenAudio();
    if (R_FAILED(opened)) return opened;
    auto cleanup = SCOPE_GUARD { audio_started.store(false); audoutStopAudioOut(); audoutExit(); };
    ryz::rumble::Synth synth;
    for (unsigned i = 0; i < BufferCount; ++i) {
        buffers[i] = {};
        buffers[i].buffer = samples[i];
        buffers[i].buffer_size = sizeof(samples[i]);
        buffers[i].data_size = Frames * 2 * sizeof(s16);
        synth.Render(samples[i], Frames, Snapshot(true), 1000);
        MeasurePcm(samples[i], Frames * 2);
        armDCacheFlush(samples[i], buffers[i].data_size);
        const ::Result rc = audoutAppendAudioOutBuffer(&buffers[i]);
        if (R_FAILED(rc)) return rc;
        submitted_frames.fetch_add(Frames);
    }
    ::Result rc = audoutStartAudioOut();
    if (R_FAILED(rc)) return rc;
    audio_started.store(true);
    last_audio_error.store(0);
    u64 idle_since = 0, last_refill = Now(), last_probe = last_refill;
    while (true) {
        const u64 now = Now();
        const auto levels = Snapshot();
        if (levels.left == 0 && levels.right == 0 && (!Enabled() || active_clients.load() == 0)) {
            if (!idle_since) idle_since = now;
            if (now - idle_since >= 500) return 0;
        } else { idle_since = 0; }
        AudioOutBuffer* buffer = nullptr; u32 released = 0;
        rc = audoutWaitPlayFinish(&buffer, &released, 50'000'000ULL);
        if (R_VALUE(rc) == KERNELRESULT(TimedOut)) { wait_timeouts.fetch_add(1); continue; }
        if (R_FAILED(rc)) return rc;
        if (!released) continue;
        bool ours = false;
        for (auto& candidate : buffers) if (buffer == &candidate) ours = true;
        if (!ours)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        synth.Render(static_cast<s16*>(buffer->buffer), Frames, Snapshot(true), 1000);
        MeasurePcm(static_cast<const s16*>(buffer->buffer), Frames * 2);
        armDCacheFlush(buffer->buffer, buffer->data_size);
        rc = audoutAppendAudioOutBuffer(buffer);
        if (R_FAILED(rc)) return rc;
        submitted_frames.fetch_add(Frames);
        const u64 refill = Now();
        const u64 gap = refill - last_refill;
        RaisePeak(max_refill_ms, static_cast<u32>(std::min<u64>(gap, UINT32_MAX)));
        if (gap > BufferCount * 10) slow_refills.fetch_add(1);
        last_refill = refill;
        if (refill - last_probe >= 1000) {
            u64 count = 0;
            if (R_SUCCEEDED(audoutGetAudioOutPlayedSampleCount(&count))) played_frames.store(count);
            last_probe = refill;
        }
    }
}
void AudioMain(void*) {
    Log("audio thread entered");
    while (true) {
        const auto levels = Snapshot();
        // Prime at game HID connection time, before its first vibration. A
        // cold audio open can otherwise swallow short on/stop command pairs.
        if ((Enabled() && active_clients.load() > 0) || levels.left > 0 || levels.right > 0) {
            const ::Result rc = PlayAudio();
            last_audio_error.store(rc);
            if (R_FAILED(rc)) { Log("audio error", rc); os::SleepThread(TimeSpan::FromSeconds(1)); }
        } else { os::SleepThread(TimeSpan::FromMilliSeconds(10)); }
    }
}
}

void MaintainRuntime() {
    // Called only by the existing volume/control worker. SD I/O and
    // per-process mixer requests must not starve the PCM refill deadline.
    static u64 checked = 0, reported = 0;
    static bool was_started = false;
    const u64 now = Now();
    if (now - checked < 1000) return;
    checked = now;
    ReloadConfig();
    const bool started = audio_started.load();
    if (started) {
        ::Result rc = audaInitialize();
        if (R_SUCCEEDED(rc)) {
            u64 pid = 0;
            rc = svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
            float gain = 0;
            if (R_SUCCEEDED(rc)) rc = audaGetAudioOutputProcessMasterVolume(pid, &gain);
            if (R_SUCCEEDED(rc) && (!std::isfinite(gain) || gain < 0 || gain > 4))
                rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (R_SUCCEEDED(rc)) {
                u32 unknown = UINT32_MAX;
                first_process_gain.compare_exchange_strong(unknown, static_cast<u32>(std::lround(gain * 1000)));
                // Only our own haptic process, never the game or audio input.
                if (Enabled() && std::fabs(gain - 1.f) > .001f)
                    rc = audaSetAudioOutputProcessMasterVolume(pid, 0, 1.f);
                if (R_SUCCEEDED(rc)) rc = audaGetAudioOutputProcessMasterVolume(pid, &gain);
                if (R_SUCCEEDED(rc)) {
                    if (std::isfinite(gain) && gain >= 0 && gain <= 4)
                        process_gain.store(static_cast<u32>(std::lround(gain * 1000)));
                    else rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
                }
            }
            audaExit();
        }
        process_result.store(rc);
    }
    if (started && !was_started) Log("audio started: fixed 225 Hz, strength 1000, antiphase; 30 ms queue");
    was_started = started;
    ReportCommands(now);
    if (started && now - reported >= 5000) {
        reported = now;
        char text[240];
        std::snprintf(text, sizeof(text), "audio transport: sent=%llu played=%llu slow=%llu gap=%u timeouts=%u stream=%u process=%u first=%u process_rc=%08X",
            static_cast<unsigned long long>(submitted_frames.load()), static_cast<unsigned long long>(played_frames.load()),
            static_cast<unsigned long long>(slow_refills.load()), max_refill_ms.load(), wait_timeouts.load(),
            stream_gain.load(), process_gain.load(), first_process_gain.load(), process_result.load());
        Log(text);
    }
}

void Log(const char* message, u32 result) {
    std::scoped_lock lock(log_mutex);
    char line[384];
    const int length = std::snprintf(line, sizeof(line), "%llu %s: %08X\n",
        static_cast<unsigned long long>(Now()), message, result);
    AMS_UNUSED(fs::CreateFile(LogPath, 0));
    fs::FileHandle file;
    if (R_FAILED(fs::OpenFile(&file, LogPath, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) return;
    s64 size = 0;
    if (R_SUCCEEDED(fs::GetFileSize(&size, file))) {
        if (size > 65536) { AMS_UNUSED(fs::SetFileSize(file, 0)); size = 0; }
        AMS_UNUSED(fs::WriteFile(file, size, line, std::min(static_cast<size_t>(length), sizeof(line) - 1), fs::WriteOption::Flush));
    }
    fs::CloseFile(file);
}
bool Enabled() { return lite.load() && enabled.load(); }
bool IsLite() { return lite.load(); }
void ObserveCommand(u64 program, u32 handle) {
    seen_program.store(program); seen_handle.store(handle); seen_commands.fetch_add(1);
}
void ObserveProbe(u32 result) { last_probe_result.store(result); probe_count.fetch_add(1); }
void ObserveClient(u64 program, bool accepted) {
    queried_program.store(program); client_queries.fetch_add(1);
    if (accepted) client_accepted.fetch_add(1);
}
ryz::rumble::ClientStatus GetClientStatus() {
    return {client_queries.load(), client_accepted.load(), queried_program.load(), session_program.load()};
}
ryz::rumble::SignalStatus GetSignalStatus() {
    return {signal_program.load(), nonzero_updates.load(), zero_updates.load(), input_peak.load(), pcm_peak.load()};
}
void SetMitmResult(u32 result) { mitm_result.store(result); }
ryz::rumble::DebugStatus GetDebugStatus() {
    return {seen_commands.load(), commands.load(), seen_program.load(), session_program.load(),
        seen_handle.load(), (IsLite() ? 1u : 0u) | (Enabled() ? 2u : 0u) |
        (audio_started.load() ? 4u : 0u) | (thread_started.load() ? 8u : 0u),
        last_audio_error.load(), mitm_result.load(), active_clients.load(), 21, {probe_count.load(),last_probe_result.load()}};
}
ryz::rumble::AudioTransportStatus GetAudioTransportStatus() {
    return {submitted_frames.load(), played_frames.load(), slow_refills.load(), stream_gain.load(),
        process_gain.load(), first_process_gain.load(), process_result.load(), max_refill_ms.load(), wait_timeouts.load()};
}
void Initialize() {
    AMS_UNUSED(fs::CreateDirectory("sdmc:/config"));
    AMS_UNUSED(fs::CreateDirectory("sdmc:/config/ryz-lite-rumble"));
    SetSysProductModel model = SetSysProductModel_Invalid;
    const ::Result rc = setsysGetProductModel(&model);
    setsysExit();
    lite.store(R_SUCCEEDED(rc) && model == SetSysProductModel_Hoag);
    Log("module starting", rc);
    Log("module version", 21);
    Log("product model", static_cast<u32>(model));
    Log("loading configuration");
    ReloadConfig();
    Log("configuration loaded");
}
void StartAudio() {
    constexpr s32 priority = os::DefaultThreadPriority;
    static_assert(priority >= os::HighestThreadPriority && priority <= os::LowestThreadPriority,
        "Stratosphere uses user thread priorities, not Horizon kernel priorities");
    Log("create audio thread begin, user priority", static_cast<u32>(priority));
    const Result rc = os::CreateThread(&audio_thread, AudioMain, nullptr, audio_stack, sizeof(audio_stack), priority, 3);
    Log("create audio thread", rc.GetValue());
    if (R_SUCCEEDED(rc)) { os::SetThreadNamePointer(&audio_thread, "RyzRumbleAudio"); os::StartThread(&audio_thread); thread_started.store(true); }
    else { enabled.store(false); }
}
void SessionOpened(u64 process, u64 program) {
    session_program.store(program);
    std::scoped_lock lock(state_mutex);
    for (auto& client : clients) if (client.process == process && client.references) {
        ++client.references; active_clients.fetch_add(1); return;
    }
    for (auto& client : clients) if (!client.references) {
        client = {process, 1}; active_clients.fetch_add(1); return;
    }
}
void SessionClosed(u64 process) {
    std::scoped_lock lock(state_mutex);
    for (auto& client : clients) if (client.process == process && client.references) {
        active_clients.fetch_sub(1);
        if (--client.references == 0) { state.StopOwner(process); client.process = 0; }
        return;
    }
}
bool Publish(u64 process, u64 program, const u32* handles, const ryz::rumble::Value* values, size_t count) {
    std::scoped_lock lock(state_mutex);
    if (!state.Apply(process, handles, values, count, Now())) return false;
    if (signal_program.load() != program) {
        signal_program.store(program); nonzero_updates.store(0); zero_updates.store(0);
        input_peak.store(0); pcm_peak.store(0);
        handheld_nonzero.store(0); pad_nonzero.store(0); last_nonzero_handle.store(0);
    }
    for (size_t i=0;i<count;++i) if (ryz::rumble::IsRenderable(handles[i])) {
        const float amplitude = std::max(values[i].amp_low, values[i].amp_high);
        if (amplitude > 0) {
            nonzero_updates.fetch_add(1); last_nonzero_handle.store(handles[i]);
            if (ryz::rumble::IsVirtual(handles[i])) handheld_nonzero.fetch_add(1);
            else pad_nonzero.fetch_add(1);
        } else zero_updates.fetch_add(1);
        RaisePeak(input_peak, static_cast<u32>(std::lround(amplitude * 1000.f)));
    }
    last_program.store(program); commands.fetch_add(1);
    return true;
}
ryz::rumble::Value Actual(u64 process, u32 handle) {
    std::scoped_lock lock(state_mutex);
    return state.Actual(process, handle);
}
}
