# Dependencies & toolchain

Everything needed to build and work on the software side. Nothing here is vendored
into the repo — Switch homebrew uses the **devkitPro** package manager (`dkp-pacman`)
to provide the toolchain and libraries, which is the correct, reproducible way.

## 1. Toolchain — devkitPro / devkitA64

The Switch homebrew toolchain. Install devkitPro, then the Switch package group.

- Install guide: https://devkitpro.org/wiki/Getting_Started
- Required package group: **`switch-dev`** (pulls devkitA64 GCC, tools, and libnx)

```sh
# after installing dkp-pacman per the guide:
sudo dkp-pacman -S switch-dev
# environment (usually set by the installer):
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export DEVKITA64=$DEVKITPRO/devkitA64
```

## 2. Core libraries

| Library | Role | Source / how to get it |
| :--- | :--- | :--- |
| **libnx** | The homebrew C library for the Switch — gives us the `hid` vibration API (`hidInitializeVibrationDevices`, `hidSendVibrationValue(s)`, `HidVibrationValue`), services, threads, USB, BT | Provided by devkitPro `switch-dev` (package `libnx`). Source: https://github.com/switchbrew/libnx |
| **libstratosphere** | Atmosphère's C++ sysmodule framework — needed for the **MITM** path (intercepting the `hid` service). Provides the HIPC server manager + MITM registration | Vendored as a git submodule in MITM projects. Source: https://github.com/Atmosphere-NX/Atmosphere-libs (also inside the Atmosphère tree) |

> For **milestone 1** (button → output) plain **libnx** is enough.
> For **milestone 2** (vibration capture) you add **libstratosphere** and build the
> sysmodule as a MITM of `hid`.

## 3. Reference implementations to study

| Project | Why it's useful | License |
| :--- | :--- | :--- |
| [jakibaki/hid-mitm](https://github.com/jakibaki/hid-mitm) | The direct template for MITM'ing `hid` — shows how to register the MITM and reshape service calls | — |
| [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) | Canonical sysmodule structure, `fs`/`hid` service definitions, command IDs | — |
| [switchbrew/switch-examples](https://github.com/switchbrew/switch-examples) (`hid/vibration`) | Minimal, correct use of the libnx vibration API | — |
| [DarkMatterCore/nxdumptool](https://github.com/DarkMatterCore/nxdumptool) | **Gamecard reference.** Talks to the card through the *legit* FS API (`fsOpenGameCardStorage`, `fsDeviceOperator*`), parses CardId/Certificate/InitialData, and even dumps the **Lotus ASIC firmware (LAFW)** from RAM. Best real-world map of the FS↔gamecard path. **GPL-3.0 — same license as this repo**, so its patterns can be reused with attribution | GPL-3.0 |
| [MissionControl](https://github.com/ndeadly/MissionControl) | Mature HD-rumble **decoding** for third-party controllers — reference for turning `HidVibrationValue` into a real motor waveform (milestone 4) | — |

## 4. Verified API facts (so we don't repeat early-doc mistakes)

- **`hid` vibration command IDs** (from switchbrew / libstratosphere, **corrected**
  from the early spec's wrong 153/154):
  | Command | ID |
  | :--- | :--- |
  | `GetVibrationDeviceInfo` | 200 |
  | `SendVibrationValue` | **201** |
  | `GetActualVibrationValue` | 202 |
  | `CreateActiveVibrationDeviceList` | 203 |
  | `SendVibrationValues` | **206** |
  | `SendVibrationValueInBool` | 212 |
- **libnx ≥ 4.0.0** HID refactor: vibration handles are **structs**
  (`HidVibrationDeviceHandle`), not bare values.
- **`HidVibrationValue`** fields: `amp_low`, `freq_low`, `amp_high`, `freq_high`
  (independent amplitude + frequency for a low and a high band).
- Handles are obtained via `hidInitializeVibrationDevices(handles, count,
  HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld)` (or `_No1` etc. for paired
  controllers).

## 5. Hardware-side toolchains (for later milestones)

| Target | Toolchain |
| :--- | :--- |
| ESP32-S3 / -C3 MCU | ESP-IDF (https://docs.espressif.com/projects/esp-idf/) — flashing over USB-CDC / DFU |
| IGLOO2 FPGA (if the slot path is ever revisited) | Microchip **Libero SoC** |
| iCE40 FPGA (open alternative) | yosys + nextpnr + IceStorm (https://github.com/YosysHQ) |
