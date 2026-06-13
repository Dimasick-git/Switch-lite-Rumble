# Switch-lite-Rumble

**Author:** Dimasick-git  
**License:** GPL-3.0  

> I need all the information about Mig-switch and more information…  
> **I can do reverse engineering, but I need help.**  
> If you understand Atmosphere, cartridge protocols, FPGA, or power management – join.

---

## What this project is

An open research effort to add **tactile feedback (Rumble)** to the **Nintendo
Switch Lite** — which has no built-in HD Rumble — by building a module that plugs
into the **game card slot**. The slot is the only spare port on the Lite, so the
challenge is getting both **power** and **vibration data** out of it past Nintendo's
protections.

This repo is the knowledge base for that effort: the protocol research, the chip
selection, the power math, and the open problems that still need solving.

## Two ways to deliver the signal

Whichever path we take, **step one is the same**: a custom Atmosphère sysmodule that
captures the live rumble values a game emits in `hid` (the Lite computes them
normally — it just has no motor). From there:

1. **In-slot cartridge — the end goal.** A cartridge in the game-card slot carries
   the actuator and is powered from the slot. Cleanest "plug it in and it works"
   form factor; the hard part is slot power/data, policed by the **Lotus3** chip.
2. **External USB-C / Bluetooth — the fallback.** The sysmodule forwards the captured
   values to an external actuator. Less elegant, but sidesteps the slot entirely and
   is testable today.

> **Scope.** We pursue the **power, data-transport and actuator** sub-problems and
> document Lotus3 from public sources. We do **not** ship a Lotus3
> authentication/signature bypass — that's the copy-protection mechanism flashcarts
> defeat, and the project doesn't need it (the USB-C/BT path is a complete, clean
> alternative; powering a rail is not the same as authenticating a cartridge).

## Goals & progress

| Goal | Status | Notes |
| :--- | :--- | :--- |
| Confirm the rumble signal exists / is reachable on a Lite | ✅ **Done** | Paired Joy-Cons vibrate from a game; the `hid` pipeline is live |
| Software: capture what a game sends (hid MITM logger) | ✅ **Built** | [`rumble-logger-mitm`](./software/rumble-logger-mitm/) compiles green in CI, ships an SD-ready package; hardware test pending |
| Software: prove a sysmodule loads + drives the vibration API | ✅ **Built** | [`rumble-tap-sysmodule`](./software/rumble-tap-sysmodule/) (libnx), logs to SD |
| Power the game-card rail at runtime | ✅ **Done (code)** | [`gc-power`](./software/gc-power/): `GCA`→**3.1 V** demonstrated via overlay |
| Pick the actuator + driver | ✅ **Decided** | iPhone 12 **Taptic Engine** + **DRV2605L** — see [`docs/HAPTICS.md`](./docs/HAPTICS.md) |
| Build automation | ✅ **Done** | GitHub Actions builds both sysmodules → SD-ready artifacts |
| Does enabling GCA power the **physical slot pins** (vs only Lotus3)? | ❔ **Open** | Needs a multimeter on hardware |
| Does the Lite's **handheld npad** receive rumble (vs only paired pads)? | ❔ **Open** | Decides whether we need a virtual controller — run a game + read the log |
| HD-rumble → single-actuator mapping | 🔄 **Planned** | Math + lookup tables in [`docs/RUMBLE-ENCODING.md`](./docs/RUMBLE-ENCODING.md) |
| Final transport (in-slot vs USB-C/BT) | 🔄 **Open** | Pick after measuring slot power + latency |
| Physical fit / cartridge shell | 🔄 **In progress** | Shell can grow upward (only the bottom is fixed); exact measurements being taken |
| Hardware prototype | ⬜ **Not started** | — |

## What we've found (documented)

- **The signal is live and trivially sendable.** The Lite's `hid` computes vibration
  as usual; only the built-in actuator is missing. Sending it from homebrew is one
  call — confirmed on a paired Joy-Con.
- **Game-card power is controllable at runtime.** The **GCA** rail (MAX77620 LDO3)
  was driven to **3100 mV** = the cartridge 3.1 V VCC, and **GCC** (LDO5) is the
  ~1.8 V I/O rail. Code: [`software/gc-power/`](./software/gc-power/). *Open:* does
  this reach the slot pins or stop at Lotus3.
- **`hid` vibration command IDs (verified):** `SendVibrationValue` = **201**,
  `SendVibrationValues` = **206** (an early note's 153/154 were wrong).
- **Lotus3 internals:** the gamecard ASIC is a **Cortex-M3** (4 KB ROM / 42 KB SRAM)
  with a hardware RNG and an **RSA-OAEP + AES-128 challenge-response** auth chain,
  bridging the Tegra (eMMC/SDMMC2 vendor commands) to the card. Full breakdown +
  every source in [`RESEARCH.md`](./RESEARCH.md).
- **Slot physical/electrical facts:** full 17-pin pinout, 1.8 V logic / 3.1 V core,
  25 MHz bus, the ~21 × 31 × 3 mm envelope, and the >300 mA actuator-peak vs slot
  budget problem — [`CHIPS.md`](./CHIPS.md).
- **MIG prior art (corrected):** a teardown shows MIG's FPGA is a **Microsemi
  IGLOO2 M2GL010** with an **ESP32-S3** helper (not an iCE40). PCB references in
  [`hardware/`](./hardware/).
- **Actuator & encoding:** HD rumble is two amplitude/frequency bands (~41–1253 Hz);
  collapse to one resonant actuator (Joy-Con LRAs sit at 180–250 Hz). MissionControl
  already does this decode. See [`docs/HAPTICS.md`](./docs/HAPTICS.md) and
  [`docs/RUMBLE-ENCODING.md`](./docs/RUMBLE-ENCODING.md).

## What's in this repo

| Path | What it covers |
| :--- | :--- |
| [`software/`](./software/) | The code. Two CI-built sysmodules — [`rumble-logger-mitm`](./software/rumble-logger-mitm/) (hid MITM vibration logger) and [`rumble-tap-sysmodule`](./software/rumble-tap-sysmodule/) (libnx PoC) — plus [`gc-power`](./software/gc-power/) (game-card rail control) and [`DEPENDENCIES.md`](./software/DEPENDENCIES.md) |
| [`docs/`](./docs/) | Design decisions ([`DESIGN-NOTES.md`](./docs/DESIGN-NOTES.md)), HD-rumble encoding ([`RUMBLE-ENCODING.md`](./docs/RUMBLE-ENCODING.md)), haptics ([`HAPTICS.md`](./docs/HAPTICS.md)), and the two architecture write-ups |
| [`CHIPS.md`](./CHIPS.md) | Hardware reference: card envelope, 17-pin pinout, Lotus3, and sized-to-fit chip candidates |
| [`RESEARCH.md`](./RESEARCH.md) | Annotated source/thread archive: Lotus3 deep dive, GBAtemp threads, MIG, the software path, tools, crypto |
| [`hardware/`](./hardware/) | MIG Dumper & Flashcart PCB reference files (prior art, by [sabogalc](https://github.com/sabogalc/MIG-Flash-PCBs)) |
| [`LICENSE`](./LICENSE) | GPL-3.0 |

## Get the build

GitHub Actions builds everything on each push. Grab the latest **green**
[build run](https://github.com/Dimasick-git/Switch-lite-Rumble/actions), download the
**`Switch-lite-Rumble-SD`** artifact, extract it, and copy the `atmosphere/` folder
to your SD card root. Reboot, launch a game with rumble, and read the logs
(`sdmc:/rumble-logger.log`). Per-module setup is in each folder's README.

**Discussion / help:** https://gbatemp.net/threads/nintendo-switch-lite-rumble.682407/

---

# Technical Specification: Rumble mod for Switch Lite through the game card slot

This document describes the deep technical implementation of a tactile feedback (Rumble) module connected via the game card slot. The main focus is on methods to bypass system limitations to provide power and data transfer.

---

## 1. Interception system architecture (Software)

To obtain vibration data, it is necessary to interfere with the **HID (Human Interface Device)** system service. On Switch Lite consoles, vibration commands are generated by games but ignored by the built-in controller driver.

### Interception point: IPC MITM
A system module (sysmodule) based on Atmosphere must be implemented to act as a man-in-the-middle (MITM) for the `hid` service.

| Service | Command | ID | Description |
| :--- | :--- | :--- | :--- |
| `hid` | `SendVibrationValue` | 201 | Send a single vibration value |
| `hid` | `SendVibrationValues` | 206 | Batch send values for multiple motors |

> These are the **verified** `hid` command IDs (switchbrew / libstratosphere). An
> early draft of this spec listed 153/154 — those were wrong. Related: 200
> `GetVibrationDeviceInfo`, 202 `GetActualVibrationValue`,
> 203 `CreateActiveVibrationDeviceList`, 212 `SendVibrationValueInBool`.

**Mechanism:**
1. The sysmodule registers an MITM handler for the `hid` port.
2. When the game module calls `SendVibrationValue`, the sysmodule intercepts the `VibrationValue` structure (amplitude and frequency for low and high frequencies).
3. The data is packed into a compact protocol and sent to the end device.

---

## 2. Bypassing game slot protection (The Bypass)

The card slot is controlled by a dedicated **Lotus3 ASIC** chip. This is the main barrier that blocks power and data for unauthorised devices.

### Lotus3 blocking mechanism
When a device is inserted (contact **CD#** shorted to ground):
1. The system loads firmware into Lotus3.
2. Initialisation power is applied (**VCC 1.8V** and **3.1V**).
3. Lotus3 tries to read the **Card Header** and perform the **Challenge-Response** procedure (authentication).
4. If authentication fails, Lotus3 cuts power lines and puts the bus into an error state.

### Bypass methods to obtain power and data

#### Option A: Protocol emulation (Hardware Emulation)
Creating a device based on a fast microcontroller or FPGA capable of responding to Lotus3 commands.
- **Complexity:** Extreme. The protocol uses CRC-32 and protected mode (AES-128-CTR).
- **Advantage:** Fully autonomous, does not require deep system patches.

#### Option B: Software patching of the FS service
Modifying the `fs` (Filesystem) service that controls `IDeviceOperator`.
- **Essence:** Patching checks in the function `nn::fs::detail::IDeviceOperator::GetGameCardHandle`.
- **Goal:** Force the system to ignore the Lotus3 error status and not issue the **Power Gating** command.
- **Risk:** Lotus3 may have a hardware timeout for power application without successful initialisation.

---

## 3. Power supply and limitations

The card slot has strict current limits, not designed for inductive loads (motors).

| Parameter | Value | Note |
| :--- | :--- | :--- |
| **VCC 3.1V** | ~50-100 mA | Main power for the cartridge chip |
| **VCC 1.8V** | ~20-50 mA | I/O logic power |
| **Taptic Engine peak** | >300 mA | Requires buffer capacitor or battery |

**Recommendation:** To prevent console resets due to voltage drops, the cartridge device must have a **supercapacitor** or a miniature Li-Po battery that charges from the slot during idle moments.

---

## 4. Data transmission channel (Rumble Data)

Power is taken from the slot. Transmission of vibration data via the **DAT0-DAT7** lines requires implementing a proprietary protocol.

1. **Path through the cartridge bus:** The sysmodule must send data to `fs`, which forwards it to Lotus3, which then drives the DAT lines. This requires writing a custom driver for Lotus3.
2. **Using slot data lines as UART/SPI:** Possible only when the stock Gamecard driver in the kernel/FS is fully deactivated.

---

## 5. Feasibility conclusion

Implementing "through the slot" with authentication bypass is a **Hardcore Reverse Engineering** level project.

1. **Power:** Achievable via software patches to the `fs` service, forcing the system to keep power on despite errors.
2. **Data:** Requires emulation of the basic cartridge protocol via the DAT lines.
3. **Safety:** There is a risk of damaging the Lotus3 power controller if the vibration motor exceeds current limits.

**Verdict:** The project is technically possible provided a custom patch for `fs-srv` and a hardware power buffer on the cartridge side are used.

---

## Looking for help

I, Dimasick-git, can do reverse engineering, but I need help:

- Atmosphere programmer (MITM module for `hid`)
- Specialist in patching `fs` under Atmosphere
- Circuit designer / FPGA developer
- Tester with custom firmware

**How to contact:** create an Issue in this repository with the tag `[help]`, or
reply on the project's discussion thread:

- **GBAtemp thread:** https://gbatemp.net/threads/nintendo-switch-lite-rumble.682407/

---

## Acknowledgements

- **Cooler3D** — runtime game-card power-rail control (from the 4IFIR project),
  which powers the cartridge slot at 3.1 V. See [`software/gc-power/`](./software/gc-power/).
  Only the game-card power domains were used; the rest of the author's work is not
  part of this repo.
- **sabogalc** — MIG Dumper/Flashcart PCB reference files ([`hardware/`](./hardware/)).

---

## License

GPL-3.0. Full text in the `LICENSE` file.
