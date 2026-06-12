# MIG Dumper & Flashcart — reference PCB files

These are the KiCad design files, schematics, boardviews and BOMs for the **MIG
Dumper** and **MIG Flashcart**, kept here as **reference / prior art** for the
Switch-lite-Rumble project. The MIG Flashcart is the closest existing device to
what this project needs: a board that fits the Switch game-card envelope and that
speaks the Lotus3 cartridge protocol with an FPGA + microcontroller.

## Attribution

- **Original author:** sabogalc
- **Original repository:** https://github.com/sabogalc/MIG-Flash-PCBs
- **Original license:** WTFPL (Do What The F*** You Want To Public License, v2)

All credit for these designs goes to the original author. They are reproduced here
under the terms of the WTFPL purely as engineering reference.

## Important caveat (from the original author)

These board files **cannot** be turned into a working device from scratch. The FPGA
and microcontroller on the real MIG boards are programmed **and encrypted at the
factory**, so the only way to get a functioning board is to **transplant the FPGA
and the MCU** from an OEM MIG device onto a board built from these files. The value
here is understanding the circuit, not cloning the product.

## What's in here

| Path | Contents |
| :--- | :--- |
| `KiCad Projects/MIG Flashcart/` | Schematic, PCB layout, footprints (`.pretty`), STEP models, gerbers, BOMs (DigiKey/Mouser CSV+XLS), interactive BOM, PDF schematic |
| `KiCad Projects/MIG Dumper/` | Same set for the Dumper board |
| `Boardview Files/` | `.bvr`/`.obdata` boardview netlists for both boards (view with OpenBoardView or FlexBV) |

Each subfolder keeps the original author's own README with detailed build notes
(board thickness, finish, component substitutions, programmer-board schematic, etc).

## What was removed during import

To keep the repo lean (the upstream repo is ~70 MB, mostly throwaway data), the
following regenerable / non-essential files were **not** imported:

- `*-backups/` — dozens of KiCad autosave `.zip` snapshots (~40 MB total)
- `fp-info-cache` — regenerable footprint cache (~3.5 MB each)
- `*.bak` — KiCad backup files

Everything needed to open, inspect, fabricate, or study the boards is retained.
The import brought the footprint down from ~70 MB to under 8 MB.

## Key takeaways for this project

- The **MIG Flashcart** is built as a **0.8 mm board with ENIG finish** to fit the
  game enclosure — a hard data point for our own form factor.
- It uses **0201 passives** and **0.2 mm vias**; hand-assembly is difficult. Expect
  the same density constraints for a slot-fit Rumble board.
- The cartridge interface is the **Lotus3** ASIC protocol (made public via the July
  2021 leaks); the FPGA + MCU are programmed to speak it.
- The BOMs are a ready-made parts reference for the passive network and connectors
  around an FPGA/MCU cartridge front-end.
