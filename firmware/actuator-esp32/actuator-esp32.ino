/*
 * actuator-esp32 — receiver/actuator side of the Switch-lite-Rumble project.
 *
 * Reads the 7-byte vibration frames defined in docs/PROTOCOL.md over USB-CDC serial
 * and drives two TI DRV2605L haptic drivers (left + right) in RTP (real-time
 * playback) mode, each driving an LRA / Taptic engine.
 *
 * The DRV2605L has a FIXED I2C address (0x5A), so two of them cannot share one bus.
 * We use the ESP32's two hardware I2C controllers: Wire (left) and Wire1 (right).
 *
 * Target: ESP32 / ESP32-S3 with the Arduino-ESP32 core. No external libraries.
 */

#include <Arduino.h>
#include <Wire.h>

// ---- wiring (change to taste) ---------------------------------------------
static const int kSdaL = 8,  kSclL = 9;    // left  DRV2605L (Wire)
static const int kSdaR = 10, kSclR = 11;   // right DRV2605L (Wire1)
static const uint8_t kDrvAddr = 0x5A;      // fixed for DRV2605L

// ---- protocol -------------------------------------------------------------
static const uint8_t kSync = 0xA5;
static const uint8_t kSideLeft = 0, kSideRight = 1, kSideStop = 0xFF;
static const uint32_t kFailsafeMs = 200;   // silence if no frame for this long

// ---- minimal DRV2605L RTP driver ------------------------------------------
struct Drv2605 {
    TwoWire *bus = nullptr;

    void writeReg(uint8_t reg, uint8_t val) {
        bus->beginTransmission(kDrvAddr);
        bus->write(reg);
        bus->write(val);
        bus->endTransmission();
    }

    bool begin(TwoWire *b) {
        bus = b;
        writeReg(0x01, 0x00);   // MODE: out of standby, internal trigger
        writeReg(0x1D, 0xA8);   // Library_Selection / control3: defaults
        writeReg(0x1A, 0xB6);   // Feedback control: N_ERM_LRA=1 (LRA mode) + defaults
        writeReg(0x03, 0x06);   // LIBRARY: LRA library (6)
        writeReg(0x02, 0x00);   // RTP input = 0 (silent)
        writeReg(0x01, 0x05);   // MODE: RTP (real-time playback)
        return true;
    }

    // amplitude 0..255 -> RTP register (unsigned)
    void drive(uint8_t amp) { writeReg(0x02, amp); }
};

static Drv2605 g_left, g_right;
static uint32_t g_last_frame_ms = 0;

// ---- frame parser (resync + checksum) -------------------------------------
static uint8_t g_buf[7];
static uint8_t g_have = 0;

static void apply_frame(const uint8_t *f) {
    const uint8_t side = f[1];
    if (side == kSideStop) {
        g_left.drive(0);
        g_right.drive(0);
        return;
    }
    const uint8_t amp_low  = f[2];
    const uint8_t amp_high = f[4];
    const uint8_t drive = amp_low > amp_high ? amp_low : amp_high;  // collapse bands
    if (side == kSideLeft)  g_left.drive(drive);
    if (side == kSideRight) g_right.drive(drive);
}

static void feed(uint8_t b) {
    if (g_have == 0) {
        if (b != kSync) return;          // wait for SYNC
        g_buf[g_have++] = b;
        return;
    }
    g_buf[g_have++] = b;
    if (g_have < 7) return;

    g_have = 0;                          // frame complete
    uint8_t x = 0;
    for (int i = 1; i <= 5; ++i) x ^= g_buf[i];
    if (x != g_buf[6]) return;           // bad checksum -> drop

    g_last_frame_ms = millis();
    apply_frame(g_buf);
}

// ---- Arduino entry points -------------------------------------------------
void setup() {
    Serial.begin(921600);
    Wire.begin(kSdaL, kSclL, 400000);
    Wire1.begin(kSdaR, kSclR, 400000);
    g_left.begin(&Wire);
    g_right.begin(&Wire1);
    g_last_frame_ms = millis();
}

void loop() {
    while (Serial.available() > 0) {
        feed((uint8_t)Serial.read());
    }
    // Failsafe: if the host went silent, ramp to 0 so the motor doesn't stick on.
    if (millis() - g_last_frame_ms > kFailsafeMs) {
        g_left.drive(0);
        g_right.drive(0);
        g_last_frame_ms = millis();      // re-arm so we don't spam the bus
    }
}
