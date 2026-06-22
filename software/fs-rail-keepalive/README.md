# fs-rail-keepalive

Version-agnostic runtime patcher for the `fs` cartridge power gate.

Where [`../fs-lotus3-patch/`](../fs-lotus3-patch/) produces **build-id-specific
IPS files** (one per HOS version), this sysmodule does the same job **at runtime
by pattern matching**, so a single signature set survives HOS updates as long as
the code around each gate site is unchanged. Same idea as `sys-patch`.

> **Scaffold + safe no-op by default.** Written to libnx sysmodule conventions,
> not yet run on hardware. Ships with every signature disabled (`patches.h`), so
> out of the box it attaches, logs, and patches **nothing**.

## How it works

1. `pmdmnt` resolves the `fs` process id.
2. `svcDebugActiveProcess(fs_pid)` suspends fs and returns a debug handle.
3. It walks memory with `svcQueryDebugProcessMemory`, and for each executable
   (R-X) region reads it in overlapping windows and scans each signature.
4. On a match it writes the patch (ARM64 NOP) with `svcWriteDebugProcessMemory`.
5. Closing the debug handle resumes fs with the patches live.

Power-gating runs on cartridge **insertion** (after boot), so patching the
already-running fs in place is sufficient for later insertions.

## IPS vs runtime - which to use

| | `fs-lotus3-patch` (IPS) | `fs-rail-keepalive` (runtime) |
| :--- | :--- | :--- |
| Binds to | one fs build id | a byte pattern |
| Survives HOS update | no - re-extract offsets | yes, if code stable |
| Applied by | Atmosphere loader at boot | this sysmodule after boot |
| Needs debug SVCs | no | yes (in npdm) |
| Best for | a pinned firmware | "works on whatever I'm on" |

They are interchangeable in effect. Use this one for the "any HOS version" goal;
keep the IPS path for a frozen setup.

## Authoring signatures

You still extract the pattern once from a real fs binary - but unlike an offset,
a good pattern keeps matching across versions.

```bash
# dump + decompress fs (see ../fs-lotus3-patch/README.md), then:
python3 ../fs-lotus3-patch/tools/find_offsets.py fs_decompressed.bin --emit-signature
```

That prints a byte window around each candidate with immediate bytes already
masked as `..`. Paste the chosen one into `source/patches.h`, set `patch_offset`
to land on the branch to NOP, flip `enabled = true`, rebuild.

Pattern syntax: space-separated hex, `..` = one wildcard byte. Anchor on stable
instructions (STP/LDP/MOV/ADRP) - ARM64 branch immediates are not byte-aligned
and cannot be pinned directly (see the note in `patches.h`).

## Build

```sh
export DEVKITPRO=/opt/devkitpro
make
```

Produces `fs-rail-keepalive.nsp`.

## Install (Atmosphere)

```
/atmosphere/contents/0100000000ABE101/exefs.nsp
/atmosphere/contents/0100000000ABE101/flags/boot2.flag
```

Reboot. Read `sdmc:/fs-rail-keepalive.log` for what matched and what was patched.

## Notes

- `title_id` `0100000000ABE101` is a **placeholder** in the system-module range
  (the sibling rumble-tap module uses `...E100`). Pick final IDs before release.
- `FS_PROGRAM_ID` in `main.c` is `0x0100000000000000`. If `pmdmntGetProcessId`
  fails, verify the fs program id for your firmware and update it.
- The npdm grants `pm:dmnt` + the debug SVCs (`svcDebugActiveProcess`,
  `svcQuery/Read/WriteDebugProcessMemory`). Debugging a system process also
  requires Atmosphere's `enable_user_exception_handlers`/debug allowance typical
  for sys-patch-style modules.
- Open question unchanged: if Lotus3 has a hardware power timeout, NOPping fs is
  not enough on its own. See [`../../docs/FS-LOTUS3.md`](../../docs/FS-LOTUS3.md) section 4.
