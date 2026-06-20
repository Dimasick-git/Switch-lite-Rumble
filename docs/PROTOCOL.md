# Wire protocol: sysmodule → actuator device

The compact frame the Switch-side sysmodule emits and the actuator device (ESP32 +
DRV2605L) consumes. One frame carries one side's current vibration state. Transport is
byte-stream agnostic — USB-CDC serial today, BLE later — the framing handles sync.

## Frame (7 bytes, fixed)

| Offset | Field | Type | Meaning |
| :--- | :--- | :--- | :--- |
| 0 | `SYNC` | u8 = `0xA5` | start-of-frame marker |
| 1 | `side` | u8 | `0` = left, `1` = right, `0xFF` = stop all |
| 2 | `amp_low` | u8 | low-band amplitude, `0..255` ↔ `0.0..1.0` |
| 3 | `freq_low` | u8 | low-band frequency, `Hz ≈ value * 8` (0..~2040 Hz) |
| 4 | `amp_high` | u8 | high-band amplitude, `0..255` ↔ `0.0..1.0` |
| 5 | `freq_high` | u8 | high-band frequency, `Hz ≈ value * 8` |
| 6 | `xsum` | u8 | XOR of bytes 1..5 |

## Encoding (Switch side)

From a captured `HidVibrationValue` (floats):

```
amp_low  = clamp(value.amp_low,  0,1) * 255
freq_low = clamp(value.freq_low / 8, 0, 255)
amp_high = clamp(value.amp_high, 0,1) * 255
freq_high= clamp(value.freq_high / 8, 0, 255)
side     = device_idx of the VibrationDeviceHandle (0=L, 1=R)
```

Send one frame per side whenever the value changes (the logger already tracks change
per handle). A `side = 0xFF` frame means "silence both" (e.g. on game exit).

## Decoding (actuator side)

The device collapses the two bands to one drive strength (a single LRA can't play both
honestly):

```
drive = max(amp_low, amp_high)            // 0..255, fed straight to DRV2605L RTP
```

Frequency bytes are advisory; the DRV2605L runs the LRA at its own resonance
(auto-resonance), so for v1 only `drive` is used. Later, `freq_*` can pick an effect
or bias.

## Robustness

- Resync on any byte: if `SYNC` isn't `0xA5`, drop one byte and keep scanning.
- Drop frames with a bad `xsum`.
- If no frame arrives for ~200 ms, ramp the actuator to 0 (failsafe).

## Reference implementations

- Switch side (encoder): the [`rumble-logger-mitm`](../software/rumble-logger-mitm/)
  already captures and splits per side; the forwarder emits these frames.
- Actuator side (decoder): [`firmware/actuator-esp32`](../firmware/actuator-esp32/).
- Host test sender (no console needed):
  [`firmware/actuator-esp32/tools/rumble-send.py`](../firmware/actuator-esp32/tools/rumble-send.py).
