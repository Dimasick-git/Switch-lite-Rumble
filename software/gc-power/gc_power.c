/*
 * gc_power — see gc_power.h.
 *
 * Attribution: game-card voltage-domain control derived from runtime code by
 * Cooler3D (4IFIR project). Game-card power domains only; audio/EQ and
 * unrelated domains intentionally omitted.
 */
#include "gc_power.h"

/* --- MAX77620 register map (publicly documented; switchbrew / hekate) ------ */

/*
 * LDO register addresses follow a 2-byte stride from a base of 0x25:
 *   LDO0=0x25, LDO1=0x27, LDO2=0x29, LDO3=0x2B, LDO4=0x2D, LDO5=0x2F
 *
 * LDO2 (0x29) is reported in issue #2 as a candidate for the 3.1 V slot
 * rail on the HDH-001 board. LDO3 (0x2B) is the assumption from the 4IFIR
 * overlay. Neither is confirmed for the Lite — verify on hardware.
 */
#define MAX77620_REG_LDO2_CFG          0x29   /* alt 3.1 V candidate (issue #2) */
#define MAX77620_REG_LDO3_CFG          0x2B   /* GCA: assumed core rail         */
#define MAX77620_REG_LDO5_CFG          0x2F   /* GCC: I/O rail                  */

#define MAX77620_LDO_VOLT_MASK         0x3F   /* bits [5:0] */
#define MAX77620_LDO_POWER_MODE_MASK   0xC0   /* bits [7:6] */
#define MAX77620_LDO_POWER_MODE_SHIFT  6
#define MAX77620_POWER_MODE_NORMAL     3
#define MAX77620_POWER_MODE_DISABLE    0

/* GCA = LDO3 (game-card core, ~3.1 V). Assumption from 4IFIR; unverified. */
const GcPowerDomain GcPower_GCA = {
    .cfg_reg   = MAX77620_REG_LDO3_CFG,
    .volt_mask = MAX77620_LDO_VOLT_MASK,
    .uv_min    = 800000,
    .uv_step   = 50000,
    .uv_max    = 3200000,
};

/*
 * GCA_ALT = LDO2 (alternative 3.1 V candidate from issue #2).
 * In other Switch models LDO2 is often the microSD/SDMMC rail; the HDH-001
 * board layout may differ. Do NOT enable both GCA and GCA_ALT simultaneously.
 */
const GcPowerDomain GcPower_GCA_ALT = {
    .cfg_reg   = MAX77620_REG_LDO2_CFG,
    .volt_mask = MAX77620_LDO_VOLT_MASK,
    .uv_min    = 800000,
    .uv_step   = 50000,
    .uv_max    = 3200000,
};

/* GCC = LDO5 (game-card I/O, ~1.8 V). Assumption from 4IFIR; unverified. */
const GcPowerDomain GcPower_GCC = {
    .cfg_reg   = MAX77620_REG_LDO5_CFG,
    .volt_mask = MAX77620_LDO_VOLT_MASK,
    .uv_min    = 800000,
    .uv_step   = 50000,
    .uv_max    = 1900000,
};

/* --- tiny I2C helpers (libnx i2c service) --------------------------------- */

static Result I2cWriteReg(I2cDevice dev, u8 reg, u8 val) {
    struct { u8 reg; u8 val; } __attribute__((packed)) cmd = { reg, val };

    I2cSession s;
    Result rc = i2cOpenSession(&s, dev);
    if (R_FAILED(rc))
        return rc;
    rc = i2csessionSendAuto(&s, &cmd, sizeof(cmd), I2cTransactionOption_All);
    i2csessionClose(&s);
    return rc;
}

static Result I2cReadReg(I2cDevice dev, u8 reg, u8 *out) {
    struct {
        u8 send;
        u8 send_len;
        u8 reg;
        u8 receive;
        u8 receive_len;
    } __attribute__((packed)) cmd = {
        .send        = 0 | (I2cTransactionOption_Start << 6),
        .send_len    = 1,
        .reg         = reg,
        .receive     = 1 | (I2cTransactionOption_All << 6),
        .receive_len = 1,
    };

    I2cSession s;
    Result rc = i2cOpenSession(&s, dev);
    if (R_FAILED(rc))
        return rc;
    rc = i2csessionExecuteCommandList(&s, out, 1, &cmd, sizeof(cmd));
    i2csessionClose(&s);
    return rc;
}

/* --- voltage conversion --------------------------------------------------- */

static u8 mv_to_multiplier(const GcPowerDomain *d, u32 mv) {
    u32 uv = mv * 1000u;
    if (uv < d->uv_min) uv = d->uv_min;
    if (uv > d->uv_max) uv = d->uv_max;
    return (u8)((uv - d->uv_min) / d->uv_step);
}

static u32 multiplier_to_mv(const GcPowerDomain *d, u8 mult) {
    return (d->uv_min + d->uv_step * mult) / 1000u;
}

/* --- public API ----------------------------------------------------------- */

u32 GcPower_GetMv(const GcPowerDomain *domain) {
    u8 val;
    if (R_FAILED(I2cReadReg(I2cDevice_Max77620Pmic, domain->cfg_reg, &val)))
        return 0u;
    return multiplier_to_mv(domain, val & domain->volt_mask);
}

Result GcPower_SetMv(const GcPowerDomain *domain, u32 mv) {
    u8 val;
    Result rc = I2cReadReg(I2cDevice_Max77620Pmic, domain->cfg_reg, &val);
    if (R_FAILED(rc))
        return rc;

    val &= ~domain->volt_mask;
    val |= mv_to_multiplier(domain, mv) & domain->volt_mask;

    rc = I2cWriteReg(I2cDevice_Max77620Pmic, domain->cfg_reg, val);
    if (R_FAILED(rc))
        return rc;

    svcSleepThread(5000000ULL); /* 5 ms ramp settle */
    return 0;
}

Result GcPower_Enable(const GcPowerDomain *domain, bool enable) {
    u8 val;
    Result rc = I2cReadReg(I2cDevice_Max77620Pmic, domain->cfg_reg, &val);
    if (R_FAILED(rc))
        return rc;

    val &= ~MAX77620_LDO_POWER_MODE_MASK;
    val |= (enable ? MAX77620_POWER_MODE_NORMAL : MAX77620_POWER_MODE_DISABLE)
           << MAX77620_LDO_POWER_MODE_SHIFT;

    rc = I2cWriteReg(I2cDevice_Max77620Pmic, domain->cfg_reg, val);
    if (R_FAILED(rc))
        return rc;

    svcSleepThread(5000000ULL); /* 5 ms settle */
    return 0;
}

Result GcPower_ReadSnapshot(GcPowerSnapshot *out) {
    out->gca_mv     = GcPower_GetMv(&GcPower_GCA);
    out->gca_alt_mv = GcPower_GetMv(&GcPower_GCA_ALT);
    out->gcc_mv     = GcPower_GetMv(&GcPower_GCC);
    return 0;
}

Result GcPower_SlotEnable(const GcPowerDomain *pwr_3v, bool enable) {
    Result rc;

    if (enable) {
        /* Step 1: bring up 1.8 V logic rail before the power rail. */
        rc = GcPower_SetMv(&GcPower_GCC, 1800);
        if (R_FAILED(rc)) return rc;
        rc = GcPower_Enable(&GcPower_GCC, true);
        if (R_FAILED(rc)) return rc;

        /*
         * Step 2: soft-start the 3.1 V rail at 2.8 V.
         * Starting at full voltage into an uncharged supercap would pull
         * close to the 100 mA LDO limit immediately. 2.8 V limits the
         * initial inrush while the cap begins charging.
         */
        rc = GcPower_SetMv(pwr_3v, 2800);
        if (R_FAILED(rc)) return rc;
        rc = GcPower_Enable(pwr_3v, true);
        if (R_FAILED(rc)) return rc;
        svcSleepThread(10000000ULL); /* 10 ms: LDO stable, inrush settled */

        /* Step 3: ramp to nominal 3.1 V and allow supercap to charge. */
        rc = GcPower_SetMv(pwr_3v, 3100);
        if (R_FAILED(rc)) return rc;
        svcSleepThread(20000000ULL); /* 20 ms: supercap initial-charge window */
    } else {
        /* Kill 3.1 V first, then logic rail. */
        rc = GcPower_Enable(pwr_3v, false);
        if (R_FAILED(rc)) return rc;
        svcSleepThread(5000000ULL);
        rc = GcPower_Enable(&GcPower_GCC, false);
        if (R_FAILED(rc)) return rc;
    }

    return 0;
}
