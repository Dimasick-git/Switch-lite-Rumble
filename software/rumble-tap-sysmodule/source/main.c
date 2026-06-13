/*
 * rumble-tap-sysmodule — milestone 1 PoC
 *
 * A background sysmodule that brings up the hid vibration API and proves the
 * read/output loop: it polls input and, on a button combo, emits a debug log
 * line and a short vibration burst to any connected vibration-capable controller.
 *
 * This is the "first win" from docs/DESIGN-NOTES.md §8: confirm a custom sysmodule
 * loads, runs, reads hid, and can drive the vibration API. The real goal —
 * capturing the vibration values a *game* sends — is milestone 2 and needs a
 * libstratosphere MITM of the hid service (see software/README.md). This file
 * deliberately stays on plain libnx so it's the simplest possible starting point.
 *
 * NOTE: scaffold. Not yet built against devkitPro or run on hardware.
 */

#include <stdlib.h>
#include <string.h>
#include <switch.h>

/* ---- sysmodule runtime configuration ---------------------------------- */

/* Run as a pure background sysmodule (no applet, no main-thread services). */
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 0;

#define INNER_HEAP_SIZE 0x80000
size_t nx_inner_heap_size = INNER_HEAP_SIZE;
char nx_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void)
{
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    fake_heap_start = nx_inner_heap;
    fake_heap_end   = nx_inner_heap + nx_inner_heap_size;
}

/* Bring up only the services we actually need. Keeps the footprint small. */
void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = hidInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    smExit(); /* sm is only needed to open the other services */
}

void __appExit(void)
{
    hidExit();
}

/* ---- vibration helpers ------------------------------------------------ */

static HidVibrationDeviceHandle g_vibration_handles[2];
static HidVibrationValue g_value_stop[2];

static void rumble_init(void)
{
    /* Handheld first; on a Lite this maps to the (absent) built-in motors, but
     * the call is harmless and lets us reuse the same path for paired pads. */
    hidInitializeVibrationDevices(g_vibration_handles, 2,
                                  HidNpadIdType_Handheld,
                                  HidNpadStyleTag_NpadHandheld);

    /* A "stop" value: zero amplitude at the conventional rest frequencies. */
    g_value_stop[0].amp_low  = 0.0f;
    g_value_stop[0].freq_low = 160.0f;
    g_value_stop[0].amp_high  = 0.0f;
    g_value_stop[0].freq_high = 320.0f;
    g_value_stop[1] = g_value_stop[0];
}

static void rumble_burst(float amp)
{
    HidVibrationValue v[2];
    v[0].amp_low  = amp;  v[0].freq_low  = 160.0f;
    v[0].amp_high = amp;  v[0].freq_high = 320.0f;
    v[1] = v[0];

    hidSendVibrationValues(g_vibration_handles, v, 2);
    svcSleepThread(150'000'000ULL); /* 150 ms */
    hidSendVibrationValues(g_vibration_handles, g_value_stop, 2);
}

/* ---- main loop -------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    rumble_init();

    while (true) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        /* First-win trigger: L + R together. Replace with the real capture
         * once the hid MITM (milestone 2) is in place. */
        if ((down & HidNpadButton_L) && (down & HidNpadButton_R)) {
            svcOutputDebugString("rumble-tap: trigger\n", 20);
            rumble_burst(0.5f);
        }

        svcSleepThread(16'000'000ULL); /* ~60 Hz poll */
    }

    return 0;
}
