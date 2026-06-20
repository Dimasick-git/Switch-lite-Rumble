# actuator-esp32

The receiver/actuator side of the project: an ESP32 that reads the vibration frames
([`../../docs/PROTOCOL.md`](../../docs/PROTOCOL.md)) over USB-CDC serial and drives
**two DRV2605L** haptic drivers (left + right) in RTP mode, each on its own LRA /
Taptic engine.

This pairs with the Switch-side [`rumble-logger-mitm`](../../software/rumble-logger-mitm/):
the sysmodule captures and splits rumble per side, the forwarder emits frames, this
firmware plays them. You can also drive it standalone with the host test sender below.

> **Why two I2C buses:** the DRV2605L's I2C address is fixed at `0x5A`, so two can't
> share one bus. The firmware uses the ESP32's two controllers — `Wire` (left) and
> `Wire1` (right).

## Wiring (defaults, edit in the `.ino`)

| Signal | Left (Wire) | Right (Wire1) |
| :--- | :--- | :--- |
| SDA | GPIO 8 | GPIO 10 |
| SCL | GPIO 9 | GPIO 11 |
| DRV2605L addr | 0x5A | 0x5A |

Each DRV2605L drives one LRA (e.g. a coin LRA for the prototype, an iPhone-12 Taptic
for the final). Power the motors from a buffered rail (supercap/Li-Po) per
[`../../docs/HAPTICS.md`](../../docs/HAPTICS.md).

## Build & flash

Arduino-ESP32 core, no external libraries. With **arduino-cli**:

```sh
arduino-cli core install esp32:esp32
arduino-cli compile  -b esp32:esp32:esp32s3 .
arduino-cli upload   -b esp32:esp32:esp32s3 -p /dev/ttyACM0 .
```

(or open `actuator-esp32.ino` in the Arduino IDE and pick your ESP32 board.)

## Test it without a Switch

```sh
pip install pyserial
python tools/rumble-send.py /dev/ttyUSB0          # alternating L/R pulse demo
python tools/rumble-send.py /dev/ttyUSB0 --side 0 --amp 220   # hold left
```

If the LRAs buzz L then R, the actuator side works — then connect the Switch forwarder.

## Notes

- v1 collapses the two HD-rumble bands to one drive value (`max(amp_low, amp_high)`)
  and lets the DRV2605L hold the LRA at resonance. Frequency bytes are advisory for
  now (see PROTOCOL.md).
- Failsafe: if no frame arrives for ~200 ms the firmware ramps both motors to 0, so a
  dropped link can't leave a motor stuck on.
