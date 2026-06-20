# Research notes & link archive

**Author:** Dimasick-git
**License:** GPL-3.0
**Last updated:** 2026-06-13

A running knowledge base for the Switch-lite-Rumble project. This research focuses on hacking the cartridge system to turn the game card slot into a high-speed rumble interface.

---

## 1. The Core Strategy: Slot Hijacking

To make a rumble cartridge work, we must defeat the **Lotus3 ASIC** security and repurpose the physical pins.

### 1.1 Bypassing Lotus3 Power Gating
The most critical hurdle is that Lotus3 cuts power to the slot if authentication fails. 
- **The Target:** `nn::fs::detail::IDeviceOperator::GetGameCardHandle`.
- **The Hack:** Patching this function in the `fs` service (via Atmosphere IPS patches) to return a success handle even if the card is "invalid". This forces the PMIC to keep the 3.1V and 1.8V rails active.
- **Related:** Atmosphere's `nogc` patch already manipulates this service; we need a more surgical version that keeps power but stops the official protocol.

### 1.2 Repurposing DAT0-DAT7
Once the official gamecard driver is "blinded" by our patch, the 8-bit data bus (DAT0-DAT7) is free.
- **Mode:** We can drive these lines as a custom parallel bus or bit-bang them as UART/SPI.
- **Speed:** The physical lines support at least 25 MHz (the stock CLK speed).
- **Architecture:** The sysmodule captures `hid` rumble data and writes it to the `fs` service's gamecard registers, which our patch redirects to the physical DAT pins.

---

## 2. Primary Technical References

| Source | What you get | Link |
| :--- | :--- | :--- |
| **switchbrew: Gamecard** | 17-pin pinout, 1.8V/3.1V rails, 8-bit bus timing. | https://switchbrew.org/wiki/Gamecard |
| **switchbrew: Lotus3** | ASIC register maps, MMC command set (60-63). | https://switchbrew.org/wiki/Lotus3 |
| **RetroReversing: Leak** | Internal schematics of the Lotus3 reader. | https://www.retroreversing.com/switch-game-card-data-sheets |

---

## 3. Hardware Reference: MIG Switch Teardown

To understand how to fit electronics in a cart, we analyzed the MIG Switch:
- **FPGA:** Microsemi **IGLOO2 M2GL010**. Handles the timing-critical Lotus3 protocol.
- **MCU:** **ESP32-S3**. Manages the SD card and FPGA bitstream.
- **Lesson:** A 0.8mm PCB is required. For our rumble project, we can skip the SD card and use a smaller FPGA (iCE40 UltraLite) to save space for the **Taptic Engine**.

---

## 4. Software: The HID MITM Tap

The software side is already proven:
- **Service:** `hid`.
- **Commands:** `201` (SendVibrationValue), `206` (SendVibrationValues).
- **Implementation:** Our [`rumble-logger-mitm`](./software/rumble-logger-mitm/) proves we can intercept these values without affecting game performance.

---

## 5. Open Leads

1.  **Exact IPS Patch:** Need the hex offsets for `GetGameCardHandle` on the latest HOS versions.
2.  **DAT Bus Stability:** Testing the maximum reliable bit-rate for bit-banging the DAT lines without the official Lotus3 firmware.
3.  **Power Draw:** Confirming the 3.1V rail's actual current limit on the Lite before it triggers a system shutdown.

---

## 6. Capture mechanisms & hardware notes (further research)

### 6.1 A second way to capture rumble (besides MITMing 201/206)
MissionControl shows another route: it intercepts the **`WriteHidData`** IPC and uses
**hid report-event redirection** — external software can register to either *consume*
redirected hid events or *forward* them back to the system. So besides MITMing
`SendVibrationValue/Values`, the vibration stream can be tapped via hid report
redirection. Useful as a cross-check / alternative path for the logger.
- Sources: [MissionControl](https://github.com/ndeadly/MissionControl), [GameBrew: MissionControl](https://www.gamebrew.org/wiki/MissionControl_Switch).

### 6.2 The Lite's game-card slot reader is a replaceable module
On the Lite (HDH-001) the **game-card reader is a separate modular PCB** with a flex
cable, sold as a **plug-and-play, no-solder** replacement part (Deal4GO, YWLRONG,
Zahara, etc.). Practical upshot: a cheap replacement slot board can be bought to probe
pins / prototype against without risking the console.
- Example part listings: search "Switch Lite HDH-001 game card reader slot".

### 6.3 Relevant homebrew-vibration thread
- **GBAtemp — "Joycon vibration support in homebrew (can we do that yet?)"**:
  https://gbatemp.net/threads/joycon-vibration-support-in-homebrew-can-we-do-that-yet.523455/
- **sys-con rumble discussion** (virtual-controller vibration limit): https://github.com/cathery/sys-con/discussions/1

### 6.4 Status of the slot-power question (unchanged, hardware-gated)
No public project powers an external accessory from the slot. The open hardware checks
remain: does enabling the GC rail (GCA→3.1 V) reach the physical slot pins, and what
current the rail tolerates before the console browns out. Both are multimeter tests.

---
*Maintained by Dimasick-git.*
