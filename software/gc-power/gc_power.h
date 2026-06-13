/*
 * gc_power — runtime control of the Nintendo Switch *game-card* power rail.
 *
 * Narrow, project-specific subset of voltage-domain control: it exposes ONLY the
 * two game-card (GC) regulator domains on the MAX77620 PMIC, so a homebrew can
 * provide power to a device in the cartridge slot (here: a rumble accessory).
 *
 * The photo/overlay that motivated this shows the "GCA" domain set to 3100 mV —
 * i.e. the cartridge VCC 3.1V rail — confirming GCA == game-card core power.
 *
 *   GCA = MAX77620 LDO3  -> game-card core rail (~3.1 V)
 *   GCC = MAX77620 LDO5  -> game-card I/O rail  (~1.8 V)
 *
 * --------------------------------------------------------------------------
 * Attribution: derived from runtime voltage-control code by **Cooler3D**
 * (from the 4IFIR project). Used here with permission, for the game-card
 * power domains only; the rest of the author's work is not part of this repo.
 *
 * Scope note: this only controls an electrical power rail. It does NOT
 * authenticate a cartridge, does NOT touch the Lotus3 challenge-response, and
 * does NOT enable reading game content — it just powers the slot for an
 * accessory.
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
