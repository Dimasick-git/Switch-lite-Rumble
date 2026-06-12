# rumble-tap-sysmodule

Milestone-1 PoC background sysmodule. Brings up the `hid` vibration API and proves
the read/output loop: polls input and, on **L + R**, logs a line and sends a short
vibration burst to a connected vibration-capable controller.

> **Scaffold — not yet compiled or run on hardware.** Written to libnx sysmodule
> conventions as a starting point. See [`../DEPENDENCIES.md`](../DEPENDENCIES.md) for
> the toolchain.

## Build

```sh
export DEVKITPRO=/opt/devkitpro
make
```

Produces `rumble-tap-sysmodule.nsp` (NSO + NPDM).

## Install (Atmosphère)

1. Copy the built sysmodule to the SD card under the program ID from
   `rumble-tap.json`:
   ```
   /atmosphere/contents/0100000000ABE100/exefs.nsp   (or exefs/main + main.npdm)
   ```
2. Enable autostart by creating an empty boot flag:
   ```
   /atmosphere/contents/0100000000ABE100/flags/boot2.flag
   ```
3. Reboot. Read `svcOutputDebugString` logs over a debug channel.

## Notes / next

- The `title_id` in `rumble-tap.json` (`0100000000ABE100`) is a **placeholder** in
  the system-module range — pick a final unused ID before release.
- This PoC uses plain **libnx**. The real signal we want (a *game's* outgoing
  vibration) requires a **libstratosphere MITM of `hid`** — milestone 2. The button
  trigger here is a stand-in so milestone 1 is testable on its own.
- Verified `hid` command IDs for the MITM step: `SendVibrationValue` = 201,
  `SendVibrationValues` = 206 (see `../DEPENDENCIES.md`).
