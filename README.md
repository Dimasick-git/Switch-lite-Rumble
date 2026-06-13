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

## What's in this repo

| File | What it covers |
| :--- | :--- |
| [`README.md`](./README.md) | Project overview + the full technical specification (interception, bypass, power, data channel, feasibility) |
| [`docs/`](./docs/) | Working design docs + the **decision log** ([`DESIGN-NOTES.md`](./docs/DESIGN-NOTES.md)): the architectures debated, what problem the project actually solves, the HD-rumble encoding ([`RUMBLE-ENCODING.md`](./docs/RUMBLE-ENCODING.md)) and haptic actuator systems ([`HAPTICS.md`](./docs/HAPTICS.md)) |
| [`software/`](./software/) | The sysmodule that taps rumble in `hid` — dependency/toolchain list ([`DEPENDENCIES.md`](./software/DEPENDENCIES.md)) and a milestone-1 PoC skeleton ([`rumble-tap-sysmodule/`](./software/rumble-tap-sysmodule/)) |
| [`CHIPS.md`](./CHIPS.md) | Hardware reference: game card physical envelope, 17-pin pinout, Lotus3, and concrete candidate chips (FPGA, MCU, haptic driver, actuator, power buffer) that fit the size constraint — with sources |
| [`RESEARCH.md`](./RESEARCH.md) | Annotated link archive + findings: Lotus3 deep dive, every relevant GBAtemp thread, MIG prior art, the HID/vibration software path, dumping tools, crypto/keys, and what it all means for this project |
| [`hardware/`](./hardware/) | PCB design files and references. Currently the **MIG Dumper & Flashcart** KiCad projects, schematics, boardviews and BOMs (prior art, original by [sabogalc](https://github.com/sabogalc/MIG-Flash-PCBs), WTFPL) — the closest existing slot-fit, Lotus3-speaking boards |
| [`LICENSE`](./LICENSE) | GPL-3.0 full text |

## Information collected so far

- **Physical envelope** of the Switch game card (~21 × 31 × 3 mm) and what that
  means for component packages — see [`CHIPS.md`](./CHIPS.md).
- **Full 17-pin slot pinout**, logic levels (1.8 V), and the 3.1 V / 1.8 V power
  budget, sourced from switchbrew.
- **Lotus3 ASIC** behaviour: power sequencing, header read, challenge-response, and
  the power-gating-on-failure that we have to defeat.
- **MIG Switch architecture** as prior art: a low-cost iCE40 FPGA emulating the
  gamecard LSI, managed by an ESP32 — confirming the approach is viable.
- **Concrete part candidates** sized to the envelope: Lattice iCE40 UltraLite
  (1.4 × 1.4 mm WLCSP), ESP32-C3, TI DRV2605L haptic driver (1.5 × 1.5 mm), coin
  LRA, and a supercapacitor power buffer.
- **The HID interception path** (MITM on the `hid` service via Atmosphere) for
  capturing the games' vibration values — see the spec below.
- **Reference PCB designs** — the full MIG Dumper & Flashcart KiCad projects,
  schematics, boardviews and BOMs, imported and cleaned up in
  [`hardware/`](./hardware/). The Flashcart confirms a real-world slot-fit form
  factor: a 0.8 mm ENIG board with 0201 passives speaking the Lotus3 protocol.
- **Lotus3 internals** — the gamecard ASIC is a Cortex-M3 with 4 KB ROM / 42 KB
  SRAM, a hardware RNG, and an RSA-OAEP + AES-128 challenge-response auth chain;
  it bridges the Tegra (eMMC/SDMMC2 vendor commands) to the card. Full breakdown
  and every source/discussion thread is collected in [`RESEARCH.md`](./RESEARCH.md).

## Status

**Research / pre-prototype.** No hardware has been built yet.

Whichever transport we use, **step one is the same**: a custom Atmosphère sysmodule
that captures the live rumble values a game emits in `hid`. That capture work is
underway in [`software/`](./software/) and is independent of how the signal finally
reaches the motor.

Two transports are on the table (full discussion in
[`docs/DESIGN-NOTES.md`](./docs/DESIGN-NOTES.md)):

1. **In-slot cartridge — the end goal.** A cartridge that lives in the game-card
   slot and carries the actuator: the cleanest "plug it in and it just works" form
   factor. The hard part is getting the slot to power and talk to a non-original
   device, which is gated by the **Lotus3** controller.
2. **External / USB-C or Bluetooth — the fallback.** The same sysmodule forwards the
   captured values to an external actuator. Not as elegant, but it sidesteps the
   slot entirely and is testable today.

> **Honest scope note.** Getting the slot to keep power on for an unauthenticated
> device, and forging the Lotus3 authentication, is the same mechanism that defeats
> the gamecard copy-protection (it's what flashcarts do). This repo documents how
> Lotus3 works (all from public sources) and pursues the power/data/actuator
> sub-problems, but it does **not** ship a working authentication/signature bypass.
> The USB-C/BT fallback exists precisely so the project has a complete, clean path
> that needs none of that.

The biggest open unknowns are where exactly to tap vibration in `hid`, whether the
handheld npad even receives rumble values on a Lite, channel latency, and the
HD-rumble→actuator mapping — see the open questions in
[`docs/DESIGN-NOTES.md`](./docs/DESIGN-NOTES.md#9-открытые-вопросы-что-реверсить-дальше)
and [`CHIPS.md`](./CHIPS.md).

**Discussion:** https://gbatemp.net/threads/nintendo-switch-lite-rumble.682407/

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
| `hid` | `SendVibrationValue` | 153 | Send a single vibration value |
| `hid` | `SendVibrationValues` | 154 | Batch send values for multiple motors |

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

## License

GPL-3.0. Full text in the `LICENSE` file.
