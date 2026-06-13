# software/

The software half of the project: the custom **sysmodule** that taps the live
rumble signal in the `hid` pipeline and forwards it to an external actuator. This is
the **agreed first step** (see [`../docs/DESIGN-NOTES.md`](../docs/DESIGN-NOTES.md)),
because it's pure software, runs on any modded Switch Lite, and doesn't touch the
game-card slot or any protection.

## Layout

| Path | What it is |
| :--- | :--- |
| [`DEPENDENCIES.md`](./DEPENDENCIES.md) | Toolchain + every library needed, with install commands and links |
| [`rumble-tap-sysmodule/`](./rumble-tap-sysmodule/) | **libnx** PoC: proves the sysmodule loads, drives the vibration API, and logs to `sdmc:/rumble-tap.log`. Built in CI. |
| [`rumble-logger-mitm/`](./rumble-logger-mitm/) | **libstratosphere `hid` MITM** that logs the vibration a *game* emits to `sdmc:/rumble-logger.log` and forwards it untouched. The real capture tool — **builds green in CI**, produces an SD-ready package. |
| [`gc-power/`](./gc-power/) | Runtime control of the game-card power rail over I²C (GCA ≈ 3.1 V, GCC ≈ 1.8 V) — the "power" half of the in-slot path. Game-card domains only; credit **Cooler3D** / 4IFIR. |

Both sysmodules build automatically on push via
[`.github/workflows/build.yml`](../.github/workflows/build.yml) (devkitPro docker)
and upload an **SD-ready package** — download the artifact, extract, and copy the
`atmosphere/` folder to your SD root. Both jobs are green.

## Milestones (matches the step plan)

1. **First win — button → output (libnx).** A background sysmodule that polls `hid`
   input and reacts to a button press (log / drive an output). Proves the sysmodule
   loads and runs. *This is what the current skeleton targets.*
2. **Vibration capture (libstratosphere MITM).** MITM the `hid` service to read the
   `HidVibrationValue` a game sends to the active player — the real signal we want.
   Template: [jakibaki/hid-mitm](https://github.com/jakibaki/hid-mitm).
3. **Forwarding.** Serialize the values out over USB-CDC / BLE to the external MCU.
4. **Actuator + mapping.** Drive the Taptic engine / LRA, map HD-rumble amplitude &
   frequency to a good physical feel.

## Build (once devkitPro is installed)

```sh
cd rumble-tap-sysmodule
make
```

Output is an `.nsp`/exefs you drop into `atmosphere/contents/<program-id>/` on the
SD card. See `DEPENDENCIES.md` for the full toolchain setup first.

> **Status:** the sysmodule is a compile-ready *skeleton*, written to the libnx
> sysmodule conventions, but it has **not been built or run on hardware yet** — there
> is no devkitPro toolchain in this environment. Treat the code as a starting point
> to build on, not a finished binary.
