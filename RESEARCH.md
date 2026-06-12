# Research notes & link archive

**Author:** Dimasick-git
**License:** GPL-3.0
**Last updated:** 2026-06-12

A running knowledge base for the Switch-lite-Rumble project: every useful source,
discussion thread and technical finding about the Switch game-card interface, the
Lotus3 ASIC, the cartridge protocol, existing flashcarts, and the software side of
vibration. Annotated so you don't have to re-read everything to know what's in it.

> **Disclaimer / scope.** This is reverse-engineering and homebrew research for an
> accessibility-style hardware mod (adding rumble the Switch Lite physically lacks).
> Sources below include leaked datasheets discussed publicly; they are linked for
> technical study, not redistribution. Nothing here helps with piracy — the goal is
> a *vibration* device, not a flashcart.

---

## 1. The core problem in one paragraph

The Switch game-card slot is policed by the **Lotus3 ASIC** — a self-contained
security chip that authenticates every card before the system will keep power and
data flowing. It is widely described in the community as a "fully standalone,
non-crackable ASIC." Any device in the slot must either (a) pass Lotus3's
authentication (effectively impossible without secrets), (b) be made to survive
despite failing it (software patch on the console side), or (c) impersonate the
console-facing side. This is the wall every line below circles around.

---

## 2. Primary technical references (authoritative)

| Source | What you get | Link |
| :--- | :--- | :--- |
| **switchbrew: Gamecard** | The 17-pin pinout, voltages (1.8 V logic, 3.1 V core), the proprietary 8-bit SPI-like protocol, command/CRC-32 format, manufacturer IDs | https://switchbrew.org/wiki/Gamecard |
| **switchbrew: Lotus3** | The ASIC itself: MMC_SEND_MANUFACTURER commands (60–63: WriteOperation/FinishOperation/Sleep/UpdateKey), 20 operation IDs, 0x200-byte pages, 16 registers, CardKeyArea (0x800 bytes), LAFW firmware + RSA-2048 signature + OTP anti-downgrade | https://switchbrew.org/wiki/Lotus3 |
| **switchbrew: XCI** | The gamecard image format, CardHeader, InitialData, certificate layout | https://switchbrew.org/wiki/XCI |
| **RetroReversing: Switch Game Card Leak** | Plain-English breakdown of the July 2021 "Gigaleak" datasheet.7z: Lotus3 + GC folders, chip internals, manufacturers | https://www.retroreversing.com/switch-game-card-data-sheets |

---

## 3. Lotus3 ASIC — deep dive (the security wall)

Consolidated from switchbrew + RetroReversing + the leak discussion:

- **Designer/maker:** MegaChips Corporation + Macronix International. Maker IDs seen:
  MegaChips (Macronix), Lapis, and a third vendor.
- **Internals:** an ARM **Cortex-M3** core, **4 KB ROM**, **42 KB SRAM**, and a
  **custom hardware RNG**. It is a real little computer, not a dumb gate.
- **Two interfaces:**
  1. **Console side (eMMC):** Lotus3 sits on the Tegra's **SDMMC2** bus and talks
     to FS over **vendor-specific MMC commands** (`MMC_SEND_MANUFACTURER`, values
     60–63). This is why people ask "can I just swap Lotus3 for an eMMC?" — the
     console-facing language *is* eMMC-like.
  2. **Card side:** Nintendo's proprietary **Game Memory Interface** (the 8-bit
     bus on the physical pins).
- **Authentication chain (why it's hard):**
  1. **RSA-OAEP** key exchange — random values exchanged both ways establish a
     shared **AES-128 (CBC + CTR)** key and IV/CTR.
  2. **Challenge-response** — each side sends AES-128-CBC-encrypted auth data, the
     other decrypts + hashes it and returns the encrypted hash. Both directions.
  3. **Secure mode** — after `ChangeModeToSecure`, *all* traffic is AES-128-CTR.
  4. FS computes a **SHA-256 over the 0x200-byte InitialData** and compares to the
     hash at **offset 0x160 in the CardHeader**.
- **Firmware (LAFW):** RSA-2048 PKCS#1 signed; an **OTP-fuse anti-downgrade**
  burns a value matching the firmware version, blocking older firmware.

**Takeaway for us:** we are *not* going to forge authentication. The realistic
angles are the console-side software patch (make FS tolerate a failed/absent card
and keep power up) or a hardware approach that lives on the bus without needing to
fully authenticate. Both are in the README's bypass section.

---

## 4. GBAtemp discussion threads (annotated)

The community's collective brain on this. Read in roughly this order:

| Thread | Why it matters | Link |
| :--- | :--- | :--- |
| **Switch Cartridge - Reverse Engineering** (the big one, 10+ pages) | The foundational RE thread: captures, pinouts, protocol guesses, FPGA emulation attempts | https://gbatemp.net/threads/switch-cartridge-reverse-engineering.464580/ |
| **The Switch Flashcart Thread (Mig Switch/UnlockSwitch etc.)** (130+ pages) | Living thread on every flashcart; MIG internals (ESP32 + iCE40 FPGA), encrypted firmware, scratched chip markings | https://gbatemp.net/threads/the-switch-flashcart-thread-mig-switch-unlockswitch-etc.651852/ |
| **Why is there no Switch flash cart?** | The "it's impossible / no it isn't" debate; clearest statement of the Lotus3 barrier and the eMMC-emulation counterargument | https://gbatemp.net/threads/why-is-there-no-switch-flash-cart.604197/ |
| **Change the Lotus3 (Gamecard ASIC) to an eMMC — is it possible?** | Explores swapping Lotus3 for a raw eMMC/microSD as a third storage medium; explains the two-interface split | https://gbatemp.net/threads/change-the-lotus3-gamecard-asic-ic-to-a-emmc-is-possible.637373/ |
| **Is it possible to make a flashcart for the Switch?** | Older feasibility discussion, good for the "sandboxed slot" framing | https://gbatemp.net/threads/is-possible-to-make-a-flashcart-for-the-switch.599971/ |
| **Flash cart for the Switch incoming?** (10+ pages) | Tracks the run-up to working flashcarts; FPGA-emulates-eMMC arguments | https://gbatemp.net/threads/flash-cart-for-the-switch-incoming.644953/ |
| **R4-like Switch FPGA Code** | FPGA-side code discussion | https://gbatemp.net/threads/r4-like-switch-fpga-code.645150/ |
| **Getting the MIG Switch to load an XCI without its original Initial Data** | InitialData.bin + Certificate.bin requirement; certificates are per-cart unique | https://gbatemp.net/threads/getting-the-mig-switch-to-load-an-xci-dump-without-its-original-initial-data.653134/ |
| **HID-Mitm: rebind buttons / custom gamepads** | The software-MITM pattern we'd reuse for capturing vibration values | https://gbatemp.net/threads/hid-mitm-rebind-buttons-and-use-custom-gamepads-on-your-switch.535095/ |
| **HD Rumble (kinda) on a fake Switch Pro controller [tutorial]** | Practical rumble-motor wiring notes from the controller side | https://gbatemp.net/threads/hd-rumble-kinda-on-a-fake-switch-pro-controller-tutorial.555560/ |

> Note: GBAtemp blocks automated fetching (HTTP 403), so the annotations above come
> from search summaries and titles. Read the threads directly in a browser for the
> full detail — they're the richest single resource on this topic.

---

## 5. Prior art: MIG Switch / MIG Flash (the closest real device)

- **What it is:** a flashcart that mimics a real game card, loading ROMs from a
  microSD. Hardware: a **low-cost iCE40-class FPGA** emulating the gamecard LSI plus
  an **ESP32** (ESP32-S2 on the dumper) for management; firmware **encrypted** and
  chip markings scratched off to slow RE.
- **Why it's our best reference:** it proves a board can fit the card envelope, hold
  up power, and speak the Lotus3 protocol well enough to be read. The PCB design
  files are imported into [`hardware/MIG-reference/`](./hardware/MIG-reference/).
- **The catch:** the FPGA + MCU are **programmed and encrypted at the factory**;
  you cannot build a working unit from the design files alone — you'd transplant the
  OEM chips. (Same limitation applies to anything we'd derive from it.)
- **Ban/legal context (for awareness):** on Switch 2, early MIG use triggered
  automatic online bans; Nintendo won a **$2M judgment** against a seller (Daly) in
  2025. None of this touches a rumble-only device, but it shows how aggressively the
  slot is policed.
- Sources: [Mig Flash, Wikipedia](https://en.wikipedia.org/wiki/Mig_Flash),
  [The Switch Flashcart Thread](https://gbatemp.net/threads/the-switch-flashcart-thread-mig-switch-unlockswitch-etc.651852/),
  [original PCB repo by sabogalc](https://github.com/sabogalc/MIG-Flash-PCBs).

---

## 6. Software side — capturing the vibration data

This is the half that's *very* tractable, independent of the slot problem.

- **The HID service** generates vibration on the console. We MITM it. Reference
  command IDs (from the README spec): `SendVibrationValue` (153),
  `SendVibrationValues` (154), carrying `VibrationValue` (amp/freq, low + high band).
- **Atmosphère `ams_mitm`** — the official MITM framework; the only CFW with the
  hooks to intercept these services.
  https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/ams_mitm.md
- **jakibaki/hid-mitm** — working example of MITM'ing the `hid` service to reshape
  controller/gamepad data. Direct template for a vibration tap.
  https://github.com/jakibaki/hid-mitm/
- **MissionControl** — a mature sysmodule that *decodes HD rumble* for third-party
  controllers; its rumble-decoding code is a goldmine for turning `VibrationValue`
  into a real motor waveform.
  https://www.gamebrew.org/wiki/MissionControl_Switch

**Strategy:** build the HID-MITM vibration tap first (pure software, testable on any
modded Switch), serialize the values out over *some* channel, and solve the physical
slot transport separately.

---

## 7. Existing Switch Lite rumble solutions (non-slot)

Worth knowing what already exists, even though they sidestep the slot:

- **"Shock 'N' Rock" / snap-on rumble grips** — clip onto the back of a Switch Lite,
  own battery, motors triggered by **audio from the headphone jack** (not real HD
  rumble data). Crude but shipping. https://gizmodo.com/snap-on-grips-upgrade-the-nintendo-switch-lite-with-rum-1844375655
- **Wireless Joy-Con / Pro Controller** — the only *official* way to get HD Rumble on
  a Lite, but defeats the point of a handheld.

Our slot approach is harder but gives **real, game-driven** vibration data, not an
audio-triggered approximation. That's the differentiator.

---

## 8. Tools & repositories

| Tool | Use | Link |
| :--- | :--- | :--- |
| **nxdumptool** | Dump XCI / InitialData / CardIdSet / certificates from real gamecards. Talks to the card via the **legit FS API** (`fsOpenGameCardStorage`, `fsDeviceOperator*`), parses CardId maker codes (MegaChips 0xC2, Lapis 0xAE…), and even dumps the **Lotus ASIC firmware (LAFW)** from RAM. The clearest real-world map of the FS↔gamecard path. **GPL-3.0 — same as this repo**, so patterns are reusable with attribution | https://github.com/DarkMatterCore/nxdumptool |
| **gcdumptool** | Older gamecard dump tool (XCI + cert handling) | https://github.com/DDinghoya/gcdumptool |
| **kicad-boardview** | The KiCad plugin used to generate the `.bvr`/`.obdata` boardviews in `hardware/` | https://github.com/whitequark/kicad-boardview |
| **OpenBoardView** | Free viewer for the `.bvr` boardview files | https://github.com/OpenBoardView/OpenBoardView |
| **Project IceStorm / yosys / nextpnr** | Open-source toolchain for the iCE40 FPGA we'd target | https://github.com/YosysHQ |

---

## 9. Cryptography & keys

- **SciresM — Switch RSA-PKCS#1 public key recovery** — recovers the three gamecard
  RSA-2048 *public* keys (HEAD, CERT, LAFW signature validators) by exploiting
  deterministic PKCS#1 padding (compute padded M from data, GCD of two moduli).
  Lets you *verify* gamecard signatures on PC, but **public keys can't forge** — so
  it doesn't break authentication, it just illustrates the crypto.
  https://gist.github.com/SciresM/d31aa89f46a8ab18345b56fbeb3cebc9
- **Certificate facts:** `Certificate.bin` + `InitialData.bin` are required to boot a
  given game; they're identical across all copies of a title, but each physical cart
  also carries a **unique certificate**. CardKeyArea is stored encrypted and holds
  the title keys used by InitialData.

---

## 10. What all this means for Switch-lite-Rumble

Pulling the threads together:

1. **Don't fight Lotus3 head-on.** Authentication is RSA-OAEP + AES challenge-
   response backed by a Cortex-M3 with signed firmware. Forging it is out.
2. **Power is the real fight, and it's winnable on the software side.** The viable
   path is patching FS so it doesn't power-gate on a failed/absent card (README
   Option B), plus a hardware energy buffer for the motor's >300 mA peaks.
3. **The data tap is easy and can be built today.** HID-MITM under Atmosphère gives
   us the vibration values now; MissionControl shows how to decode them. This work
   doesn't depend on solving the slot at all.
4. **The MIG Flashcart is the form-factor proof.** A 0.8 mm ENIG board with an
   FPGA+MCU fits the enclosure and survives on the bus — exactly the envelope a slot
   rumble module needs.
5. **Decouple the two halves.** Software vibration tap and physical slot transport
   are independent problems — build and test them separately, integrate last.

---

## 11. Open leads still to chase

- Read the full **Switch Cartridge - Reverse Engineering** thread page-by-page for
  logic-analyzer captures and timing numbers (search summaries don't capture these).
- ~~Find the exact FPGA part MIG uses (markings scratched).~~ **Resolved by team
  teardown:** MIG's main FPGA is a **Microsemi/Microchip IGLOO2 M2GL010** (variant
  **M2GL025**), helper MCU **ESP32-S3** — *not* an iCE40 as earlier search summaries
  implied. See [`docs/DESIGN-NOTES.md`](./docs/DESIGN-NOTES.md) §4 and
  [`CHIPS.md`](./CHIPS.md) §4.1.
- Confirm whether FS's power-gating-on-auth-failure has a **hardware timeout** in
  Lotus3 that a software patch can't override.
- Look for anyone who has actually driven the **DAT lines as UART/SPI** with the
  stock gamecard driver disabled.
- Investigate the **Lotus3→eMMC swap** thread's conclusions for an alternate power
  story.

---

## 12. Additional technical findings (round 2)

### 12.1 Game send rumble even on a Lite — but the built-in pad has no motor
- The Switch Lite's **integrated controller has no vibration device**. Games still
  send rumble through the standard **HID npad** protocol; only external wireless
  Joy-Con / Pro Controllers physically vibrate.
- **Implication for our capture point:** the live signal exists in `hid`, but it may
  be addressed to a paired controller's npad, not the handheld one. Open empirical
  question (**resolves DESIGN-NOTES open Q2**): does a game call
  `hidInitializeVibrationDevices(Handheld)` and `SendVibrationValues` for the
  built-in handheld npad even though it has no motor? If yes, the MITM taps it
  directly. If no, we likely must **register a virtual controller** so the game has a
  vibration-capable target to send to. **Must be tested on hardware.**

### 12.2 Data transport off the console (no slot needed)
- **USB-C (device mode):** libnx `usb_comms` (`usbCommsInitialize` /
  `usbCommsRead/Write`) lets a sysmodule push bytes over USB-C to a host. Proven by
  Tinfoil. Lowest latency, but the cable occupies the only port and implies a
  tethered/host topology.
- **Bluetooth:** the cleaner "dongle" channel, but the Switch's BT stack is busy with
  controllers; a custom BLE peripheral link from a sysmodule is more involved than
  USB. MissionControl shows BT MITM is feasible under Atmosphère.
- First milestone can sidestep both with a wired/GPIO/UART-style debug output; pick
  USB-C vs BT after measuring latency (DESIGN-NOTES Q3).

### 12.3 Gamecard bus timing (for the deferred slot path)
- **CLK = 25 MHz**, 8-bit bus, data captured on the rising edge.
- Command = **16 bytes + 4-byte CRC-32**. Host pulls **CS low**, clocks a byte/cycle.
- Read handshake: host clocks 2 settle cycles; if the card didn't get "CRC OK" it
  replies `01`; when ready it **pulls DAT0 high for 2 cycles**, then streams data +
  4-byte CRC-32. This is the real-time behaviour any slot-side emulator must match —
  reinforces why an FPGA (not an MCU bit-bang) is needed for the slot path.

### 12.4 Actuator specs to design against
- Genuine Joy-Con LRAs: resonant **180–250 Hz ±5%**; HD rumble usable span
  **~41–1253 Hz** across two bands.
- LRAs are most efficient **at resonance**; off-resonance drive wastes power and
  feels weak — matters given the tight power budget. Pin the DRV2605L to the Taptic
  engine's resonance and modulate amplitude (RTP mode) for the first pass.
- Encoding math + lookup tables captured in [`docs/RUMBLE-ENCODING.md`](./docs/RUMBLE-ENCODING.md).

### 12.5 Switch Lite internals (HOAG) — fitting space
- SoC: **Tegra T210B01**, 4 GB LPDDR4X, **32 GB soldered eMMC**, **13.6 Wh** flat
  battery, downsized fan/heatsink.
- The **Game Card reader is a replaceable module** (good — implies a defined, openable
  mechanical interface around the slot).
- iFixit doesn't give slot/board dimensions; the team is sourcing exact cartridge
  measurements + 3D models directly (DESIGN-NOTES §7). The shell can grow **upward**,
  so actuator height isn't bound by the 2.1 mm card thickness.

### Sources (round 2)
- HD rumble encoding — [dekuNukem Switch RE: rumble_data_table.md](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/rumble_data_table.md), [HD-rumble data issue #11](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/issues/11)
- Rumble decode in practice — [MissionControl](https://github.com/ndeadly/MissionControl)
- libnx USB comms — [usb_comms.h](https://switchbrew.github.io/libnx/usb__comms_8h.html)
- Gamecard timing — [switchbrew: Gamecard](https://switchbrew.org/wiki/Gamecard), [ReSwitched Wiki: gamecard](https://reswitched.tech/hardware/gamecard/)
- Actuator specs — [Precision Microdrives: LRAs](https://www.precisionmicrodrives.com/linear-resonant-actuators-lras), [TechRadar: HD Rumble](https://www.techradar.com/news/meet-the-minds-behind-nintendo-switchs-hd-rumble-tech)
- Switch Lite teardown — [iFixit](https://www.ifixit.com/Teardown/Nintendo+Switch+Lite+Teardown/126223)
