/*
 * gc_power — see gc_power.h.
 *
 * Attribution: game-card voltage-domain control derived from runtime code by
 * Cooler3D (4IFIR project). Game-card power domains only; audio/EQ and unrelated
 * domains intentionally omitted.
 */
#include "gc_power.h"

/* --- MAX77620 register map (publicly documented; switchbrew / hekate) ------ */
#define MAX77620_REG_LDO3_CFG          0x2B   /* GCA: core rail */
#define MAX77620_REG_LDO5_CFG          0x2F   /* GCC: I/O rail  */

#define MAX77620_LDO_VOLT_MASK         0x3F   /* bits [5:0] */
#define MAX77620_LDO_POWER_MODE_MASK   0xC0   /* bits [7:6] */
#define MAX77620_LDO_POWER_MODE_SHIFT  6
#define MAX77620_POWER_MODE_NORMAL     3
#define MAX77620_POWER_MODE_DISABLE    0

/* GCA = LDO3 (game-card core, ~3.1 V). 50 mV step is required to reach 3.1 V
 * within a 6-bit field, matching the observed 3100 mV setting. */
const GcPowerDomain GcPower_GCA = {
    .cfg_reg   = MAX77620_REG_LDO3_CFG,
    .volt_mask = MAX77620_LDO_VOLT_MASK,
    .uv_min    = 800000,
    .uv_step   = 50000,
    .uv_max    = 3200000,
};

/* GCC = LDO5 (game-card I/O, ~1.8 V). */
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
