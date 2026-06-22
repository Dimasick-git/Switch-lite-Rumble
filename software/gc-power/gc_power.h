/*
 * gc_power — runtime control of the Nintendo Switch *game-card* power rail.
 *
 * Narrow, project-specific subset of voltage-domain control: it exposes ONLY the
 * two game-card (GC) regulator domains on the MAX77620 PMIC, so a homebrew can
 * provide power to a device in the cartridge slot (here: a rumble accessory).
 *
 * An overlay was seen setting the "GCA" domain to 3100 mV (matching the cartridge
 * VCC 3.1V rail). The exact regulator->slot mapping is NOT yet hardware-verified
 * (issue #2 reports LDO2 for the 3.1V rail, not LDO3) — confirm on hardware before
 * relying on it. See README "Power budget — verify on hardware".
 *
 *   GCA = MAX77620 LDO3  -> game-card core rail (~3.1 V)   [assumed, unverified]
 *   GCC = MAX77620 LDO5  -> game-card I/O rail  (~1.8 V)   [assumed, unverified]
 *
 * --------------------------------------------------------------------------
 * Attribution: derived from runtime voltage-control code by **Cooler3D**
 * (from the 4IFIR project). Used here with permission, for the game-card
 * power domains only; the rest of the author's work is not part of this repo.
 *
 * This controls the GC voltage rail over I2C — it's the power side of the slot,
 * separate from Lotus3 init/handshake.
 * --------------------------------------------------------------------------
 */
#pragma once

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A controllable game-card power domain (LDO on the MAX77620). */
typedef struct {
    u8  cfg_reg;     /* LDOx_CFG: bits[5:0]=voltage, bits[7:6]=power mode */
    u8  volt_mask;   /* voltage field mask (0x3F for these LDOs)          */
    u32 uv_min;      /* voltage for multiplier 0, in microvolts           */
    u32 uv_step;     /* microvolts per step                               */
    u32 uv_max;      /* clamp ceiling, in microvolts                      */
} GcPowerDomain;

/* GCA = LDO3 -> game-card core (~3.1 V). GCC = LDO5 -> game-card I/O (~1.8 V).
 * Domain parameters as provided by the source; verify against your unit. */
extern const GcPowerDomain GcPower_GCA;  /* core rail, up to 3.2 V */
extern const GcPowerDomain GcPower_GCC;  /* I/O rail,  up to 1.9 V */

/* Read the current rail voltage (mV). Returns 0 on error. */
u32    GcPower_GetMv(const GcPowerDomain *domain);

/* Set the rail voltage (mV), clamped to the domain range. */
Result GcPower_SetMv(const GcPowerDomain *domain, u32 mv);

/* Enable (true) or disable (false) the rail (power mode NORMAL / DISABLE). */
Result GcPower_Enable(const GcPowerDomain *domain, bool enable);

#ifdef __cplusplus
}
#endif
