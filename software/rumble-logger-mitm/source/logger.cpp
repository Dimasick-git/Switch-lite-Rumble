/*
 * Minimal SD-card logger for the hid vibration MITM. See logger.hpp.
 */
#include <stratosphere.hpp>
#include "logger.hpp"

namespace ams::mitm::hid {

    namespace {

        constexpr const char *LogMountName = "sdmc";
        constexpr const char *LogFilePath  = "sdmc:/rumble-logger.log";

        constinit os::SdkMutex g_log_mutex;
        constinit bool         g_initialized = false;
        constinit fs::FileHandle g_file;
        constinit s64          g_offset = 0;

        /* Simple change-throttle so we don't spam identical frames. */
        constinit float g_last_amp = -1.0f;

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

        /* Create the file if it doesn't exist yet (ignore "already exists"). */
        fs::CreateFile(LogFilePath, 0);

        if (R_FAILED(fs::OpenFile(std::addressof(g_file), LogFilePath, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
            return;
        }

        if (R_FAILED(fs::GetFileSize(std::addressof(g_offset), g_file))) {
            g_offset = 0;
        }

        g_initialized = true;

        const char *banner = "--- rumble-logger started ---\n";
        WriteLine(banner, std::strlen(banner));
    }

    void LogVibration(u64 program_id, u32 handle, float amp_low, float freq_low, float amp_high, float freq_high) {
        const float amp = amp_low > amp_high ? amp_low : amp_high;

        /* Throttle: only log when the dominant amplitude changed noticeably. */
        if (g_last_amp >= 0.0f) {
            const float d = amp > g_last_amp ? amp - g_last_amp : g_last_amp - amp;
            if (d < 0.03f) {
                return;
            }
        }
        g_last_amp = amp;

        std::scoped_lock lk(g_log_mutex);

        char line[160];
        const auto len = util::SNPrintf(line, sizeof(line),
            "tid=%016lx tick=%lu handle=%08x al=%.3f fl=%.1f ah=%.3f fh=%.1f\n",
            program_id, os::GetSystemTick().GetInt64Value(), handle,
            amp_low, freq_low, amp_high, freq_high);

        if (len > 0) {
            WriteLine(line, static_cast<size_t>(len));
        }
    }

}
