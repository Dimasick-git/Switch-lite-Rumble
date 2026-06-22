#!/usr/bin/env python3
"""
find_offsets.py -- locate fs power-gate patch sites by byte-pattern search.

Usage:
    python3 find_offsets.py <fs_decompressed.bin> [--verbose]

Input: a decompressed fs NSO flat binary.  Decompress with nso2elf or nx2elf.
The tool strips the 0x100-byte NSO header automatically if the magic is present.

Outputs candidate .text offsets (suitable for use in offsets/*.json) for each
of the three power-gate branch sites in nn::fs::detail::IDeviceOperator:

  Site 1 -- IsGameCardInserted : CBZ/CBNZ after the polling call
  Site 2 -- GetGameCardHandle  : CBNZ X0 after handle-acquisition BL
  Site 3 -- GetGameCardAttribute: CBNZ X0 after attribute-check BL

Each site is identified by searching for a BL immediately followed by a
CBZ/CBNZ that branches to an error/power-cut path.  The instruction to NOP
is always the CBZ/CBNZ (4 bytes after the BL match start).

Expect 10-30 candidates per pattern; cross-check the short list with a
disassembler (Ghidra / Binary Ninja).  The target is the site whose error
branch leads to a PowerOff / LDO-disable call chain.
"""
import sys
import struct
from pathlib import Path

NSO_MAGIC = b"NSO0"
NSO_HEADER_SIZE = 0x100
NOP_BYTES = bytes.fromhex("1F2003D5")


# ---------------------------------------------------------------------------
# ARM64 instruction decoders
# ---------------------------------------------------------------------------

def _word(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]

def is_bl(w: int) -> bool:
    """BL <imm26>: bits[31:26] == 0b100101"""
    return (w & 0xFC000000) == 0x94000000

def is_cbz_64(w: int, reg: int = None) -> bool:
    """CBZ Xn: bits[31:24]=0xB4, bits[4:0]=reg (None=any)"""
    if (w & 0xFF000000) != 0xB4000000:
        return False
    return reg is None or (w & 0x1F) == reg

def is_cbnz_64(w: int, reg: int = None) -> bool:
    """CBNZ Xn: bits[31:24]=0xB5"""
    if (w & 0xFF000000) != 0xB5000000:
        return False
    return reg is None or (w & 0x1F) == reg

def is_cbz_32(w: int, reg: int = None) -> bool:
    """CBZ Wn: bits[31:24]=0x34"""
    if (w & 0xFF000000) != 0x34000000:
        return False
    return reg is None or (w & 0x1F) == reg

def is_cbnz_32(w: int, reg: int = None) -> bool:
    """CBNZ Wn: bits[31:24]=0x35"""
    if (w & 0xFF000000) != 0x35000000:
        return False
    return reg is None or (w & 0x1F) == reg

def is_any_cbz_cbnz(w: int) -> bool:
    return (is_cbz_64(w) or is_cbnz_64(w) or
            is_cbz_32(w) or is_cbnz_32(w))

def branch_target(base_offset: int, w: int) -> int:
    """Decode the branch target offset from a CBZ/CBNZ instruction."""
    imm19 = (w >> 5) & 0x7FFFF
    if imm19 & 0x40000:            # sign-extend
        imm19 |= ~0x7FFFF
    return base_offset + imm19 * 4

def disasm_brief(w: int, off: int) -> str:
    """One-line human-readable for BL / CBZ / CBNZ."""
    if is_bl(w):
        imm26 = w & 0x3FFFFFF
        if imm26 & 0x2000000:
            imm26 |= ~0x3FFFFFF
        target = off + imm26 * 4
        return f"BL   0x{target & 0xFFFFFFFF:08X}"
    for fn, mnem in [
        (is_cbz_64,  "CBZ  X"),
        (is_cbnz_64, "CBNZ X"),
        (is_cbz_32,  "CBZ  W"),
        (is_cbnz_32, "CBNZ W"),
    ]:
        if fn(w):
            reg = w & 0x1F
            tgt = branch_target(off, w)
            return f"{mnem}{reg}, 0x{tgt & 0xFFFFFFFF:08X}"
    return f"??   0x{w:08X}"


# ---------------------------------------------------------------------------
# Search
# ---------------------------------------------------------------------------

class Site:
    def __init__(self, name, desc, bl_before, cbz_reg=None, want_x64=True):
        self.name = name
        self.desc = desc
        self.bl_before = bl_before   # True = BL must precede the CBZ
        self.cbz_reg = cbz_reg       # None = any register
        self.want_x64 = want_x64     # True = 64-bit Xn; False = 32-bit Wn


SITES = [
    Site(
        "IsGameCardInserted_false_branch",
        "CBZ/CBNZ Wn after polling BL -- returns false, cuts rails",
        bl_before=True,
        cbz_reg=0,
        want_x64=False,   # bool return in W0
    ),
    Site(
        "GetGameCardHandle_power_gate_branch",
        "CBNZ X0 after handle-acquisition BL -- Result != 0, cuts rails",
        bl_before=True,
        cbz_reg=0,
        want_x64=True,    # Result in X0
    ),
    Site(
        "GetGameCardAttribute_fail_branch",
        "CBNZ X0 after attribute-check BL -- bad card type, cuts rails",
        bl_before=True,
        cbz_reg=0,
        want_x64=True,
    ),
]


def search_site(text: bytes, site: Site, verbose: bool) -> list:
    """
    Find all BL+CBZ/CBNZ pairs where:
      - word[i]   is a BL instruction
      - word[i+1] is CBZ/CBNZ on the expected register / size
    Returns list of (text_offset_of_cbz, bl_word, cbz_word).
    """
    hits = []
    n = len(text) // 4
    for i in range(n - 1):
        off_bl  = i * 4
        off_cbz = off_bl + 4
        wbl  = _word(text, off_bl)
        wcbz = _word(text, off_cbz)

        if not is_bl(wbl):
            continue

        matched = False
        if site.want_x64:
            if is_cbnz_64(wcbz, site.cbz_reg) or is_cbz_64(wcbz, site.cbz_reg):
                matched = True
        else:
            if is_cbnz_32(wcbz, site.cbz_reg) or is_cbz_32(wcbz, site.cbz_reg):
                matched = True

        if matched:
            hits.append((off_cbz, wbl, wcbz))

    return hits


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("-")]

    if not args:
        print(__doc__)
        sys.exit(1)

    path = Path(args[0])
    raw = path.read_bytes()

    # Strip NSO header if present
    if raw[:4] == NSO_MAGIC:
        text = raw[NSO_HEADER_SIZE:]
        print(f"NSO header detected; .text starts at file offset 0x{NSO_HEADER_SIZE:X}")
    else:
        text = raw
        print("No NSO magic; treating entire file as .text")

    print(f"Searching {path.name} ({len(text):,} bytes)\n")
    print("NOP payload to apply at each offset: 1F 20 03 D5\n")
    print("=" * 72)

    for site in SITES:
        hits = search_site(text, site, verbose)
        print(f"\n[{site.name}]")
        print(f"  {site.desc}")
        print(f"  {len(hits)} candidate(s) -- patch offset = offset of the CBZ/CBNZ")

        if not hits:
            print("  -> NO MATCHES. Check decompression or adjust pattern.")
            continue

        # Show at most 8 candidates to keep output readable
        show = hits[:8]
        for (off_cbz, wbl, wcbz) in show:
            off_bl = off_cbz - 4
            tgt = branch_target(off_cbz, wcbz)
            print(f"  offset 0x{off_cbz:08X}  |  "
                  f"{disasm_brief(wbl,  off_bl):<32s}  "
                  f"{disasm_brief(wcbz, off_cbz):<32s}"
                  f"  branch->0x{tgt:08X}")

        if len(hits) > 8:
            print(f"  ... and {len(hits)-8} more (use --verbose to see all)")

        if verbose:
            for (off_cbz, wbl, wcbz) in hits[8:]:
                off_bl = off_cbz - 4
                tgt = branch_target(off_cbz, wcbz)
                print(f"  offset 0x{off_cbz:08X}  {disasm_brief(wbl, off_bl):<32s}  "
                      f"{disasm_brief(wcbz, off_cbz):<32s}  branch->0x{tgt:08X}")

    print()
    print("=" * 72)
    print("Next step: open the binary in Ghidra/Binary Ninja and verify which")
    print("candidate's branch target leads to a PowerOff / LDO-disable chain.")
    print("Record confirmed offsets in offsets/<version>.json, then run mk_ips.py.")


if __name__ == "__main__":
    main()
