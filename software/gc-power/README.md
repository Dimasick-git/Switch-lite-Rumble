# gc-power

Runtime control of the Switch **game-card power rail** via the MAX77620 PMIC over
I²C. This is the "power" half of the in-slot cartridge path: it lets a homebrew turn
on and set the voltage of the rail that feeds the cartridge slot, so an accessory
(our rumble actuator) can draw power there.

A homebrew voltage overlay was observed setting the **GCA** domain to **3100 mV** —
matching the cartridge VCC 3.1 V rail. The likely mapping (see the verification caveat
below — **not yet confirmed on hardware**):

| Symbol | Regulator | Rail | Note |
| :--- | :--- | :--- | :--- |
| `GcPower_GCA` | MAX77620 LDO3 | Game-card **core** ~3.1 V | maxes at 3.2 V |
| `GcPower_GCC` | MAX77620 LDO5 | Game-card **I/O** ~1.8 V | maxes at 1.9 V |

## API

```c
u32    GcPower_GetMv(const GcPowerDomain *domain);            // read mV
Result GcPower_SetMv(const GcPowerDomain *domain, u32 mv);    // set mV (clamped)
Result GcPower_Enable(const GcPowerDomain *domain, bool on);  // enable/disable rail
```

Self-contained on **libnx** (`i2cOpenSession` + the i2c service). To use it from a
sysmodule, give the process `i2c` service access in its NPDM and call the API.

## Attribution

The voltage-domain control logic is derived from runtime code by **Cooler3D**, from
the **[4IFIR](https://github.com/rashevskyv/4IFIR)** project, used with permission.
Thanks! Only the game-card power domains are included here; the rest of the author's
work is not part of this repo.

## Scope / safety

- This is the **power side** of the slot — the GC voltage rail over I2C, separate
  from Lotus3 init/handshake.
- Driving the GC rail is marked "use with caution" in the source overlay for a
  reason: wrong voltages or fighting the system's own power management can be
  destabilising. Test carefully on hardware.
- **Open question:** whether enabling this LDO actually powers the physical slot
  pins, or only powers Lotus3 (which may still gate the pins). This is exactly the
  thing to measure next — see [`../../RESEARCH.md`](../../RESEARCH.md).

## Power budget — verify on hardware (from issue #2)

Before trusting this on hardware, confirm these on a real board — they decide whether
the slot can power an actuator at all:

- **Which regulator actually feeds the slot.** This code assumes the 3.1 V rail is
  **LDO3** (GCA, from the 4IFIR tooling). Issue #2 reports it as **LDO2** — and in the
  MAX77620 maps we've seen, LDO2 is usually the microSD/SDMMC rail. The two disagree
  and **neither is confirmed for the cartridge slot**, so step 0 is to verify the
  LDO↔pin mapping (probe the rail while toggling each LDO).
- **Current limit ≈ 100 mA continuous** on the 3.1 V rail. A Taptic peak (>300 mA)
  will brown it out unless buffered → the supercap/Li-Po buffer is **mandatory**, not
  optional.
- **Possible 10 Ω series resistor** on the HDH-001 slot board's 3.1 V line (reported
  from teardown images). If real, that's ~1 V drop at 100 mA, so the actual voltage at
  the cart pins under load may be well below 3.1 V. **Measure pin voltage under a
  representative load** before sizing the supercap charge rate or assuming headroom.
- **Data rate is a non-issue:** the vibration payload is a few bytes per side at
  ~200 Hz (~kB/s), nowhere near any bus limit — no need to push clocks.
