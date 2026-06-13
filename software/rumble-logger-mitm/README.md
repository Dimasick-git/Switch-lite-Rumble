# rumble-logger-mitm

The real capture tool: an Atmosphère **MITM sysmodule on the `hid` service** that
logs the vibration values a **game** emits, then forwards every call through to the
real `hid` so the console behaves exactly as normal. This is how we answer the open
question *"does a Switch Lite actually receive rumble values, and what do they look
like?"* without any guesswork.

> **STATUS: work in progress, CI-gated.** The code is written to Atmosphère's own
> MITM pattern (modelled on `ams_mitm` / `bpc_mitm`), but it has **not** been
> verified on hardware and the build is still being brought to green in CI. The
> `build` GitHub Actions workflow compiles it on every push (its job is allowed to
> fail while we iterate). Treat this as the active development target, not a release.

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

```
/atmosphere/contents/0100000000ABE200/exefs.nsp          <- the built nsp
/atmosphere/contents/0100000000ABE200/flags/boot2.flag   <- empty file, enables autostart
```

Reboot, **launch a game with obvious rumble**, play for a bit, then read
`sdmc:/rumble-logger.log`. Send that log back and we use it to decide the capture
architecture (does the handheld npad get values, or do we need a virtual controller).

## Files

| File | Purpose |
| :--- | :--- |
| `source/main.cpp` | Sysmodule init + MITM server registration for `hid` |
| `source/hid_mitm_service.{hpp,cpp}` | The MITM interface (cmds 201/206) and log-then-forward impl |
| `source/logger.{hpp,cpp}` | Throttled SD-card logger |
| `rumble-logger-mitm.json` | NPDM manifest (MITM permissions) |
| `Makefile`, `system_module.mk` | Build wiring (Atmosphère template; only the libstratosphere include path differs) |
| `libstratosphere/` | Submodule: Atmosphere-libs |
