# HD Rumble encoding reference

Technical reference for milestone 4 (turning captured `HidVibrationValue`s into a
real actuator waveform). Sourced from the Joy-Con reverse-engineering work
([dekuNukem/Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering))
and [MissionControl](https://github.com/ndeadly/MissionControl), which already does
this decode for third-party controllers.

---

## 1. Two layers, don't confuse them

1. **The `hid` service API layer** (what our sysmodule sees): a game calls
   `SendVibrationValue(s)` with a `HidVibrationValue` — **floats**, human-readable:
   `amp_low`, `freq_low`, `amp_high`, `freq_high`.
2. **The Joy-Con BT packet layer** (what the firmware encodes down to): a packed
   **4-byte** rumble payload per side. We do **not** need to emit this — we get the
   nice float layer at the MITM point. But understanding it explains the ranges and
   gives ready-made lookup tables for driving our own actuator.

For our project we work mostly at layer 1 (the floats) and map those to the Taptic
engine / LRA via the DRV2605L.

---

## 2. The float layer (`HidVibrationValue`)

```c
typedef struct {
    float amp_low;    // low-band amplitude
    float freq_low;   // low-band centre frequency (Hz)
    float amp_high;   // high-band amplitude
    float freq_high;  // high-band centre frequency (Hz)
} HidVibrationValue;
```

- HD rumble **decouples amplitude and frequency** (unlike an ERM motor, where they're
  tied to rotation speed). Two independent bands (low + high) are mixed.
- **Neutral / stop** value (conventional): `amp = 0.0`, `freq_low = 160 Hz`,
  `freq_high = 320 Hz`.
- Amplitudes above ~1.0 are flagged **unsafe for LRAs** — clamp before driving real
  hardware.

## 3. Frequency range & encoding (layer 2 detail)

- Valid frequency span: **~41 Hz – 1253 Hz**.
- Joy-Con bands: low **40.875 – 626.286 Hz**, high **81.751 – 1252.572 Hz**.
- Encoding is **logarithmic**: `encoded = round(log2(freq / 10.0) * 32.0)`.
  - High-frequency byte = that value `* 4` (big-endian in the packet).
  - Low-frequency byte = that value `- 0x40`.
  - Capped at 1252 Hz.

## 4. Amplitude encoding (layer 2 detail)

Piecewise logarithmic:

| Amplitude range | Formula |
| :--- | :--- |
| `amp ≥ 0.23` | `log2(8.7 * amp) * 32` |
| `0.12 ≤ amp < 0.23` | `log2(17.0 * amp) * 16` |
| `amp < 0.12` | low-range table (small linear-ish steps) |

The dekuNukem repo ships **full lookup tables** (frequency 41–1253 Hz, amplitude
0.0–1.8) with hex byte equivalents, so a decoder can avoid runtime `log2`.

## 5. Mapping to our actuator (the practical part)

Our target is an **iPhone 12 Taptic Engine** (an LRA) driven by a **DRV2605L** over
I²C. Plan:

1. At the MITM, read `HidVibrationValue` (floats) per frame (~5 ms cadence is what
   games use).
2. Collapse two bands → one drive value the LRA can follow. A simple first pass:
   `drive_amp = clamp(max(amp_low, amp_high), 0, 1)`; pick the dominant band's
   frequency.
3. The Taptic LRA resonates ~**150–230 Hz** (genuine Joy-Con LRAs are spec'd
   **180–250 Hz ±5%**). The LRA is **most efficient at its resonant frequency**, so:
   - Either pin the DRV2605L to the actuator's resonance and modulate only amplitude
     (simplest, power-efficient, "good enough" rumble), or
   - Use the DRV2605L **real-time playback (RTP) mode** (0–255 intensity) and feed it
     the per-frame amplitude for variable strength.
4. The DRV2605L's **closed-loop auto-resonance + 123-effect library** can do a lot of
   the feel work; start with RTP amplitude streaming, refine later.

## 6. Why this is tractable

MissionControl already converts HD-rumble floats into drive signals for non-Nintendo
controllers, including LRAs — so the decode math and the "two bands → one actuator"
collapse are solved problems we can lift (it's open source). Our novelty is only the
**capture point** (a Lite, via `hid` MITM) and the **physical actuator** path.
