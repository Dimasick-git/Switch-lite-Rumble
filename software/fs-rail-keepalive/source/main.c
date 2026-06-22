/*
 * fs-rail-keepalive - version-agnostic runtime patcher for the fs power gate.
 *
 * Instead of build-id-specific IPS files (which break on every HOS update),
 * this sysmodule attaches to the running `fs` process with debug SVCs, scans
 * its executable .text for byte-pattern signatures (see patches.h), and writes
 * ARM64 NOPs over the power-gate branches at runtime. One signature set works
 * across HOS versions while the surrounding code is stable - the same strategy
 * sys-patch uses for nogc/es/nim patches.
 *
 * Flow:
 *   1. pmdmnt -> resolve the fs process id.
 *   2. svcDebugActiveProcess(fs_pid) -> suspends fs, gives a debug handle.
 *   3. Walk memory regions with svcQueryDebugProcessMemory; for each R-X
 *      (code) region, read it in overlapping windows and scan each signature.
 *   4. On a match, svcWriteDebugProcessMemory the patch bytes.
 *   5. svcCloseHandle(debug) -> resumes fs with the patches live.
 *
 * Power-gating runs on cartridge insertion, which happens after boot, so
 * patching the already-running fs in place takes effect for later insertions.
 *
 * Log: sdmc:/fs-rail-keepalive.log
 *
 * SAFETY: ships with all signatures disabled (patches.h). With nothing enabled
 * it walks, logs, and patches nothing. It cannot mispatch until you add a
 * verified signature. See README.md.
 *
 * NOTE: scaffold - written to libnx sysmodule conventions, not yet run on HW.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "patches.h"

/* fs system-module program id. Verify against your firmware if matching fails. */
#define FS_PROGRAM_ID 0x0100000000000000ULL

/* Scan window: read code in WINDOW-byte chunks, overlapping by OVERLAP so a
 * signature straddling a chunk boundary is still found. OVERLAP must exceed the
 * longest signature in bytes. */
#define SCAN_WINDOW  0x8000
#define SCAN_OVERLAP 0x80
#define MAX_PAT_LEN  0x40

/* ---- sysmodule runtime configuration ---------------------------------- */

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

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

    rc = pmdmntInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    fsdevMountSdmc();
    smExit();
}

void __appExit(void)
{
    fsdevUnmountAll();
    pmdmntExit();
    fsExit();
}

/* ---- logging ---------------------------------------------------------- */

static void log_line(const char *fmt, ...)
{
    FILE *f = fopen("sdmc:/fs-rail-keepalive.log", "a");
    if (f == NULL)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---- hex / pattern helpers -------------------------------------------- */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Parse a pattern string ("f3 .. 1e f8") into out_bytes/out_mask.
 * out_mask[i] = 1 means "must equal out_bytes[i]"; 0 means wildcard.
 * Returns pattern length in bytes, or -1 on malformed input.
 */
static int parse_pattern(const char *p, u8 *out_bytes, u8 *out_mask, int cap)
{
    int n = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (n >= cap) return -1;

        if (p[0] == '.' && p[1] == '.') {
            out_bytes[n] = 0;
            out_mask[n]  = 0;
            n++;
            p += 2;
            continue;
        }
        int hi = hex_nibble(p[0]);
        int lo = hex_nibble(p[1]);
        if (hi < 0 || lo < 0) return -1;
        out_bytes[n] = (u8)((hi << 4) | lo);
        out_mask[n]  = 1;
        n++;
        p += 2;
    }
    return n;
}

/* Parse a plain hex string ("1F2003D5") into bytes. Returns length or -1. */
static int parse_hex(const char *p, u8 *out, int cap)
{
    int n = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        if (n >= cap) return -1;
        int hi = hex_nibble(p[0]);
        int lo = hex_nibble(p[1]);
        if (hi < 0 || lo < 0) return -1;
        out[n++] = (u8)((hi << 4) | lo);
        p += 2;
    }
    return n;
}

/* Find pat (with mask) in buf. Returns index of first match, or -1. */
static long find_pattern(const u8 *buf, long buflen,
                         const u8 *pat, const u8 *mask, int patlen)
{
    if (patlen <= 0 || buflen < patlen)
        return -1;
    for (long i = 0; i + patlen <= buflen; i++) {
        int ok = 1;
        for (int j = 0; j < patlen; j++) {
            if (mask[j] && buf[i + j] != pat[j]) { ok = 0; break; }
        }
        if (ok)
            return i;
    }
    return -1;
}

/* ---- core: scan one code region for one signature --------------------- */

/*
 * Scan [region_base, region_base+region_size) of the debugged process for the
 * signature, reading in overlapping windows. On match, write the patch.
 * Returns 1 if patched, 0 if not found, negative on error.
 */
static int scan_and_patch_region(Handle debug, u64 region_base, u64 region_size,
                                 const FsRailPatch *patch)
{
    u8 pat[MAX_PAT_LEN], mask[MAX_PAT_LEN];
    int patlen = parse_pattern(patch->pattern, pat, mask, MAX_PAT_LEN);
    if (patlen <= 0) {
        log_line("  [%s] empty/invalid pattern - skipped", patch->name);
        return 0;
    }

    u8 patch_bytes[16];
    int patch_len = parse_hex(patch->patch, patch_bytes, sizeof(patch_bytes));
    if (patch_len <= 0) {
        log_line("  [%s] invalid patch bytes - skipped", patch->name);
        return -1;
    }

    static u8 win[SCAN_WINDOW + SCAN_OVERLAP];
    u64 pos = 0;
    while (pos < region_size) {
        u64 chunk = region_size - pos;
        if (chunk > SCAN_WINDOW + SCAN_OVERLAP)
            chunk = SCAN_WINDOW + SCAN_OVERLAP;

        Result rc = svcReadDebugProcessMemory(win, debug, region_base + pos, chunk);
        if (R_FAILED(rc)) {
            log_line("  [%s] read failed @0x%lx rc=0x%x", patch->name,
                     region_base + pos, rc);
            return -1;
        }

        long idx = find_pattern(win, (long)chunk, pat, mask, patlen);
        if (idx >= 0) {
            u64 match_addr = region_base + pos + (u64)idx;
            u64 write_addr = match_addr + (s64)patch->patch_offset;
            rc = svcWriteDebugProcessMemory(debug, patch_bytes, write_addr, patch_len);
            if (R_FAILED(rc)) {
                log_line("  [%s] WRITE FAILED @0x%lx rc=0x%x", patch->name,
                         write_addr, rc);
                return -1;
            }
            log_line("  [%s] match @0x%lx -> patched @0x%lx (%d bytes)",
                     patch->name, match_addr, write_addr, patch_len);
            return 1;
        }

        if (chunk < SCAN_WINDOW + SCAN_OVERLAP)
            break;                       /* last (short) chunk done */
        pos += SCAN_WINDOW;              /* advance, keeping OVERLAP */
    }
    return 0;
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    log_line("fs-rail-keepalive: start");

    /* Count enabled signatures up front; if none, do not even attach. */
    int enabled = 0;
    for (size_t i = 0; i < FS_RAIL_PATCH_COUNT; i++)
        if (g_fs_rail_patches[i].enabled) enabled++;

    if (enabled == 0) {
        log_line("no signatures enabled - nothing to patch (safe no-op). "
                 "Fill patches.h and rebuild.");
        /* idle; nothing to do */
        while (true) svcSleepThread(1'000'000'000ULL);
        return 0;
    }

    u64 fs_pid = 0;
    Result rc = pmdmntGetProcessId(&fs_pid, FS_PROGRAM_ID);
    if (R_FAILED(rc)) {
        log_line("pmdmntGetProcessId(fs) failed rc=0x%x - is FS_PROGRAM_ID correct?", rc);
        while (true) svcSleepThread(1'000'000'000ULL);
        return 0;
    }
    log_line("fs pid = %lu", fs_pid);

    Handle debug;
    rc = svcDebugActiveProcess(&debug, fs_pid);
    if (R_FAILED(rc)) {
        log_line("svcDebugActiveProcess failed rc=0x%x", rc);
        while (true) svcSleepThread(1'000'000'000ULL);
        return 0;
    }

    /* Walk the address space; scan executable regions. */
    int total_patched = 0;
    u64 addr = 0;
    while (true) {
        MemoryInfo mi = {0};
        u32 pi = 0;
        rc = svcQueryDebugProcessMemory(&mi, &pi, debug, addr);
        if (R_FAILED(rc))
            break;

        if ((mi.perm & Perm_X) && mi.size > 0) {
            log_line("code region @0x%lx size=0x%lx perm=0x%x",
                     mi.addr, mi.size, mi.perm);
            for (size_t i = 0; i < FS_RAIL_PATCH_COUNT; i++) {
                const FsRailPatch *p = &g_fs_rail_patches[i];
                if (!p->enabled)
                    continue;
                int r = scan_and_patch_region(debug, mi.addr, mi.size, p);
                if (r == 1)
                    total_patched++;
            }
        }

        u64 next = mi.addr + mi.size;
        if (next <= addr)               /* no progress / wrap -> done */
            break;
        addr = next;
    }

    /* Closing the debug handle resumes fs with the patches live. */
    svcCloseHandle(debug);
    log_line("done: %d/%d signature(s) patched", total_patched, enabled);

    while (true)
        svcSleepThread(1'000'000'000ULL);
    return 0;
}
