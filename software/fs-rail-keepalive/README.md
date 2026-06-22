# fs-rail-keepalive

Version-agnostic runtime patcher for the `fs` cartridge power gate.
Same idea as `sys-patch`: match by byte pattern, not by build-id offset,
so one signature survives HOS updates.

**Two modes, auto-selected by `patches.h`:**

| Mode | When | What it does |
| :--- | :--- | :--- |
| **Discovery** | all `enabled=false` (default) | Scans fs .text, logs ALL BL+CBZ/CBNZ X0/W0 candidates with ready-to-paste signatures |
| **Patch** | ≥1 `enabled=true` | Matches signatures, writes NOPs, resumes fs |

---

## Quick start: nxdumptool → signatures → patch

### Step 1 — dump the fs binary with nxdumptool (on console)

```
Launch nxdumptool from hbmenu
  -> System Titles
  -> fs (0100000000000000)
  -> NCA/NCA FS dump options
  -> Program #0
  -> ExeFS section data dump
  -> Start NCA FS section data dump
```

This writes `sdmc:/nxdt_rw_proc/<title_id>/<build_id>/exefs/main` —
a **decrypted, compressed NSO** file.

### Step 2a — analyse on PC (recommended for first run)

Copy `main` to your PC and run:

```bash
python3 software/fs-lotus3-patch/tools/find_offsets.py main --emit-signature
```

The tool auto-detects the NSO format, decompresses `.text` with LZ4,
prints candidate offsets, and with `--emit-signature` outputs ready-to-paste
byte patterns. No dependencies needed (built-in LZ4); or `pip install lz4`
for faster decoding.

### Step 2b — discover directly on console (no PC)

Install this sysmodule with all signatures **disabled** (the default).
On boot it runs in **Discovery Mode**:

1. Attaches to the running `fs` process via debug SVCs.
2. Scans every R-X region for `BL + CBNZ/CBZ X0/W0` pairs.
3. Logs each candidate with a ready-to-paste `signature:` block to
   `sdmc:/fs-rail-keepalive.log`.

Read the log and open the fs binary in Ghidra (use `main` from Step 1)
to confirm which candidate's branch target leads to the PMIC power-off chain.

### Step 3 — fill in patches.h

Open `source/patches.h`. Copy one of the `CANDIDATE N:` blocks from the log
or the `--emit-signature` output and paste it in:

```c
{
    .name         = "GetGameCardHandle_power_gate_branch",
    .pattern      = "f3 0f 1e f8 .. .. .. .. .. .. .. b5 ..",  // from log/tool
    .patch_offset = 12,    // patch_off value from the log
    .patch        = ARM64_NOP,
    .enabled      = true,  // <-- flip to true
},
```

Repeat for all three power-gate sites.  Rebuild and reinstall.

### Step 4 — verify

On next boot the module switches to **Patch Mode**, logs `N/3 patched`,
and closes. Check `fs-rail-keepalive.log`.

---

## IPS path (alternative, pinned firmware only)

If you stay on one HOS version permanently, use the static IPS approach
instead — it's simpler and has no runtime overhead:

```bash
# Get the build_id from find_offsets.py output, then:
python3 software/fs-lotus3-patch/tools/mk_ips.py \
    --offset <off1> --offset <off2> --offset <off3> \
    --build-id <BUILD_ID_FIRST_8_BYTES>
# Copy the resulting .ips to:
# sdmc:/atmosphere/exefs_patches/fs/<BUILD_ID>.ips
```

See [`../fs-lotus3-patch/README.md`](../fs-lotus3-patch/README.md).

---

## Build

```sh
export DEVKITPRO=/opt/devkitpro
make
```

## Install

```
/atmosphere/contents/0100000000ABE101/exefs.nsp
/atmosphere/contents/0100000000ABE101/flags/boot2.flag
```

Read `sdmc:/fs-rail-keepalive.log` after reboot.

---

## Notes

- `FS_PROGRAM_ID` in `main.c` = `0x0100000000000000`. If `pmdmntGetProcessId`
  fails, verify for your firmware.
- The npdm grants `pm:dmnt` and the debug SVCs (`svcDebugActiveProcess`,
  `svcQuery/Read/WriteDebugProcessMemory`). Atmosphere grants these to
  sysmodules with the right NPDM.
- Discovery mode allocates one region buffer at a time (`malloc(mi.size)`)
  and caps output at 256 candidates.
- Open question: Lotus3 hardware timeout. If the ASIC cuts power lines
  regardless of fs state, the NOPs alone aren't enough. See
  `docs/FS-LOTUS3.md` section 4.
