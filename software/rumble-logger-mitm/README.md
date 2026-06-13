# rumble-logger-mitm

The real capture tool: an Atmosphère **MITM sysmodule on the `hid` service** that
logs the vibration values a **game** emits, then forwards every call through to the
real `hid` so the console behaves exactly as normal. This is how we answer the open
question *"does a Switch Lite actually receive rumble values, and what do they look
like?"* without any guesswork.

> **STATUS: builds green in CI; not yet hardware-tested.** The code is written to
> Atmosphère's own MITM pattern (modelled on `ams_mitm` / `bpc_mitm`) and now
> **compiles cleanly** against libstratosphere in the `build` GitHub Actions
> workflow, which uploads an **SD-ready package** on every push. It has not yet been
> run on a console — that's the next step (install, run a game, check the log).

## How it works

- MITMs only **applications** (`ncm::IsApplicationId`) — i.e. games.
- Overrides exactly two `hid` commands and logs their arguments:
  - `201 SendVibrationValue`
  - `206 SendVibrationValues`
- Every other `hid` command (and the two above, after logging) is **forwarded** to
  the real service via `sm::mitm::ResultShouldForwardToSession()`. Nothing about the
  console's behaviour changes; we only observe.
- Logging is throttled (only on amplitude change) and written to
  **`sdmc:/rumble-logger.log`**.

## Build

Needs the `libstratosphere` submodule:

```sh
git submodule update --init --recursive
export DEVKITPRO=/opt/devkitpro
cd software/rumble-logger-mitm
make            # builds libstratosphere, then the module
```

Output: `out/.../rumble-logger-mitm.nsp`.

## Install (Atmosphère)

Easiest: download the **`rumble-logger-mitm-SD`** artifact from the latest green
[build run](https://github.com/Dimasick-git/Switch-lite-Rumble/actions), extract it,
and copy the `atmosphere/` folder onto your SD card root. That gives you exactly:

```
/atmosphere/contents/0100000000ABE200/exefs.nsp          <- the built nsp
/atmosphere/contents/0100000000ABE200/flags/boot2.flag   <- empty file, enables autostart
```

Reboot, **launch a game with obvious rumble**, play for a bit, then read
`sdmc:/rumble-logger.log`. Send that log back and we use it to decide the capture
architecture (does the handheld npad get values, or do we need a virtual controller).

> **Test gotcha:** if you see no entries, make sure **Controller Vibration is enabled
> in System Settings** — there's a known firmware quirk where vibration isn't emitted
> until it's toggled on (documented by the sys-con project). See `../../RESEARCH.md` §13.

## Files

| File | Purpose |
| :--- | :--- |
| `source/main.cpp` | Sysmodule init + MITM server registration for `hid` |
| `source/hid_mitm_service.{hpp,cpp}` | The MITM interface (cmds 201/206) and log-then-forward impl |
| `source/logger.{hpp,cpp}` | Throttled SD-card logger |
| `rumble-logger-mitm.json` | NPDM manifest (MITM permissions) |
| `Makefile`, `system_module.mk` | Build wiring (Atmosphère template; only the libstratosphere include path differs) |
| `libstratosphere/` | Submodule: Atmosphere-libs |
