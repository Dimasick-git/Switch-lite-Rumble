# gc-power

Runtime control of the Switch **game-card power rail** via the MAX77620 PMIC over
I²C. This is the power half of the in-slot cartridge path: it lets a homebrew turn
on and set the voltage of the rail that feeds the cartridge slot so an accessory
(our rumble actuator) can draw power there.

---

## LDO mapping — NOT yet verified on hardware

Two candidate mappings exist for the 3.1 V slot rail. **Neither is confirmed** for the
HDH-001 board — see the [Verification workflow](#verification-workflow) below.

| Symbol | Regulator | Register | Rail | Source |
| :--- | :--- | :--- | :--- | :--- |
| `GcPower_GCA` | MAX77620 LDO3 | `0x2B` | Game-card core ~3.1 V | 4IFIR overlay (assumed) |
| `GcPower_GCA_ALT` | MAX77620 LDO2 | `0x29` | Alt 3.1 V candidate | Issue #2 report |
| `GcPower_GCC` | MAX77620 LDO5 | `0x2F` | Game-card I/O ~1.8 V | 4IFIR overlay (assumed) |

In other Switch models, LDO2 is often the microSD/SDMMC rail — the HDH-001 layout
may differ. Do **not** enable GCA and GCA_ALT simultaneously.

---

## API

```c
/* primitives */
u32    GcPower_GetMv(const GcPowerDomain *domain);         // read mV (0 = error)
Result GcPower_SetMv(const GcPowerDomain *domain, u32 mv); // set mV, clamped
Result GcPower_Enable(const GcPowerDomain *domain, bool);  // on / off

/* verification helper */
Result GcPower_ReadSnapshot(GcPowerSnapshot *out);         // read LDO2+3+5 at once

/* sequenced slot control */
Result GcPower_SlotEnable(const GcPowerDomain *pwr_3v, bool enable);
```

### GcPower_ReadSnapshot

Fills a `GcPowerSnapshot`:

```c
typedef struct {
    u32 gca_mv;      // LDO3 voltage
    u32 gca_alt_mv;  // LDO2 voltage
    u32 gcc_mv;      // LDO5 voltage
} GcPowerSnapshot;
```

Designed for the hardware verification step: read snapshot → toggle each LDO →
probe slot pin 8 with a multimeter → the LDO that makes pin 8 appear/disappear
is the one feeding the slot.

### GcPower_SlotEnable

Sequenced enable/disable with soft-start. Pass `&GcPower_GCA` or
`&GcPower_GCA_ALT` once you know which LDO feeds the slot.

**Enable sequence:**
1. GCC (1.8 V logic) up first — logic before power.
2. `pwr_3v` soft-start at **2.8 V** — limits inrush into uncharged supercap.
3. 10 ms settle, then ramp `pwr_3v` to **3.1 V**.
4. 20 ms window for supercap initial-charge.

**Disable sequence:**
1. `pwr_3v` (3.1 V) off.
2. 5 ms settle, then GCC (1.8 V) off.

The soft-start step is mandatory given the 100 mA LDO limit and the possible 10 Ω
series resistor on the HDH-001 slot board (see power budget below).

---

## Verification workflow

Before relying on any of this in hardware, confirm:

**Step 0 — which LDO feeds slot pin 8 (VCC 3.1 V)**
```c
GcPowerSnapshot snap;
GcPower_ReadSnapshot(&snap);
// log snap.gca_mv and snap.gca_alt_mv
// probe pin 8 with a multimeter while toggling GCA then GCA_ALT
```
The LDO that makes pin 8 appear/disappear is the right one. Update your
`GcPower_SlotEnable()` call accordingly.

**Step 1 — current limit**
The 3.1 V rail (whichever LDO feeds it) is rated ~100 mA continuous on the Lite.
A Taptic peak (>300 mA) will brown it out — the supercap buffer is **mandatory**.

**Step 2 — series resistor**
The HDH-001 slot board reportedly has a 10 Ω resistor on the 3.1 V line. At 100 mA
that's a 1 V drop, so the **actual voltage at the cart pins under load** may be
well below 3.1 V. Measure pin 8 under a representative load before sizing the
supercap charge rate or assuming voltage headroom.

---

## Integration

Self-contained on **libnx** (`i2c` service). To use from a sysmodule, add `i2c`
service access in the process NPDM and call the API after `i2cInitialize()`.

Typical usage once the correct LDO is confirmed:

```c
i2cInitialize();
// enable the slot with verified domain:
GcPower_SlotEnable(&GcPower_GCA /* or GCA_ALT */, true);
// ... actuator active ...
GcPower_SlotEnable(&GcPower_GCA, false);
i2cExit();
```

---

## Attribution

Voltage-domain control logic derived from runtime code by **Cooler3D**
([4IFIR](https://github.com/rashevskyv/4IFIR)), used with permission.
Only the game-card power domains are included here.

---

## Scope / safety

- This controls the GC voltage rail over I2C — the power side of the slot,
  separate from Lotus3 init / handshake.
- Wrong voltages or fighting the system's own power management can be
  destabilising. Test carefully; never enable GCA and GCA_ALT simultaneously.
- The open question — whether enabling this LDO powers the physical slot pins
  or only Lotus3 — is answered by the verification workflow above, not in code.
