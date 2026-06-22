# fs/Lotus3 power-gate patch

**Status:** tooling complete; offsets/signatures need hardware extraction (see §6).  
**Owner:** Dimasick-git  
**Related:** `RESEARCH.md §1`, `docs/slot-approach-technical-spec.md §2`, issue #2

---

## 1. The problem: three-function power-gate chain

The Switch `fs` service drives `nn::fs::detail::IDeviceOperator`, which sits on top
of the Lotus3 ASIC. When a cartridge is inserted (CD# pin goes low), `fs` starts a
polling loop. **Three functions in sequence** decide whether the rails stay on:

```
IsGameCardInserted()          <-- poll loop; false -> rails cut immediately
    | true
    v
GetGameCardHandle()           <-- acquires slot handle; failure -> rails cut
    | success
    v
GetGameCardAttribute()        <-- validates card type; failure -> rails cut
    | success
    v
    [rails stay on, bus handed to driver]
```

Patching only `GetGameCardHandle` (the original plan) leaves two cut-points active.
All three branches must be neutralised for the rails to stay alive with no authentic
cart responding. This is the key correction from issue #2.

---

## 2. Why not just use Atmosphere nogc?

`nogc` patches `GetGameCardHandle` to prevent it from writing the firmware version
to the cart (protecting against version-lock on MIG-style carts). It does **not**
keep the rails on after authentication failure -- it still lets power die once the
version check path is done. Our goal is the opposite: keep power alive indefinitely
with no card responding at all.

The diff is small (one or two more NOPs deeper in the chain) but the intent is
different. We start from the same offset search as nogc but patch further.

---

## 3. ARM64 patch plan

Each cut-point is a conditional branch (CBZ/CBNZ or B.cond) that jumps to an error
path which ultimately calls the power-down sequence. One NOP per site is enough.

```
ARM64 NOP: 1F 20 03 D5   (little-endian: D5 03 20 1F)
```

### Site 1 -- IsGameCardInserted

The function returns a bool in W0. The polling loop calls it and, if W0 == 0, cuts
the rails before calling anything else.

Target: the `CBZ W0` / `CBNZ W0` immediately after the return from the polling
function, on the path that leads to `PowerOff`.

```arm
; Before patch:
BL   <IsGameCardInserted_impl>
CBZ  W0, power_cut_label      ; <- NOP THIS

; After patch:
BL   <IsGameCardInserted_impl>
NOP                            ; W0 value ignored, falls through
```

### Site 2 -- GetGameCardHandle

The handle acquisition calls into the Lotus3 driver. On failure (Result != 0), a
branch leads to power-gate.

Target: the `CBNZ X0` / `CBZ X0` after the BL to the handle-acquisition inner call.

```arm
; Before patch:
BL   <AcquireHandle_impl>
CBNZ X0, power_cut_label      ; <- NOP THIS (X0 = Result, non-zero = error)

; After patch:
BL   <AcquireHandle_impl>
NOP                            ; error result ignored, falls through
```

### Site 3 -- GetGameCardAttribute

Called right after handle acquisition to validate the card header attribute byte.
Failure here also triggers a power cut even if the handle succeeded.

Target: `CBNZ X0` after the BL to the attribute-check call.

```arm
; Before patch:
BL   <CheckAttribute_impl>
CBNZ X0, power_cut_label      ; <- NOP THIS

; After patch:
BL   <CheckAttribute_impl>
NOP
```

---

## 4. Open question: Lotus3 hardware timeout

Even with these three fs patches, Lotus3 itself may have a **hardware timeout** that
cuts the physical power lines if no successful init occurs within N milliseconds.
This is implemented in the ASIC and is invisible to the `fs` software patch.

If this timeout exists:
- The rails come up briefly, then Lotus3 kills them regardless of fs state.
- The `GcPower_SlotEnable` path (see `software/gc-power/`) would need to re-enable
  the LDO after the timeout, creating a constant re-enable loop.
- Alternatively, Option A (FPGA/MCU emulating the Lotus3 init sequence well enough
  to satisfy the hardware handshake) is required.

**This is the single biggest unknown on the slot path.** It can only be answered by
putting a logic analyser on the slot pins with the fs patch active and observing
whether the rails stay up or pulse.

---

## 5. Version strategy: offsets vs signatures

There are two ways to apply the three NOPs, and the project ships both. They differ
only in **how they survive a HOS update**.

### 5.1 Build-id IPS (pinned firmware) -- `software/fs-lotus3-patch/`

An `.ips` file keyed by the fs NSO **build id**. Atmosphere applies it at boot.
Simple, no debug SVCs, but the build id changes with **every** fs update, so the
patch silently stops applying after any system update until you re-extract offsets.
Good when you stay on one pinned firmware.

### 5.2 Runtime pattern patcher (any HOS version) -- `software/fs-rail-keepalive/`

A sysmodule that attaches to the running `fs` with debug SVCs, scans `.text` for a
**byte-pattern signature**, and writes the NOPs in memory. Because it matches a
pattern rather than a fixed address, **one signature keeps working across HOS
versions** as long as the code around the gate site is unchanged. This is the same
mechanism `sys-patch` uses, and it is the right choice for the "works on whatever
version I'm on" goal (e.g. 20.0.1 / 20.1.0 today, 22.5.0 tomorrow).

**Sub-byte caveat.** ARM64 branch immediates (`BL imm26`, `CBNZ imm19`) are not
byte-aligned, so the branch instructions themselves cannot be pinned by fixed bytes
-- only their opcode byte is stable. A good signature therefore anchors on nearby
**fully-fixed** instructions (STP/LDP/MOV/ADRP) and applies the patch at a relative
offset. `find_offsets.py --emit-signature` drafts this automatically (it masks the
branch immediates as `..`); refine by hand if a windowed instruction carries an
address that moves between builds.

### 5.3 Which to use

| | IPS (`fs-lotus3-patch`) | Runtime (`fs-rail-keepalive`) |
| :--- | :--- | :--- |
| Binds to | one fs build id | a byte pattern |
| Survives HOS update | no | yes, if code stable |
| Applied by | Atmosphere loader at boot | sysmodule after boot |
| Needs debug SVCs | no | yes |
| Best for | a frozen firmware | any/updating firmware |

---

## 6. Finding offsets / signatures (per binary, once)

Offsets into the `fs` NSO are version-specific; a signature is portable but still
has to be drafted from one real binary. Both start from the same extraction.

### 6.1 Extract the fs binary

```
nxdumptool (on-console) -> dump SystemVersion title -> extract fs.nsp
nsp_tool or hacbrewpack -> extract fs.nso from the NCA
nso2elf (or nx2elf) -> decompress the NSO to a flat binary
```

The decompressed flat binary has the NSO header (0x100 bytes) followed by the `.text`
section. Offsets in the IPS patch are into `.text` (i.e., subtract 0x100 from what a
disassembler shows if it places the header at base 0).

### 6.2 Auto-search with find_offsets.py

```bash
# offsets only:
python3 software/fs-lotus3-patch/tools/find_offsets.py fs_decompressed.bin

# also print portable signatures for the runtime patcher:
python3 software/fs-lotus3-patch/tools/find_offsets.py fs_decompressed.bin --emit-signature
```

The script searches for `BL + CBZ/CBNZ` pairs at each of the three sites using
masked byte patterns. It prints candidate offsets with context. Expect 1-3 candidates
per site; cross-check with a disassembler.

### 6.3 Manual verification in Ghidra / Binary Ninja

1. Load the flat binary at base `0x7100000000` (standard NSO load address).
2. Find `IDeviceOperator` via string refs or vtable.
3. Locate the three functions. Each should have a recognisable call-then-branch pattern.
4. The branch to patch is the one whose target leads (eventually) to a function that
   writes to the PMIC over I2C to disable the LDO -- the same register that
   `GcPower_Enable(&domain, false)` writes on the software side.

### 6.4 Record the result

- **IPS path:** copy `software/fs-lotus3-patch/offsets/template.json` to
  `offsets/<version>.json`, fill the `offset` fields, run `mk_ips.py`.
- **Runtime path:** paste the `--emit-signature` output into
  `software/fs-rail-keepalive/source/patches.h`, set `patch_offset`, flip
  `enabled = true`, rebuild.

---

## 7. Deployment (IPS path)

Atmosphere loads IPS patches for NSO modules from:

```
sdmc:/atmosphere/exefs_patches/<module_name>/<build_id_hex>.ips
```

For the `fs` service the module name is `fs`. The build ID is the first 8 bytes of
the NSO build ID field (at offset 0x40 in the NSO header), printed as uppercase hex.

```bash
# After mk_ips.py generates patch.ips:
BUILD_ID=$(python3 -c "
import struct, sys
d = open('fs_raw.nso','rb').read()
print(d[0x40:0x48].hex().upper())
")
mkdir -p /mnt/sdcard/atmosphere/exefs_patches/fs/$BUILD_ID
cp patch.ips /mnt/sdcard/atmosphere/exefs_patches/fs/$BUILD_ID/
```

Reboot with Atmosphere. On next boot, `fs` will run with the three branches NOPd.

For the runtime path, see `software/fs-rail-keepalive/README.md`.

---

## 8. What happens after the patch

With the three branches NOPd:

1. The rails (3.1V GCA, 1.8V GCC) **stay on** regardless of Lotus3 authentication.
2. The `fs` game-card driver sees "card present" and tries to proceed normally,
   but the official Lotus3 handshake never completes (no authentic cart responding).
3. The `fs` driver sits in a retry/error state -- but because we patched its power
   cut, it does not disable the rails. The slot is powered but the official
   protocol is stalled.
4. At this point, **the DAT0-DAT7 lines are ours**: the FPGA/MCU on the rumble cart
   can use them as a custom parallel/UART bus while the official driver is stuck.

This is the "blind zone" the project needs: power up, official protocol dead,
bus physically available for our custom protocol (see `docs/PROTOCOL.md`).
