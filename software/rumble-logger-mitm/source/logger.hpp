/*
 * Minimal SD-card logger for the hid vibration MITM.
 *
 * Vibration commands fire very often (multiple times per frame), so we throttle:
 * only entries whose amplitude meaningfully changed are written. Output lands at
 * sdmc:/rumble-logger.log for the user to share.
 */
#pragma once
#include <stratosphere.hpp>

namespace ams::mitm::hid {

    /* Mount the SD card and open the log. Safe to call once at startup. */
    void InitializeLogger();

    /* Log one vibration value sent by a game (throttled on change). */
    void LogVibration(u64 program_id, u32 handle, float amp_low, float freq_low, float amp_high, float freq_high);

}
