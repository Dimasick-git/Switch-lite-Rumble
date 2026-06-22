# fs-lotus3-patch

Atmosphere IPS patches for the Switch `fs` service that prevent Lotus3 from
cutting the cartridge slot power rails when no authentic game card responds.

This is the software side of the slot power path. With these patches active,
the 3.1V and 1.8V rails stay on after insertion, giving the rumble accessory
power without an authentic cartridge being present.

For the full technical explanation see [`../../docs/FS-LOTUS3.md`](../../docs/FS-LOTUS3.md).

---

## Quick start

### 1. Extract and decompress the fs binary for your HOS version

```bash
# on-console: nxdumptool -> System titles -> fs
# then on PC:
nso2elf fs.nso fs_decompressed.bin   # decompress NSO
```

### 2. Find offsets

```bash
python3 tools/find_offsets.py fs_decompressed.bin
```

Output lists candidate offsets for each of the three patch sites.
Cross-check one with Ghidra/Binary Ninja before committing.

### 3. Record offsets

```bash
cp offsets/template.json offsets/<your_version>.json
# edit the three offset fields
```

### 4. Generate the IPS file

```bash
python3 tools/mk_ips.py offsets/<your_version>.json patch.ips
```

### 5. Deploy

```
sdmc:/atmosphere/exefs_patches/fs/<BUILD_ID>/patch.ips
```

The build ID is the first 8 bytes of the NSO build ID (offset 0x40 in the raw
`.nso` file), uppercase hex. `mk_ips.py` prints the expected path.

---

## Directory layout

```
fs-lotus3-patch/
  tools/
    find_offsets.py   pattern-based offset finder (decompressed NSO input)
    mk_ips.py         IPS file generator from offsets JSON
  offsets/
    template.json     annotated template — copy and fill per HOS version
    18.1.0.json       HOS 18.1.0 — offsets TBD (needs binary)
  patches/            pre-built .ips files go here once offsets are confirmed
  README.md           this file
```

---

## Status

| HOS version | Offsets | IPS built | Tested on HW |
| :--- | :--- | :--- | :--- |
| 18.1.0 | TBD | ✗ | ✗ |

To add a version: run `find_offsets.py`, fill `offsets/<ver>.json`, run `mk_ips.py`,
test on hardware, update this table, drop the `.ips` in `patches/`.

---

## Safety

- These patches disable three safety branches in `fs`. The console will no longer
  cut cartridge rail power on auth failure. **Do not use on a console with a real
  game inserted** — the official game-card driver will be stuck in error state.
- The Lotus3 ASIC may have a hardware power timeout independent of fs (see
  `docs/FS-LOTUS3.md §4`). If so, the rails pulse rather than stay on, and this
  patch alone is insufficient — hardware emulation of the Lotus3 init is then
  required.
- Always keep a stock Atmosphere backup. To remove, delete the patch from the
  `exefs_patches/fs/` directory and reboot.
