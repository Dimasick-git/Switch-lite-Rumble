# Haptics: actuator types and tactile-feedback systems

A reference on the "vibration hardware" for the project: what actuators exist, how
they differ, how to drive them, and which fit the form factor (a small removable
module for the Switch Lite with its own actuator).

Related docs: [`RUMBLE-ENCODING.md`](./RUMBLE-ENCODING.md) (how to decode HD rumble
from `hid`), [`../CHIPS.md`](../CHIPS.md) (chip selection and physical envelope),
[`DESIGN-NOTES.md`](./DESIGN-NOTES.md) §0 (why an actuator is needed at all).

---

## 1. The core principle

Tactile feedback = **controlled mechanical vibration**. "HD-Rumble quality" depends on
whether you can **independently** vary:
- **amplitude** (strength), and
- **frequency** (tone/character — a "tap," a "hum," a "texture").

Plain motors can't do this (frequency and strength are coupled). HD Rumble can —
because it uses **linear actuators**, not a spinning eccentric mass.

---

## 2. Actuator types

### 2.1 ERM (Eccentric Rotating Mass) — the plain "vibro motor"
A motor spins an off-center weight.
- **Pros:** dirt cheap, simple, everywhere; "buzzes" noticeably.
- **Cons:** amplitude and frequency are **coupled** (stronger = faster = higher tone);
  slow start/stop (tens of ms); coarse feel. Can't do "HD."
- **Size:** 6–10 mm coins, cylinders.
- **For us:** only as a cheap "vibration yes/no" prototype. Not for the final build.

### 2.2 LRA (Linear Resonant Actuator) — linear resonant
A mass on a spring + a coil, oscillating along one axis at a **resonant frequency**.
- **Pros:** fast start/stop (single-digit ms), a crisp "click" feel, amplitude
  controlled separately; **the same class as in Joy-Cons**.
- **Cons:** efficient **only near resonance** (narrow band); off resonance it's weak
  and wastes current. Needs an auto-resonance driver.
- **Size:** ~8–10 mm coins (Z-axis) and rectangular (X/Y-axis).
- **Frequencies:** typical resonance 150–235 Hz; **real Joy-Con LRAs: 180–250 Hz
  ±5%**.
- **For us:** the baseline workable option. Cheap, thin, "HD enough."

### 2.3 Wideband LRA / "haptic reactor" — wideband linear
An improved LRA that holds a band wider than a single resonance.
- The Joy-Con 2 (Switch 2) uses a **Wideband LRA based on the Alps Alpine Haptic
  Reactor**.
- **Pros:** closer to "real HD texture," a wider tonal range.
- **Cons:** more expensive, rarer as a repair part.
- **For us:** the feel ideal, but availability/cost are questionable.

### 2.4 Taptic Engine (Apple) — a large linear actuator (candidate #1)
Essentially a large, well-tuned **wideband LRA**.
- **Pros:** an excellent size/feel ratio; strong yet precise; genuinely capable of
  HD-like vibration. Sold widely as a repair part (AliExpress). Thin enough — there's
  an example of fitting it between the board and the Lite shell.
- **Cons:** larger than a coin LRA; high peak current (needs a power buffer); needs
  proper drive (see §4). Pinout/resonance must be measured per specific model.
- **For us:** the **target actuator** for real feel. The cartridge/module shell can be
  extended upward (leave the bottom), so height isn't a blocker.

### 2.5 Piezo actuators
A bending piezo plate.
- **Pros:** very thin, fast, wideband, "sharp" feel.
- **Cons:** need **high voltage** (tens–hundreds of V) → a separate boost driver,
  which is bad for the tight power budget and size.
- **For us:** interesting for thinness, but the power need kills it in a slot/dongle.

### 2.6 Voice-coil / wideband "vibration speakers"
A linear drive like a mini subwoofer.
- **Pros:** the richest, "audio-quality" feedback.
- **Cons:** large, power-hungry.
- **For us:** won't fit.

---

## 3. Comparison (brief)

| Type | Indep. amp/freq | Speed | "HD" quality | Size | Power | For the project |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| ERM | ✗ | slow | low | small | low | prototype only |
| LRA (coin) | ✓ (narrowband) | fast | medium | very small | low | baseline option |
| Wideband LRA | ✓ | fast | high | small | medium | ideal, but costly/rare |
| **Taptic (iPhone)** | ✓ | fast | **high** | medium | medium-high | **target** |
| Piezo | ✓ | very fast | high | thin | **high (HV)** | power rules it out |
| Voice-coil | ✓ | fast | max | large | high | won't fit |

---

## 4. Drive: the actuator driver

You don't hang the actuator straight off a GPIO — you need a **haptic driver**.

### 4.1 TI DRV2605L (recommended)
- Built for **LRA and ERM**, in a **DSBGA 1.5 × 1.5 mm** package (fits) or VSSOP
  3×3 mm.
- **Closed loop with auto-resonance** — keeps an LRA at resonance itself (matters for
  efficiency + feel).
- **I²C** control; a built-in library of **123 effects** (up to 8 chained).
- **RTP (Real-Time Playback) mode:** write intensity **0–255** to a register for
  instant strength control. This is the main mode: send amplitude per rumble frame.
- Power 2–5.2 V.

### 4.2 Drive scheme for this task
1. The sysmodule catches `HidVibrationValue` (amp/freq, 2 bands) — see
   `RUMBLE-ENCODING.md`.
2. Collapse the 2 bands into one strength value; to start,
   `drive = clamp(max(amp_low, amp_high), 0, 1)`.
3. Send it to the ESP32 (USB-C/BLE), which pushes an **RTP value 0–255** to the
   DRV2605L over I²C.
4. The DRV2605L holds the actuator at resonance; only strength is modulated. Later,
   play with frequency/effects for "texture."

### 4.3 Driver alternatives
- **DRV2603/DRV2604** — simpler/lighter, if the effect library isn't needed.
- **Direct H-bridge + PWM from the MCU** — maximum flexibility, but all the
  auto-resonance and safety logic is on you; not worth it for v1.

---

## 5. Power (critical to the feel)

- A linear actuator's peak (especially the Taptic) is **>300 mA** in short pulses,
  above both the idle USB-passthrough budget and certainly the slot.
- Fix: an **energy buffer next to the actuator** — a supercap or small Li-Po, trickle-
  charged while idle, delivering the peak on impact.
- An LRA is efficient near resonance → the right driver (DRV2605L) saves current.
- Powering from a module with its own battery (dongle) removes the problem but grows
  the size. With USB-C passthrough, a buffer is mandatory.

More on currents/rails — [`../CHIPS.md`](../CHIPS.md), the "Power" section.

---

## 6. How "HD Rumble" maps onto the actuator

- HD Rumble = **2 bands** (low + high), each with independent amplitude and frequency;
  range ~41–1253 Hz.
- A cheap coin LRA or a Taptic physically holds a **narrow band at its resonance**
  (≈150–250 Hz), so you can't reproduce both bands "honestly" — **collapse to one** and
  modulate strength. That's "HD enough" for a handheld.
- **MissionControl already does this collapse-decode** for third-party controllers
  (incl. LRAs) — open source, the "2 bands → one actuator" math is reusable (GPL).
  Encoding formulas and tables are in `RUMBLE-ENCODING.md`.

---

## 7. Recommendation for the project

1. **Prototype:** coin LRA + DRV2605L (RTP) — cheap, quickly validates the whole chain
   "capture → ESP32 → I²C → actuator."
2. **Final:** **iPhone 12 Taptic Engine** + DRV2605L + power buffer (supercap), for
   real feel; extend the shell upward.
3. **ERM** — only if a quick, crude "vibration exists" demo is needed.
4. **Piezo/voice-coil/wideband-LRA** — kept in mind, but power/cost/size rule them out
   for now.
