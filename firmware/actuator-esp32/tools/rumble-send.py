#!/usr/bin/env python3
"""
rumble-send — host-side test sender for the actuator-esp32 firmware.

Sends docs/PROTOCOL.md frames over serial so you can bring up the hardware (ESP32 +
two DRV2605L + LRAs) WITHOUT a Switch in the loop.

Usage:
    python rumble-send.py /dev/ttyUSB0            # pulse L and R alternately
    python rumble-send.py COM5 --side 0 --amp 200 # hold left at amp 200

Needs pyserial:  pip install pyserial
"""
import argparse
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed:  pip install pyserial")

SYNC = 0xA5
SIDE_LEFT, SIDE_RIGHT, SIDE_STOP = 0, 1, 0xFF


def frame(side: int, amp_low: int, freq_low: int, amp_high: int, freq_high: int) -> bytes:
    body = bytes((side & 0xFF, amp_low & 0xFF, freq_low & 0xFF,
                  amp_high & 0xFF, freq_high & 0xFF))
    xsum = 0
    for b in body:
        xsum ^= b
    return bytes((SYNC,)) + body + bytes((xsum,))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="serial port (e.g. /dev/ttyUSB0 or COM5)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--side", type=int, default=None,
                    help="hold one side (0=L,1=R); default = alternating pulse demo")
    ap.add_argument("--amp", type=int, default=200, help="amplitude 0..255")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=1) as s:
        # freq bytes are advisory; ~160 Hz / 320 Hz -> /8
        fl, fh = 160 // 8, 320 // 8
        try:
            if args.side is not None:
                print(f"holding side {args.side} at amp {args.amp} (Ctrl-C to stop)")
                while True:
                    s.write(frame(args.side, args.amp, fl, args.amp, fh))
                    time.sleep(0.02)
            else:
                print("alternating L/R pulse demo (Ctrl-C to stop)")
                while True:
                    for side in (SIDE_LEFT, SIDE_RIGHT):
                        s.write(frame(side, args.amp, fl, args.amp, fh))  # on
                        time.sleep(0.15)
                        s.write(frame(side, 0, fl, 0, fh))                # off
                        time.sleep(0.10)
        except KeyboardInterrupt:
            s.write(frame(SIDE_STOP, 0, 0, 0, 0))
            print("\nstopped.")


if __name__ == "__main__":
    main()
