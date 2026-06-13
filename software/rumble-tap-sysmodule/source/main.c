/*
 * rumble-tap-sysmodule — milestone 1 PoC + logger
 *
 * A background sysmodule that brings up the hid vibration API and proves the
 * read/output loop end to end, writing a log file to the SD card so results can
 * be shared. On the L + R combo it logs an event and sends a short vibration
 * burst to any connected vibration-capable controller.
 *
 * IMPORTANT honesty note: plain libnx CANNOT see the vibration values a *game*
 * emits to another controller — that requires a hid MITM (see
 * ../rumble-logger-mitm/). This module proves the pipeline and the output path,
 * and logs its own activity. The game-capture logging is the MITM module's job.
 *
 * Log file: sdmc:/rumble-tap.log
 *
 * NOTE: scaffold. Built in CI (devkitPro) but not yet run on hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

/* ---- sysmodule runtime configuration ---------------------------------- */

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1; /* needed for SD-card logging via fsdev */

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

void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    rc = hidInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    fsdevMountSdmc();

    smExit(); /* sm only needed to open the other services */
}

void __appExit(void)
{
    fsdevUnmountAll();
    hidExit();
    fsExit();
}

/* ---- logging ---------------------------------------------------------- */

static void log_line(const char *msg)
{
    FILE *f = fopen("sdmc:/rumble-tap.log", "a");
    if (f == NULL)
        return;
    u64 tick = armGetSystemTick();
    fprintf(f, "[%lu] %s\n", tick, msg);
    fclose(f);
}

/* ---- vibration helpers ------------------------------------------------ */

static HidVibrationDeviceHandle g_vibration_handles[2];
static HidVibrationValue g_value_stop[2];

static void rumble_init(void)
{
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
    log_line("rumble-tap: started");

    while (true) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        /* L + R: log an event and pulse any vibration-capable controller. */
        if ((down & HidNpadButton_L) && (down & HidNpadButton_R)) {
            log_line("rumble-tap: trigger (L+R) -> burst");
            rumble_burst(0.5f);
        }

        svcSleepThread(16'000'000ULL); /* ~60 Hz poll */
    }

    return 0;
}
