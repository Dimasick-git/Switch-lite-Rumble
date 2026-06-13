# docs/

Working design documents and the decision log for the Switch-lite-Rumble project.

| File | What's inside |
| :--- | :--- |
| [`DESIGN-NOTES.md`](./DESIGN-NOTES.md) | **Main:** the discussion summary and decisions — the architectures (slot vs. external dongle), technical facts, what was agreed first, open questions |
| [`concept-external-approach.md`](./concept-external-approach.md) | The "external/sysmodule" concept (v1): tap rumble in `hid` → forward over USB-C/Bluetooth to an external actuator. Slot not used. (Earlier concept; the slot path is now the primary goal.) |
| [`slot-approach-technical-spec.md`](./slot-approach-technical-spec.md) | The "through-the-slot" spec: cartridge emulation, slot power, the Lotus3 problem. The primary path. |
| [`RUMBLE-ENCODING.md`](./RUMBLE-ENCODING.md) | How HD rumble works: the `HidVibrationValue` float layer, logarithmic frequency/amplitude encoding, ranges, and the mapping onto an actuator (Taptic/LRA via DRV2605L) |
| [`HAPTICS.md`](./HAPTICS.md) | Haptics: actuator types (ERM/LRA/Taptic/piezo/voice-coil), comparison, the DRV2605L driver, power, and what fits the project |

Related material at the repo root: [`../CHIPS.md`](../CHIPS.md) (chip selection and
physical constraints) and [`../RESEARCH.md`](../RESEARCH.md) (the archive of sources
and findings on Lotus3 / the cartridge protocol).
