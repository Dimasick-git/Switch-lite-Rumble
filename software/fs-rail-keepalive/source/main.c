/*
 * fs-rail-keepalive - version-agnostic runtime patcher / discovery tool.
 *
 * TWO MODES, auto-selected by patches.h:
 *
 * PATCH MODE (at least one signature enabled=true in patches.h)
 *   Attaches to fs, scans .text for the enabled signatures, writes NOPs.
 *   Resumes fs with the power-gate branches neutralised.
 *
 * DISCOVERY MODE (all signatures disabled -- the default out of the box)
 *   Same attach + scan, but with a BROAD pattern: every BL immediately
 *   followed by CBNZ/CBZ X0 or W0. Logs each candidate with a ready-to-paste
 *   byte pattern (ARM64 immediates auto-masked as "..") to
 *   sdmc:/fs-rail-keepalive.log, then idles.
 *
 *   Workflow:
 *     1. Run nxdumptool -> System Titles -> fs -> ExeFS dump.
 *        (Gets you the .nso for the PC path with find_offsets.py.)
 *     2. Boot with this sysmodule installed and NO signatures enabled.
 *     3. Read sdmc:/fs-rail-keepalive.log - each CANDIDATE block contains
 *        a ready-to-paste pattern.  Open the binary in Ghidra to confirm
 *        which candidate's branch target leads to the PMIC power-off path.
 *     4. Paste the chosen pattern into patches.h, set enabled=true, rebuild.
 *     5. On next boot the module runs in PATCH MODE instead.
 *
 * Log: sdmc:/fs-rail-keepalive.log
 * FS program id: 0x0100000000000000 (must be running; true for any HOS).
 *
 * NOTE: scaffold; written to libnx sysmodule conventions, not yet run on HW.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "patches.h"

#define FS_PROGRAM_ID        0x0100000000000000ULL
#define SCAN_WINDOW          0x8000
#define SCAN_OVERLAP         0x80
#define MAX_PAT_LEN          0x40
#define DISCOVERY_MAX_HITS   256        /* cap output size in discovery mode */
#define SIG_CTX_WORDS_BEFORE 4
#define SIG_CTX_WORDS_AFTER  2

/* ---- sysmodule config -------------------------------------------------- */

u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

#define INNER_HEAP_SIZE 0x80000
size_t nx_inner_heap_size = INNER_HEAP_SIZE;
char   nx_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void) {
    extern char *fake_heap_start, *fake_heap_end;
    fake_heap_start = nx_inner_heap;
    fake_heap_end   = nx_inner_heap + nx_inner_heap_size;
}

void __appInit(void) {
    Result rc;
    rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);
    rc = pmdmntInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);
    fsdevMountSdmc();
    smExit();
}

void __appExit(void) {
    fsdevUnmountAll();
    pmdmntExit();
    fsExit();
}

/* ---- logging ----------------------------------------------------------- */

static FILE *g_log = NULL;

static void log_open(void) {
    g_log = fopen("sdmc:/fs-rail-keepalive.log", "a");
}
static void log_close(void) {
    if (g_log) { fclose(g_log); g_log = NULL; }
}
static void logf(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* ---- hex / pattern helpers --------------------------------------------- */

static int hex_nibble(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
}

static int parse_pattern(const char *p, u8 *bytes, u8 *mask, int cap) {
    int n = 0;
    while (*p) {
        while (*p==' '||*p=='\t') p++;
        if (!*p) break;
        if (n >= cap) return -1;
        if (p[0]=='.'&&p[1]=='.') { bytes[n]=0; mask[n]=0; n++; p+=2; continue; }
        int hi=hex_nibble(p[0]), lo=hex_nibble(p[1]);
        if (hi<0||lo<0) return -1;
        bytes[n]=(u8)((hi<<4)|lo); mask[n]=1; n++; p+=2;
    }
    return n;
}

static int parse_hex(const char *p, u8 *out, int cap) {
    int n=0;
    while (*p) {
        while (*p==' ') p++;
        if (!*p) break;
        if (n>=cap) return -1;
        int hi=hex_nibble(p[0]),lo=hex_nibble(p[1]);
        if (hi<0||lo<0) return -1;
        out[n++]=(u8)((hi<<4)|lo); p+=2;
    }
    return n;
}

static long find_pattern(const u8 *buf, long blen, const u8 *pat, const u8 *mask, int plen) {
    if (plen<=0||blen<plen) return -1;
    for (long i=0; i+plen<=blen; i++) {
        int ok=1;
        for (int j=0;j<plen;j++) if (mask[j]&&buf[i+j]!=pat[j]) {ok=0;break;}
        if (ok) return i;
    }
    return -1;
}

/* ---- ARM64 instruction helpers (for discovery mode) -------------------- */

typedef u32 insn_t;

static inline insn_t rd_insn(const u8 *buf, long off) {
    insn_t w;
    __builtin_memcpy(&w, buf+off, 4);
    return __builtin_bswap32(__builtin_bswap32(w));  /* keep LE */
}

static inline int is_bl(insn_t w)     { return (w&0xFC000000)==0x94000000; }
static inline int is_cbz64(insn_t w)  { return (w&0xFF000000)==0xB4000000; }
static inline int is_cbnz64(insn_t w) { return (w&0xFF000000)==0xB5000000; }
static inline int is_cbz32(insn_t w)  { return (w&0xFF000000)==0x34000000; }
static inline int is_cbnz32(insn_t w) { return (w&0xFF000000)==0x35000000; }
static inline int is_any_cb(insn_t w) {
    return is_cbz64(w)||is_cbnz64(w)||is_cbz32(w)||is_cbnz32(w);
}
static inline int insn_reg(insn_t w)  { return (int)(w&0x1F); }

/*
 * Build a pasteable signature token string for one 32-bit instruction.
 * ARM64 branch immediates are NOT byte-aligned:
 *   BL imm26  -> all 4 bytes vary
 *   CBZ/CBNZ  -> bytes 0-2 vary (imm19 + Rt); byte 3 (opcode) is stable
 *   Other     -> emit literal hex bytes (assumed stable)
 */
static void insn_sig_tokens(insn_t w, char *out8) {
    u8 b[4];
    __builtin_memcpy(b, &w, 4);
    if (is_bl(w)) {
        strcpy(out8, ".. .. .. ..");
        return;
    }
    if (is_any_cb(w)) {
        snprintf(out8, 16, ".. .. .. %02x", b[3]);
        return;
    }
    snprintf(out8, 16, "%02x %02x %02x %02x", b[0],b[1],b[2],b[3]);
}

/*
 * Emit a context window centred on off_cb (the CBZ/CBNZ instruction).
 * Returns the patch_offset (bytes from pattern start to the CBZ/CBNZ).
 */
static int build_signature(const u8 *region, long region_size,
                           long off_bl, char *pat_out, int pat_cap) {
    long start = off_bl - SIG_CTX_WORDS_BEFORE * 4;
    if (start < 0) start = 0;
    long end = off_bl + 8 + SIG_CTX_WORDS_AFTER * 4;  /* BL + CBZ + after */
    if (end > region_size) end = region_size;

    pat_out[0] = '\0';
    long o = start;
    while (o < end && o + 4 <= region_size) {
        insn_t w = rd_insn(region, o);
        char tok[16];
        insn_sig_tokens(w, tok);
        if (pat_out[0]) strncat(pat_out, " ", pat_cap - (int)strlen(pat_out) - 1);
        strncat(pat_out, tok, pat_cap - (int)strlen(pat_out) - 1);
        o += 4;
    }
    return (int)(off_bl + 4 - start);  /* byte offset to CBZ/CBNZ in pattern */
}

/* ---- discovery: scan one R-X region ------------------------------------ */

static void discovery_scan_region(const u8 *buf, long blen,
                                  u64 region_base, int *count) {
    /* Broad search: BL immediately followed by CBZ/CBNZ X0 or W0. */
    long n = blen / 4;
    for (long i = 0; i + 1 < n; i++) {
        if (*count >= DISCOVERY_MAX_HITS) break;
        insn_t wbl  = rd_insn(buf, i * 4);
        insn_t wcb  = rd_insn(buf, i * 4 + 4);
        if (!is_bl(wbl)) continue;
        if (!is_any_cb(wcb)) continue;
        if (insn_reg(wcb) != 0) continue;   /* only X0/W0 */

        long off_bl = i * 4;
        u64  abs_bl = region_base + (u64)off_bl;

        /* Build signature string */
        char sig[512];
        int poff = build_signature(buf, blen, off_bl, sig, (int)sizeof(sig));

        /* Determine type label */
        const char *kind;
        if (is_cbnz64(wcb)||is_cbz64(wcb)) kind = "X0 (Result/handle)";
        else                                kind = "W0 (bool)";

        logf("CANDIDATE %d: @0x%lx (region+0x%lx)", *count, abs_bl, off_bl);
        logf("  type     : BL + %s%d [%s]",
             is_cbz64(wcb)||is_cbz32(wcb) ? "CBZ " : "CBNZ",
             insn_reg(wcb), kind);
        logf("  signature: %s", sig);
        logf("  patch_off: %d  (offset from sig start to the CBZ/CBNZ)", poff);
        logf("  patch    : \"1F2003D5\"  (ARM64 NOP)");
        logf("  -> verify: branch target in disassembler leads to PMIC power-off?");
        logf("");
        (*count)++;
    }
}

/* ---- shared: scan + patch one region for one signature ----------------- */

static int scan_and_patch_region(Handle dbg, u64 base, u64 size,
                                 const FsRailPatch *p) {
    u8 pat[MAX_PAT_LEN], mask[MAX_PAT_LEN];
    int plen = parse_pattern(p->pattern, pat, mask, MAX_PAT_LEN);
    if (plen <= 0) { logf("  [%s] empty pattern", p->name); return 0; }

    u8 pbytes[16];
    int pblen = parse_hex(p->patch, pbytes, (int)sizeof(pbytes));
    if (pblen <= 0) { logf("  [%s] bad patch bytes", p->name); return -1; }

    static u8 win[SCAN_WINDOW + SCAN_OVERLAP];
    for (u64 pos = 0; pos < size; ) {
        u64 chunk = size - pos;
        if (chunk > SCAN_WINDOW + SCAN_OVERLAP) chunk = SCAN_WINDOW + SCAN_OVERLAP;

        if (R_FAILED(svcReadDebugProcessMemory(win, dbg, base+pos, chunk))) {
            logf("  [%s] read fail @0x%lx", p->name, base+pos);
            return -1;
        }
        long idx = find_pattern(win, (long)chunk, pat, mask, plen);
        if (idx >= 0) {
            u64 match = base + pos + (u64)idx;
            u64 waddr = match + (s64)p->patch_offset;
            if (R_FAILED(svcWriteDebugProcessMemory(dbg, pbytes, waddr, pblen))) {
                logf("  [%s] WRITE FAIL @0x%lx", p->name, waddr);
                return -1;
            }
            logf("  [%s] match @0x%lx -> NOP @0x%lx", p->name, match, waddr);
            return 1;
        }
        if (chunk < SCAN_WINDOW + SCAN_OVERLAP) break;
        pos += SCAN_WINDOW;
    }
    return 0;
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    log_open();

    /* Count enabled signatures. */
    int enabled = 0;
    for (size_t i = 0; i < FS_RAIL_PATCH_COUNT; i++)
        if (g_fs_rail_patches[i].enabled) enabled++;

    /* Locate fs process. */
    u64 fs_pid = 0;
    Result rc = pmdmntGetProcessId(&fs_pid, FS_PROGRAM_ID);
    if (R_FAILED(rc)) {
        logf("pmdmntGetProcessId(fs) failed rc=0x%x", rc);
        log_close();
        while (true) svcSleepThread(1'000'000'000ULL);
    }

    /* Attach debugger. */
    Handle dbg;
    rc = svcDebugActiveProcess(&dbg, fs_pid);
    if (R_FAILED(rc)) {
        logf("svcDebugActiveProcess failed rc=0x%x", rc);
        log_close();
        while (true) svcSleepThread(1'000'000'000ULL);
    }

    if (enabled == 0) {
        /* ----------------------------------------------------------------
         * DISCOVERY MODE
         * Walk every R-X region, log all BL+CBZ/CBNZ X0/W0 candidates
         * with ready-to-paste signatures.  Output goes to the log file.
         * ---------------------------------------------------------------- */
        logf("=== DISCOVERY MODE ===");
        logf("All signatures disabled. Scanning fs .text for power-gate candidates.");
        logf("Paste a CANDIDATE block into patches.h, set enabled=true, rebuild.");
        logf("Verify the branch target in Ghidra: must lead to PMIC power-off.");
        logf("");
        logf("nxdumptool path for offline analysis:");
        logf("  Launch nxdumptool -> System Titles -> fs");
        logf("  -> NCA/NCA FS dump options -> Program #0 -> ExeFS section -> Start");
        logf("  -> Copy sdmc:/nxdt_rw_proc/.../exefs/main to PC");
        logf("  -> python3 find_offsets.py main --emit-signature");
        logf("");

        int count = 0;
        u64 addr  = 0;
        while (true) {
            MemoryInfo mi = {0}; u32 pi = 0;
            if (R_FAILED(svcQueryDebugProcessMemory(&mi, &pi, dbg, addr))) break;
            if ((mi.perm & Perm_X) && mi.size > 0) {
                logf("--- code region @0x%lx size=0x%lx ---", mi.addr, mi.size);
                /* Read entire region for discovery (broad scan). */
                u8 *buf = malloc(mi.size);
                if (buf) {
                    if (R_SUCCEEDED(svcReadDebugProcessMemory(buf, dbg, mi.addr, mi.size)))
                        discovery_scan_region(buf, (long)mi.size, mi.addr, &count);
                    free(buf);
                }
            }
            u64 next = mi.addr + mi.size;
            if (next <= addr) break;
            addr = next;
            if (count >= DISCOVERY_MAX_HITS) break;
        }
        svcCloseHandle(dbg);
        logf("discovery done: %d candidate(s) logged.", count);
    } else {
        /* ----------------------------------------------------------------
         * PATCH MODE
         * Walk R-X regions, match enabled signatures, NOP the branches.
         * ---------------------------------------------------------------- */
        logf("=== PATCH MODE === (%d signature(s) enabled)", enabled);
        int total = 0;
        u64 addr  = 0;
        while (true) {
            MemoryInfo mi = {0}; u32 pi = 0;
            if (R_FAILED(svcQueryDebugProcessMemory(&mi, &pi, dbg, addr))) break;
            if ((mi.perm & Perm_X) && mi.size > 0) {
                logf("code region @0x%lx size=0x%lx", mi.addr, mi.size);
                for (size_t i = 0; i < FS_RAIL_PATCH_COUNT; i++) {
                    const FsRailPatch *p = &g_fs_rail_patches[i];
                    if (!p->enabled) continue;
                    int r = scan_and_patch_region(dbg, mi.addr, mi.size, p);
                    if (r == 1) total++;
                }
            }
            u64 next = mi.addr + mi.size;
            if (next <= addr) break;
            addr = next;
        }
        svcCloseHandle(dbg);
        logf("done: %d/%d signature(s) patched", total, enabled);
    }

    log_close();
    while (true) svcSleepThread(1'000'000'000ULL);
    return 0;
}
