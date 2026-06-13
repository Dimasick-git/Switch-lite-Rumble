# gc-power

Runtime control of the Switch **game-card power rail** via the MAX77620 PMIC over
I²C. This is the "power" half of the in-slot cartridge path: it lets a homebrew turn
on and set the voltage of the rail that feeds the cartridge slot, so an accessory
(our rumble actuator) can draw power there.

A homebrew voltage overlay was observed setting the **GCA** domain to **3100 mV** —
the cartridge VCC 3.1 V rail — which confirms the mapping:

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
