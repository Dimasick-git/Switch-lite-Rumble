# Chips & Hardware Reference

**Author:** Dimasick-git
**License:** GPL-3.0

This document collects everything currently known/researched about the components
that could realistically fit inside a Switch Lite game-card-slot Rumble module.
The hard constraint for *any* design is the physical envelope of a Nintendo Switch
game card, so every chip below is judged primarily on whether it can fit that
footprint.

---

## 1. The hard constraint: game card envelope

A standard Nintendo Switch game card is tiny. Anything we build must live inside
(or just behind) this envelope.

| Property | Value | Note |
| :--- | :--- | :--- |
| Width | ~21 mm | The narrow dimension |
| Height | ~31 mm | The long dimension (pins on the short edge) |
| Thickness | ~3 mm (≈2.1 mm at the casing) | This is the killer constraint — components must be very low-profile |
| Contacts | 17 pins | 1.8 V logic, single 3.1 V core rail |
| Data bus | 8-bit (DAT0–7), proprietary SPI-like | Captured on rising edge of 25 MHz CLK |
| Capacity tiers (real cards) | 1 / 2 / 4 / 8 / 16 / 32 GB | Not relevant to us, listed for reference |

**Practical takeaway:** usable internal PCB area is roughly **20 mm × 30 mm**, and
total stack height (PCB + components + solder) has to stay under ~2 mm. That rules
out anything in a tall QFP/SOIC body and pushes us hard toward **WLCSP / BGA / QFN**
chip-scale packages.

---

## 2. Game card pinout (the 17 contacts)

Source: switchbrew Gamecard documentation. All IO is **1.8 V logic** (HIGH = 1.8 V,
LOW = 0 V) except the 3.1 V core rail.

| Pin | Name | Direction | Voltage | Function |
| :--- | :--- | :--- | :--- | :--- |
| 1 | GND | — | 0 V | Ground |
| 2 | CD# | Output (card → host) | 1.8 V | Card Detect (shorted to GND when inserted) |
| 3 | CLK | Input | 1.8 V | 25 MHz clock |
| 4 | RCLK | Output | 1.8 V | Return clock (delayed echo) |
| 5 | CS# | Input | 1.8 V | Chip Select |
| 6, 7 | DAT0, DAT1 | I/O | 1.8 V | Data bus |
| 8 | VCC 3.1 V | Input | 3.1 V | Core power |
| 9, 10 | DAT2, DAT3 | I/O | 1.8 V | Data bus |
| 11 | VCC 1.8 V | Input | 1.8 V | I/O power |
| 12, 13, 14, 15 | DAT4–DAT7 | I/O | 1.8 V | Data bus |
| 16 | GND | — | 0 V | Ground |
| 17 | RST# | Input | 1.8 V | Reset |

**Power budget (this is what limits the motor):**

| Rail | Approx. current available | Use |
| :--- | :--- | :--- |
| VCC 3.1 V | ~50–100 mA | Core power for the cartridge controller |
| VCC 1.8 V | ~20–50 mA | I/O logic |
| Taptic / LRA peak demand | **>300 mA** | Far above what the slot gives — needs a buffer |

Because the motor's peak current is several times the slot budget, a
**supercapacitor or tiny Li-Po buffer** is mandatory; it charges during idle and
delivers the peak pulse to the actuator.

---

## 3. The chip we have to beat: Lotus3

The slot is policed by Nintendo's **Lotus3 ASIC**. On insertion it powers up the
card (1.8 V then 3.1 V), reads the card header, and runs a **challenge-response**
authentication. On failure it cuts the rails and parks the bus in an error state.
Cartridges are manufactured by three vendors (MegaChips, Lapis, LSI Logic), each
with its own card ID. Commands are 16 bytes + 4-byte CRC-32; protected mode adds
AES-128-CTR. Beating or bypassing Lotus3 is the central reverse-engineering problem
(see the README for the two bypass strategies).

---

## 4. Candidate chips by role

### 4.1 Gamecard / bus emulation (the "pretend to be a card" job)

This block must respond to Lotus3's clocked 8-bit protocol in real time. An FPGA
or CPLD is the realistic choice because the timing is tight and the protocol is
custom. This is also exactly the approach the **MIG Switch** flashcart uses: an
FPGA emulates the gamecard LSI, managed by a small MCU. (Chip markings were
scratched and firmware encrypted to slow reverse engineering.)

> **Confirmed part (team teardown):** a MIG Switch unit was opened and its main FPGA
> is a **Microsemi/Microchip IGLOO2 M2GL010** (the larger sibling **M2GL025** also
> exists), with an **ESP32-S3** as the helper MCU (microSD, firmware update, loading
> the FPGA bitstream at boot). This **corrects** the earlier assumption that MIG uses
> an iCE40 — see [`RESEARCH.md`](./RESEARCH.md). The iCE40 options below remain valid
> *candidates* for our own from-scratch board (smaller, fully open toolchain); IGLOO2
> is listed because it's the proven-working reference part.

| Chip | Package / size | Why it fits | Watch-outs |
| :--- | :--- | :--- | :--- |
| **Microsemi IGLOO2 M2GL010** (MIG's actual FPGA) | smallest is TQ144 (0.5 mm pitch) or FCS BGA (0.5 mm pitch); ~6,060 logic elements | **Proven** to work as a gamecard emulator in MIG; flash-based (no config chip needed) | Larger packages than iCE40; proprietary Libero toolchain; markings scrubbed on units |
| **Microsemi IGLOO2 M2GL025** | BGA/TQ, ~12,084 logic elements | More headroom; same proven family | Bigger die/package; same toolchain caveat |
| **Lattice iCE40 UltraLite (UL1K)** | 16-ball WLCSP, **1.4 × 1.4 mm**, 0.35 mm pitch | Among the smallest FPGAs in existence; trivially fits the envelope; ultra-low power; open toolchain (IceStorm/yosys/nextpnr) | Limited LUTs — emulation logic must be lean; not yet proven on this protocol |
| **Lattice iCE40 UltraPlus (UP5K)** | QFN-48 7 × 7 mm, or WLCSP ~2.1 × 2.5 mm | More logic + RAM headroom for the protocol state machine; open toolchain | QFN body is large for the slot — prefer the CSP |

**Recommendation:** start prototyping on a UP5K dev board (room to breathe), then
target the **iCE40 UltraLite WLCSP** for the production-size card. Native 1.8 V I/O
banks line up perfectly with the slot's logic level.

### 4.2 Management MCU (config, USB/SD, vibration decode)

Loads the FPGA bitstream, handles the decoded vibration stream, and does general
housekeeping. The MIG Switch pairs an **ESP32-S3** with its FPGA for exactly this
(microSD, firmware update, loading the bitstream at boot). For a *rumble-only*
board, much of that MIG workload (microSD, file catalogs, USB mass storage) is
unnecessary — an open question is whether we need a helper MCU at all, or whether
the FPGA alone suffices.

| Chip | Package / size | Notes |
| :--- | :--- | :--- |
| **ESP32-S3** | QFN, ~7 × 7 mm | The exact part MIG uses; USB + BT; well-documented (DFU flashing over USB-CDC) |
| **ESP32-C3** | QFN, ~5 × 5 mm | Single RISC-V core, BLE — wireless config without extra pins; cheap and everywhere |
| **ESP32 (classic)** | QFN 5 × 5 / 6 × 6 mm | Same family; 6 mm body is tight but feasible |
| **RP2040** | QFN-56, **7 × 7 mm** | Cheap, great PIO for bit-banging the bus, dual M0+; but 7 mm is borderline for the envelope and it needs external flash |
| **STM32 (C0/G0 in WLCSP)** | WLCSP ~2–3 mm | Smallest footprint option if BLE isn't needed |

**Recommendation:** **ESP32-C3** for the balance of size, wireless config, and
direct lineage to the MIG design. If absolutely every mm² counts and no wireless is
needed, an STM32 in WLCSP wins on size.

### 4.3 Haptic / motor driver

Drives the actuator and offloads waveform shaping from the MCU.

| Chip | Package / size | Notes |
| :--- | :--- | :--- |
| **TI DRV2605L** | **DSBGA, 1.5 × 1.5 mm** (also VSSOP 3 × 3 mm) | Purpose-built LRA/ERM driver, closed-loop "smart-loop", I²C, built-in effect library; the DSBGA fits the envelope easily |
| **TI DRV2603 / DRV2604** | small QFN/WLCSP | Lighter-weight alternatives if the effect library isn't needed |

**Recommendation:** **DRV2605L in DSBGA** — closed-loop control keeps an LRA on
resonance, which both feels better and is more power-efficient (critical given the
slot's current limit).

### 4.4 Actuator (the thing that actually buzzes)

- **iPhone 12 Taptic Engine** (team's preferred candidate) — a high-quality linear
  actuator; strong but controllable, capable of HD-rumble-like feel. Thin enough
  that it has been fitted between the board and the shell of a Switch Lite; widely
  available (e.g. AliExpress) as a repair part. Best feel-for-size of the options.
- **LRA (Linear Resonant Actuator)** — flat coin types ~8–10 mm diameter, low
  profile, sharp/precise feel, resonance-tuned (pairs with DRV2605L). Good generic
  fit for the thin envelope.
- **Small ERM** — cheaper, "rumblier", but bulkier and slower to spin up; harder to
  fit in <3 mm.

**Recommendation:** target the **iPhone 12 Taptic Engine** for the real feel the
project wants; prototype/fallback with a **coin LRA** driven on-resonance by the
DRV2605L. Note the cartridge shell can be **extended upward** (only the bottom must
stay stock), so actuator height is less constrained than the 2.1 mm card thickness
implies.

### 4.5 Power buffer (non-negotiable)

The slot can't source the >300 mA peak, so store energy locally.

| Option | Notes |
| :--- | :--- |
| **Supercapacitor (e.g. 0.1–1 F, low-profile)** | Simple, no charge-management IC strictly required, handles short peaks; watch height |
| **Miniature Li-Po** | More energy, but needs a charge controller and is thicker/safety-sensitive |
| **Bulk ceramic caps (10–47 µF stacked)** | Cheapest, smooths short transients, but won't cover sustained rumble |

**Recommendation:** a low-profile **supercapacitor** trickle-charged from VCC 3.1 V
during idle, feeding the DRV2605L's motor rail through a small boost/LDO as needed.

---

## 5. Reference block diagram

```
[Switch Lite slot, 17 pins]
        │ 1.8V logic / 3.1V core
        ▼
   ┌──────────────┐   SPI    ┌────────────┐
   │  iCE40 FPGA  │◄────────►│  ESP32-C3  │
   │ (bus / Lotus │          │  (mgmt +   │
   │  emulation)  │          │  vib decode)│
   └──────┬───────┘          └─────┬──────┘
          │ decoded amplitude/freq │ I²C
          ▼                        ▼
                ┌──────────────┐
                │   DRV2605L   │  (haptic driver, closed loop)
                └──────┬───────┘
                       ▼
                  [ coin LRA ]
                       ▲
          ┌────────────┴────────────┐
          │  Supercap power buffer  │ ◄── trickle-charged from VCC 3.1V
          └─────────────────────────┘
```

---

## 6. Open questions / help wanted

These are the unknowns that most need a contributor with the right gear:

1. **Lotus3 challenge-response** — exact authentication sequence and whether a
   software `fs-srv` patch can suppress the power-gating-on-failure behaviour.
2. **Hardware timeout** — does Lotus3 cut power after a fixed time if init never
   succeeds? This decides whether Option B (software patch) is viable at all.
3. **DAT-line repurposing** — can the 8-bit bus be driven as UART/SPI once the stock
   gamecard driver is disabled, and at what clock?
4. **Exact MIG FPGA part** — confirming the scrubbed iCE40 variant would shortcut a
   lot of the emulation work.
5. **Real current ceiling** — measured (not datasheet) current the slot tolerates
   before the console browns out / resets.

If you can help with any of these, open an Issue tagged `[help]`.

---

## Sources

- Nintendo Switch game card dimensions — [Wikipedia: Nintendo Game Card](https://en.wikipedia.org/wiki/Nintendo_Game_Card), [Nintendo tech specs](https://www.nintendo.com/us/switch/tech-specs/)
- Gamecard pinout & protocol — [switchbrew: Gamecard](https://switchbrew.org/wiki/Gamecard)
- MIG Switch internals (ESP32 + iCE40 FPGA, encrypted firmware) — [Mig Flash, Wikipedia](https://en.wikipedia.org/wiki/Mig_Flash), [GBAtemp Switch Flashcart thread](https://gbatemp.net/threads/the-switch-flashcart-thread-mig-switch-unlockswitch-etc.651852/)
- iCE40 package sizes — [Lattice iCE40 UltraLite](https://www.latticesemi.com/Products/FPGAandCPLD/iCE40Ultra), [iCE (FPGA), Wikipedia](https://en.wikipedia.org/wiki/ICE_(FPGA))
- DRV2605L haptic driver — [TI DRV2605L](https://www.ti.com/product/DRV2605L)
- MCU package sizes — [RP2040, Wikipedia](https://en.wikipedia.org/wiki/RP2040), [ESP32, Wikipedia](https://en.wikipedia.org/wiki/ESP32)
