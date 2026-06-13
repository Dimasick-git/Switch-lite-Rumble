# Switch-lite-Rumble

**Author:** Dimasick-git  
**License:** GPL-3.0  

> **Project Goal:** Hack the cartridge system to enable real HD Rumble on the Switch Lite via an in-slot module. 
> I can do reverse engineering, and I'm pushing for a hardware solution that fits inside a game card.
> If you understand Atmosphere, Lotus3 protocol, FPGA, or FS-srv patching – you're in the right place.

---

## What this project is

An open research effort to add **tactile feedback (Rumble)** to the **Nintendo Switch Lite** by building a module that plugs into the **game card slot**. Since the Lite has no built-in motors, we are hijacking the game card interface to provide both **power** and **vibration data** to a custom actuator (Taptic Engine) living inside a cartridge shell.

This is not just a software mod; it's a hardware hack to bypass Nintendo's **Lotus3** protections and repurpose the slot pins for our own data protocol.

## The "In-Slot" Hack Strategy

The main challenge is that the **Lotus3 ASIC** cuts power and data if a cartridge isn't authenticated. Our goal is to defeat this:

1.  **Software Bypass (FS-srv Patch):** Patching `nn::fs::detail::IDeviceOperator::GetGameCardHandle` in the Atmosphere `fs` service to ignore authentication errors and prevent **Power Gating**. This keeps the 3.1V and 1.8V rails alive.
2.  **Data Transport (DAT-line Hijack):** Once the official gamecard driver is suppressed, we repurpose the **DAT0-DAT7** lines as a custom high-speed bus (UART/SPI) to send captured vibration values to the cartridge.
3.  **Hardware Emulation:** A low-power FPGA (like the **iCE40 UltraLite** or the **IGLOO2** found in MIG) handles the physical bus communication, while a **DRV2605L** drives an **iPhone 12 Taptic Engine**.

## Goals & Progress

| Goal | Status | Notes |
| :--- | :--- | :--- |
| Confirm rumble signal exists on Lite | ✅ **Done** | `hid` pipeline is live; external pads vibrate |
| Capture game vibration (HID MITM) | ✅ **Built** | [`rumble-logger-mitm`](./software/rumble-logger-mitm/) works in CI |
| Control game-card power rails | ✅ **Done** | [`gc-power`](./software/gc-power/) drives PMIC LDOs to 3.1V/1.8V |
| Actuator Selection | ✅ **Done** | **Taptic Engine** + **DRV2605L** (best feel/size) |
| **Bypass Lotus3 Power Gating** | 🔄 **In Progress** | Researching FS-srv patch offsets |
| **DAT-line custom protocol** | 🔄 **Planned** | Need to disable stock driver and bit-bang DAT lines |
| Physical Cartridge Shell | 🔄 **In Progress** | Designing for Taptic Engine fit (extending upward) |
| Hardware Prototype | ⬜ **Not started** | Building the first "Hacker Cart" |

## Technical Findings

-   **Signal is ready:** The Lite computes vibration; we just need to catch it. `SendVibrationValue` (201) and `SendVibrationValues` (206) are the targets.
-   **Power is controllable:** We can drive the **GCA** (3.1V) and **GCC** (1.8V) rails via I2C to the MAX77620 PMIC.
-   **Lotus3 Internals:** It's a Cortex-M3 based security wall. We don't fight the crypto; we patch the console to ignore it.
-   **MIG Prior Art:** Confirmed MIG uses **Microsemi IGLOO2 M2GL010** + **ESP32-S3**. We can use a smaller FPGA for our narrower scope.
-   **Haptics:** iPhone Taptic Engines are the gold standard for this mod. They fit if we grow the cart shell slightly.

## Repository Structure

-   [`software/`](./software/): HID MITM logger, PoC sysmodules, and PMIC power control code.
-   [`docs/`](./docs/): Technical specs, haptics research, and design decisions.
-   [`CHIPS.md`](./CHIPS.md): Hardware reference (pinouts, chip candidates, power math).
-   [`RESEARCH.md`](./RESEARCH.md): Deep dive into Lotus3, protocol RE, and community threads.
-   [`hardware/`](./hardware/): Reference files from existing flashcarts.

## How to Help

I'm looking for:
-   **Atmosphere/FS experts** to help with the `fs-srv` power-gate bypass.
-   **FPGA hackers** to write the custom DAT-bus receiver.
-   **PCB designers** who can fit a Taptic Engine + FPGA into a 2mm stack.

**GBAtemp Discussion:** [Nintendo Switch Lite Rumble](https://gbatemp.net/threads/nintendo-switch-lite-rumble.682407/)

---
*Project by Dimasick-git. No mention of AI allowed.*
