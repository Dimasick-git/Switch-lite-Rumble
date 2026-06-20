/*
 * Minimal SD-card logger for the hid vibration MITM. See logger.hpp.
 *
 * Logs per-handle so the left/right vibration domains stay distinguishable (a
 * "full" rumble build needs both sides). The HidVibrationDeviceHandle is a packed
 * u32: byte0 = npad_style_index, byte1 = player_number, byte2 = device_idx
 * (0 = left, 1 = right), byte3 = pad.
 */
#include <stratosphere.hpp>
#include <cstring>
#include "logger.hpp"

namespace ams::mitm::hid {

    namespace {

        constexpr const char *LogMountName = "sdmc";
        constexpr const char *LogFilePath  = "sdmc:/rumble-logger.log";

        constinit os::SdkMutex g_log_mutex;
        constinit bool         g_initialized = false;
        constinit fs::FileHandle g_file;
        constinit s64          g_offset = 0;

        /* Per-handle change throttle so each side (L/R) is logged independently. */
        constexpr size_t MaxTrackedHandles = 8;
        struct HandleAmp { u32 handle; float amp; bool used; };
        constinit HandleAmp g_last[MaxTrackedHandles] = {};

        /* Returns true if this (handle, amp) is a meaningful change worth logging. */
        bool ShouldLog(u32 handle, float amp) {
            HandleAmp *slot = nullptr;
            HandleAmp *free_slot = nullptr;
            for (auto &e : g_last) {
                if (e.used && e.handle == handle) { slot = std::addressof(e); break; }
                if (!e.used && free_slot == nullptr) { free_slot = std::addressof(e); }
            }
            if (slot == nullptr) {
                /* First time we see this handle (or table full → overwrite slot 0). */
                slot = (free_slot != nullptr) ? free_slot : std::addressof(g_last[0]);
                slot->used = true;
                slot->handle = handle;
                slot->amp = amp;
                return true;
            }
            const float d = amp > slot->amp ? amp - slot->amp : slot->amp - amp;
            if (d < 0.03f) {
                return false;
            }
            slot->amp = amp;
            return true;
        }

        void WriteLine(const char *line, size_t len) {
            if (!g_initialized) {
                return;
            }
            if (R_SUCCEEDED(fs::WriteFile(g_file, g_offset, line, len, fs::WriteOption::Flush))) {
                g_offset += static_cast<s64>(len);
            }
        }

    }

    void InitializeLogger() {
        std::scoped_lock lk(g_log_mutex);
        if (g_initialized) {
            return;
        }

        if (R_FAILED(fs::MountSdCard(LogMountName))) {
            return;
        }

        fs::CreateFile(LogFilePath, 0); /* ignore "already exists" */

        if (R_FAILED(fs::OpenFile(std::addressof(g_file), LogFilePath, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
            return;
        }

        if (R_FAILED(fs::GetFileSize(std::addressof(g_offset), g_file))) {
            g_offset = 0;
        }

        g_initialized = true;

        const char *banner = "--- rumble-logger started (cols: tid tick npad idx side al fl ah fh) ---\n";
        WriteLine(banner, std::strlen(banner));
    }

    void LogVibration(u64 program_id, u32 handle, float amp_low, float freq_low, float amp_high, float freq_high) {
        const float amp = amp_low > amp_high ? amp_low : amp_high;

        std::scoped_lock lk(g_log_mutex);

        if (!ShouldLog(handle, amp)) {
            return;
        }

        /* Decode the packed handle. */
        const u8 npad_style = static_cast<u8>(handle & 0xFF);
        const u8 player     = static_cast<u8>((handle >> 8) & 0xFF);
        const u8 device_idx = static_cast<u8>((handle >> 16) & 0xFF);
        const char *side = (device_idx == 0) ? "L" : (device_idx == 1) ? "R" : "?";
        AMS_UNUSED(npad_style);

        char line[176];
        const auto len = util::SNPrintf(line, sizeof(line),
            "tid=%016lx tick=%lu npad=%02x idx=%u side=%s al=%.3f fl=%.1f ah=%.3f fh=%.1f\n",
            program_id, os::GetSystemTick().GetInt64Value(), player, device_idx, side,
            amp_low, freq_low, amp_high, freq_high);

        if (len > 0) {
            WriteLine(line, static_cast<size_t>(len));
        }
    }

}
