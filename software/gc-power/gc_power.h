/*
 * gc_power — runtime control of the Nintendo Switch *game-card* power rail.
 *
 * Narrow, project-specific subset of voltage-domain control: it exposes ONLY
 * the game-card (GC) regulator domains on the MAX77620 PMIC, so a homebrew
 * can provide power to a device in the cartridge slot (rumble accessory).
 *
 * IMPORTANT — LDO-to-slot mapping is NOT yet hardware-verified (see README):
 *
 *   GcPower_GCA     = MAX77620 LDO3 → game-card core rail (~3.1 V)  [4IFIR assumption]
 *   GcPower_GCA_ALT = MAX77620 LDO2 → alternative 3.1 V candidate   [issue #2 report]
 *   GcPower_GCC     = MAX77620 LDO5 → game-card I/O rail  (~1.8 V)  [4IFIR assumption]
 *
 * Step 0 of hardware bring-up: call GcPower_ReadSnapshot(), toggle each LDO
 * with Enable(), and probe slot pin 8 (VCC 3.1 V) with a multimeter to
 * confirm which domain actually feeds the physical pins. Then pass the
 * confirmed domain to GcPower_SlotEnable().
 *
 * --------------------------------------------------------------------------
 * Attribution: derived from runtime voltage-control code by Cooler3D
 * (4IFIR project). Used with permission; game-card domains only.
 * --------------------------------------------------------------------------
 */
#pragma once

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A controllable game-card power domain (LDO on the MAX77620). */
typedef struct {
    u8  cfg_reg;    /* LDOx_CFG: bits[5:0]=voltage, bits[7:6]=power mode */
    u8  volt_mask;  /* voltage field mask (0x3F for these LDOs)           */
    u32 uv_min;     /* voltage for multiplier 0, in microvolts            */
    u32 uv_step;    /* microvolts per step                                */
    u32 uv_max;     /* clamp ceiling, in microvolts                       */
} GcPowerDomain;

/*
 * Snapshot of all GC-relevant LDO voltages (mV). 0 means read error or
 * the LDO is disabled. Use GcPower_ReadSnapshot() + a multimeter on slot
 * pin 8 to determine which LDO actually feeds the cartridge rail.
 */
typedef struct {
    u32 gca_mv;      /* LDO3 voltage — current assumed GCA  */
    u32 gca_alt_mv;  /* LDO2 voltage — alt GCA (issue #2)   */
    u32 gcc_mv;      /* LDO5 voltage — GCC 1.8 V I/O rail   */
} GcPowerSnapshot;

extern const GcPowerDomain GcPower_GCA;      /* LDO3, ~3.1 V core (assumed) */
extern const GcPowerDomain GcPower_GCA_ALT;  /* LDO2, ~3.1 V core (alt)    */
extern const GcPowerDomain GcPower_GCC;      /* LDO5, ~1.8 V I/O           */

/* Read the current rail voltage (mV). Returns 0 on error. */
u32    GcPower_GetMv(const GcPowerDomain *domain);

/* Set the rail voltage (mV), clamped to the domain range. */
Result GcPower_SetMv(const GcPowerDomain *domain, u32 mv);

/* Enable (true) or disable (false) the rail (power mode NORMAL / DISABLE). */
Result GcPower_Enable(const GcPowerDomain *domain, bool enable);

/*
 * Read LDO2, LDO3 and LDO5 voltages in one call.
 *
 * Use for hardware verification: call this, then toggle GcPower_GCA and
 * GcPower_GCA_ALT one at a time while probing slot pin 8 (VCC 3.1 V) with
 * a multimeter. The LDO whose enable/disable makes pin 8 appear/disappear
 * is the one feeding the slot.
 */
Result GcPower_ReadSnapshot(GcPowerSnapshot *out);

/*
 * Sequenced slot power control — safe enable/disable with soft-start.
 *
 *   pwr_3v — domain driving the 3.1 V slot rail; pass &GcPower_GCA or
 *            &GcPower_GCA_ALT once you have confirmed which LDO feeds
 *            the slot (see GcPower_ReadSnapshot).
 *
 * On enable:
 *   1. GCC (1.8 V logic) up first — logic rail before power rail.
 *   2. pwr_3v at 2.8 V (soft-start) — limits inrush into uncharged supercap.
 *   3. 10 ms settle, then ramp pwr_3v to 3.1 V.
 *   4. 20 ms window for the supercap to begin charging.
 *
 * On disable:
 *   1. pwr_3v (3.1 V) off first.
 *   2. 5 ms settle, then GCC (1.8 V) off.
 */
Result GcPower_SlotEnable(const GcPowerDomain *pwr_3v, bool enable);

#ifdef __cplusplus
}
#endif
