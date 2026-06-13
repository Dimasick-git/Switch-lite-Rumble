# Design notes & decisions

*Summary of the project discussion, distilled from working chats.*

> Records the architectures debated (in-slot cartridge vs. external/sysmodule
> dongle), the technical facts established, what was agreed as the first step, and the
> open questions. The two source design docs live alongside this file in
> [`docs/`](.).

---

## 0. What the project actually solves (keep focus)

Experiment confirmed: **the vibration signal on a Lite is live and trivially
sendable** from homebrew/an overlay — a paired Joy-Con vibrates. It's easy to wrongly
conclude "the project is pointless." It isn't — the goal just has to be stated
precisely:

> **The point isn't to "revive the signal" (it always existed); it's to provide an
> actuator you feel while playing the Lite as a handheld, without holding or
> embedding whole Joy-Cons.**

Direct consequences that drive the whole architecture:

1. **A paired Joy-Con's vibration only helps while you hold it** — which kills the
   "all in one body" point. So "it already vibrates" ≠ a solution.
2. **Embedding real Joy-Con haptics inside (Path C) needs no custom software at all.**
   The console drives the paired Joy-Con itself. A working but crude solution.
3. **A sysmodule/MITM is justified in exactly one case — driving a non-Joy-Con
   actuator** (e.g. a Taptic engine) the console can't talk to itself. Only then is
   intercepting the game's rumble worthwhile.
4. **The defensible value over "just stuff a Joy-Con inside" = plug-and-play + small
   size + no console surgery and no sacrificing two Joy-Cons.** If the module needs
   the same soldering as Path C, Path C wins.
5. **The in-slot cartridge is the target form factor.** The cleanest "plug it in and
   it works," with no console surgery. The main difficulty is slot power (see §12). If
   that doesn't pan out in hardware, the external dongle gives the same result via the
   fallback.

Focus: aim for a **cartridge in the slot with its own actuator (L/R)**, driven by a
capture sysmodule; keep external USB-C/BT as the backup. Actuator detail in
[`HAPTICS.md`](./HAPTICS.md).

---

## 1. The debate: three architectures

Three paths emerged; it's important not to conflate them.

### Path A — through the slot (cartridge emulation, like MIG Switch)
A cartridge device sits in the slot, pretends to be a real card, draws power from the
slot, and sends/receives vibration data over the cartridge bus. Detailed in
[`slot-approach-technical-spec.md`](./slot-approach-technical-spec.md).

- **Plus:** a clean "real cartridge," one form factor, nothing sticking out.
- **Minus:** getting the slot to power up and not cut off means dealing with Lotus3
  and its init / power-gating — the technically hardest node of the project (see §12).

### Path B — external / sysmodule (dongle)
A custom sysmodule catches the vibration in `hid` and forwards it over **USB-C /
Bluetooth** to an external device (ESP32 + actuator). The slot is **not used at all**.
Detailed in [`concept-external-approach.md`](./concept-external-approach.md).

- **Plus:** simple, testable today, doesn't hit Lotus3.
- **Minus:** a "dongle" form factor, not the ideal in-slot cartridge.

### Path C — physical Joy-Con transplant (known prior art)
No software or RE: **Joy-Cons (or their vibro motors) are physically embedded** in the
Lite shell, and the vibration comes from them as normally-paired controllers. Needs
mechanical work — relocating a few buttons, soldering standoffs in the shell. Someone
actually built this prototype (see §11 "Sources"); demo: button press → vibration.

- **Plus:** works out of the box, no firmware and no bypass of anything — the console
  just sends rumble to paired Joy-Cons that happen to be inside.
- **Minus:** a crude build, takes the space of two Joy-Cons, not plug-and-play, needs
  opening and soldering the console. Not the goal (cartridge/dongle), but an
  **important reference and proof of the key fact** below.

---

## 2. The key fact everything rests on

**The rumble pipeline on a Switch Lite is NOT removed.** The Lite works normally with
external Joy-Con / Pro Controllers that have vibration, so the `hid` subsystem
**computes vibration values as usual** — there's just no actuator in the built-in
body. The signal is live and can be intercepted. That makes Path B realistic without
any reverse engineering of the proprietary bus.

**Confirmation from Path C (important, with a caveat).** The contact who built the
transplant confirmed: **paired Joy-Cons on a Lite vibrate normally**. So rumble to
external/paired controllers definitely goes through. **BUT** that does not prove the
built-in handheld npad receives vibration values — the Joy-Cons in that build are
still *external* controllers, just placed inside. The original worry ("vibration is
muted in handheld mode") is **not** closed by this: the open question is specifically
about the motorless handheld npad (see §9 #2 and
[`../RESEARCH.md`](../RESEARCH.md) §12.1) — answerable only on hardware.

---

## 3. What's established about the slot protection (Lotus3)

- The card is managed by the **Lotus3** ASIC; the bridge processor sends an
  authenticity request **almost every ms**.
- For the console to accept a device, it must answer **CMD60 with OperationId 0x0E**
  and present a correct, **Nintendo-signed game header (HACR)**. Without a valid
  **RSA signature** on the header the console won't even start talking.
- On authentication failure, Lotus3 **cuts power** and puts the bus into an error
  state.
- Slot takeaway: the signature can't be forged. In theory what remains is either an
  `fs` software patch (make the system not power-gate on error) or MITM tricks — but
  that's the same circumvention that makes the project resemble a flashcart, so the
  slot isn't taken as the *first* step.

---

## 4. Reference device: MIG Switch (chip teardown)

To understand what's actually needed to live in the slot, MIG was broken down:

- **Main chip — Microsemi IGLOO2 FPGA**, model **M2GL010** or **M2GL025**. A teardown
  confirmed an **IGLOO2 M2GL010** in a real MIG Switch. *(This corrects earlier
  research that assumed iCE40 — see [`../RESEARCH.md`](../RESEARCH.md). This hardware
  revision is IGLOO2.)*
- **Helper chip — ESP32-S3.** Handles the microSD card, firmware update, and loading
  the FPGA config at power-on.
- **Key takeaway:** much of MIG's hardware (USB, file catalogs, microSD) is **not
  needed** for the rumble project. Carrying the whole MIG board makes no sense — it
  "doesn't save space at all." Fine for a prototype/tests; for the final build, a
  **board from scratch**.
- Open question: is the ESP32 needed at all, or does the FPGA alone suffice for this
  narrow task? Needs working out what each chip actually does.

---

## 5. Power (idea from the discussion)

- The slot-path spec originally assumed 3.1 V power. The chat clarified: **you can
  also power from 1.8 V**.
- Proposed scheme: **power the controller from 1.8 V through a resistor** (plenty for
  it), **the motor from 3.1 V through a small capacitor**.
- Either way the actuator peak (>300 mA) exceeds the slot budget → a buffer is needed
  (supercap / small battery / capacitor), trickle-charged while idle.

---

## 6. Actuator (vibro motor)

- Candidate #1 — the **iPhone 12 Taptic Engine**. Reasons: good size/quality ratio,
  strong but controllable feedback, capable of HD-like vibration in theory. Sold on
  AliExpress. Thin enough — there's an example fitting it between the board and the
  Lite shell.
- Selection criterion: "not a token buzz," yet **low-power** and small enough (vibro +
  chip + microcontroller all within the cartridge envelope).

---

## 7. Shell form factor

- A standard cartridge is very small, but it **doesn't have to be matched 1:1**. What
  matters is that everything fits inside and the lid closes.
- Agreement: **the top can be extended**, the bottom is best left alone. There turned
  out to be more room up top than expected.
- Exact cartridge measurements were requested from a contact; dimensions and 3D models
  exist online (see also [`../CHIPS.md`](../CHIPS.md), the physical-envelope section).

---

## 8. What was agreed as the first step

Despite the debate over final hardware, the first step was agreed:

> **Step 1 — software.** Write a homebrew/sysmodule that catches the signal in `hid`
> (at first even on a button press) and emits it outward — e.g. to an external device,
> light an LED / spin a test motor. A pure-software "first win," testable on a live
> console, **independent of the slot and Lotus3.**

Then: forward the values to an ESP32 → real actuator → HD-rumble mapping → enclosure.
Project difficulty rated **medium**: the main risk isn't code brilliance but the
"edit → test on hardware → analyze" loop.

"Start simple" is the current decision; the slot path is deferred but not dropped.

---

## 8a. API corrections (verified)

A few details from early documents corrected against switchbrew/libnx:

- **`hid` vibration command IDs** were listed in the original spec as 153/154 — **that
  is wrong**. The real IDs: **`SendVibrationValue` = 201**, **`SendVibrationValues` =
  206** (also `GetVibrationDeviceInfo` = 200, `GetActualVibrationValue` = 202,
  `CreateActiveVibrationDeviceList` = 203, `SendVibrationValueInBool` = 212).
- **MIG's FPGA** — IGLOO2 M2GL010, not iCE40 (see §4).
- libnx ≥ 4.0.0: vibration handles are structs (`HidVibrationDeviceHandle`), and
  `HidVibrationValue` holds `{ amp_low, freq_low, amp_high, freq_high }`.

## 9. Open questions (what to reverse next)

1. **Where exactly in `hid`** to tap the active player's vibration values.
2. **Does the Lite populate** the built-in handheld controller's vibration device, or
   must a virtual controller be registered for the game to send it rumble.
   *(Round-2 research: the Lite's built-in controller has no motor at all; games send
   rumble over the standard HID npad, and only external controllers feel it. The
   question reduces to whether a game calls `SendVibrationValues` for the handheld npad
   despite the missing motor — if yes, the MITM catches it directly; if no, a virtual
   controller is needed. Hardware-only. See [`../RESEARCH.md`](../RESEARCH.md) §12.1.)*
3. **Channel latency** (USB vs BT), so the vibration doesn't lag.
4. **HD-rumble mapping** (amplitude/frequency) onto the chosen actuator (Taptic).
5. On the slot path: does Lotus3 have a **hardware timeout** on power without
   successful init (decides whether an `fs` software patch can even survive).
6. What **each chip** in MIG actually does, and whether the ESP32 can be dropped.

Answer tools: **switchbrew wiki + libnx/Atmosphere sources + hardware experiments**
(not a disassembler of the proprietary bus).

---

## 10. Maintainers

Maintained by a small group of contributors (reverse engineering + hardware, with a
programmer on implementation). Background includes an **LED-panel mod for the Switch
Lite** (HOAG model). Specific external sources are credited below and in
[`../RESEARCH.md`](./../RESEARCH.md).

---

## 11. Sources / contacts

- **A MIG Switch owner** — provided the chip/data on IGLOO2 M2GL010 (§4).
- **An anonymous contact (Path C, Joy-Con transplant)** — previously built a handheld
  Lite prototype with embedded Joy-Cons (vibration from paired controllers, relocated
  buttons, soldered standoffs; there was a demo video — button press → vibration).
  Gave the valuable confirmation that paired Joy-Cons on a Lite vibrate normally (§2).
  **No source/firmware** (a purely mechanical approach), no GitHub account; **declined
  a co-author credit and gave no name** — so recorded as an anonymous source, not a
  contributor/author. If they later agree, they'll be added on their terms.

---

## 12. Consultation (Cooler3D) — key points

Analysis from the author of 4IFIR (who provided the GC-rail control code):

- **The Lite in HOS:** the Lite is a tablet like the other models; the body doesn't
  vibrate, connected gamepads do. The system sees the built-in controller as something
  between a **wired gamepad without vibration** and extra body buttons. → Confirms §2:
  the signal is computed, the actuator is missing.
- **Slot as a power source — uncertain on current/voltage.** "LD domains aren't a
  universal outlet": it's not certain the GC rail can drive the actuator by current.
  This is exactly the open question (does GCA power the slot pins, and is there enough
  current) — answerable on hardware.
- **The slot's signal lines** could in principle carry a MITM of the vibration stack
  via a driver-interpreter (on the 4IFIR side). An alternative transport view; kept in
  mind.
- **⚑ Important for the actuator:** "without left/right domain separation you can't
  build *full* vibration." A single actuator with both sides collapsed gives a
  simplified feel; for "real" vibration you need **two actuators (L and R)**, and the
  vibration stream must be **kept split per side**. Practical note: the logger already
  writes values **per handle** (the handle encodes position/side), so **L/R separation
  is preserved** in the log — don't collapse it prematurely.
- **Cooler3D's hardmod preference:** compact "button-sized" vibro elements, slot as
  power (with the caveat above).

### Slot power — directions
To make the slot deliver power, two directions exist: (1) `fs` edits by analogy with
MIG, (2) working with Lotus3 and its bus at the sysmodule level. This is the project's
hardest open piece; progress is tracked in
[`slot-approach-technical-spec.md`](./slot-approach-technical-spec.md). In parallel
there's a backup path not tied to the slot — external USB-C/BT (Path B).
