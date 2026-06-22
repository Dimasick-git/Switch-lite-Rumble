#!/usr/bin/env python3
"""
find_offsets.py -- locate fs power-gate patch sites, with nxdumptool support.

Usage:
    # from an nxdumptool ExeFS dump (recommended):
    python3 find_offsets.py /path/to/exefs/main          # raw NSO file
    python3 find_offsets.py /path/to/exefs/main.nso      # same, .nso extension

    # from an already-decompressed flat binary (legacy):
    python3 find_offsets.py fs_decompressed.bin

    # add --emit-signature for portable patterns (paste into patches.h):
    python3 find_offsets.py fs_main.nso --emit-signature

nxdumptool workflow (on console):
    1. Launch nxdumptool from hbmenu.
    2. Browse System Data Archive -> fs -> NCA/NCA FS dump options
       -> Program #0 -> ExeFS section -> Start dump.
       (Or: System Titles -> fs -> Dump ExeFS)
    3. Copy sdmc:/nxdt_rw_proc/.../<build_id>/exefs/main to PC.
    4. Run this script on that 'main' file.

The script auto-detects NSO format (magic "NSO0") and decompresses
.text / .rodata / .data sections with LZ4 if the compression flag is set.

No external dependencies for LZ4 -- a minimal block decompressor is built in.
Optionally install lz4 for faster decoding: pip install lz4

Searches for BL + CBZ/CBNZ X0/W0 pairs at each of the three power-gate sites:
  Site 1 -- IsGameCardInserted:   CBZ/CBNZ W0 after polling call
  Site 2 -- GetGameCardHandle:    CBNZ X0 after handle-acquisition BL
  Site 3 -- GetGameCardAttribute: CBNZ X0 after attribute-check BL

With --emit-signature prints a portable byte pattern (ARM64 branch immediates
masked as "..") ready to paste into
  software/fs-rail-keepalive/source/patches.h
"""
import sys
import struct
from pathlib import Path

NSO_MAGIC       = b"NSO0"
NSO_HEADER_SIZE = 0x100
NOP_BYTES       = bytes.fromhex("1F2003D5")

SIG_WORDS_BEFORE = 4
SIG_WORDS_AFTER  = 2


# ---------------------------------------------------------------------------
# Minimal LZ4 block decompressor (NSO sections use raw LZ4 blocks, no frame)
# ---------------------------------------------------------------------------

def _lz4_block_decompress(src: bytes, max_out: int) -> bytes:
    """Pure-Python LZ4 block decompressor. Replaces lz4.block if not installed."""
    out  = bytearray()
    pos  = 0
    slen = len(src)
    while pos < slen:
        tok  = src[pos]; pos += 1
        llen = tok >> 4
        if llen == 15:
            while pos < slen:
                x = src[pos]; pos += 1
                llen += x
                if x != 255: break
        out.extend(src[pos:pos + llen]); pos += llen
        if pos >= slen:
            break
        off = src[pos] | (src[pos + 1] << 8); pos += 2
        mlen = (tok & 0xF) + 4
        if (tok & 0xF) == 15:
            while pos < slen:
                x = src[pos]; pos += 1
                mlen += x
                if x != 255: break
        m = len(out) - off
        for _ in range(mlen):
            out.append(out[m]); m += 1
        if len(out) >= max_out:
            break
    return bytes(out[:max_out])


try:
    import lz4.block as _lz4
    def lz4_block_decompress(src, max_out):
        return _lz4.decompress(src, uncompressed_size=max_out)
except ImportError:
    lz4_block_decompress = _lz4_block_decompress


# ---------------------------------------------------------------------------
# NSO loader
# ---------------------------------------------------------------------------

def _u32le(data, off):
    return struct.unpack_from('<I', data, off)[0]


def load_nso(raw: bytes):
    """
    Parse an NSO file from nxdumptool (or hbloader).  Returns (text, build_id).
    The NSO0 header is 0x100 bytes followed by the compressed/raw section data.
    """
    if raw[:4] != NSO_MAGIC:
        return raw, None                     # assume pre-decompressed flat binary

    flags               = _u32le(raw, 0x0C)
    text_foff           = _u32le(raw, 0x10)
    text_size           = _u32le(raw, 0x18)  # decompressed size
    text_compressed_sz  = _u32le(raw, 0x60)

    if flags & 1:                            # .text is LZ4 compressed
        src = raw[text_foff:text_foff + text_compressed_sz]
        text = lz4_block_decompress(src, text_size)
    else:
        text = raw[text_foff:text_foff + text_size]

    full_build_id = raw[0x40:0x60]           # 32 bytes
    return text, full_build_id


# ---------------------------------------------------------------------------
# ARM64 helpers
# ---------------------------------------------------------------------------

def _word(data, off):
    return struct.unpack_from('<I', data, off)[0]

def is_bl(w):           return (w & 0xFC000000) == 0x94000000
def is_cbz64(w, r=None):  return (w & 0xFF000000) == 0xB4000000 and (r is None or (w & 0x1F) == r)
def is_cbnz64(w, r=None): return (w & 0xFF000000) == 0xB5000000 and (r is None or (w & 0x1F) == r)
def is_cbz32(w, r=None):  return (w & 0xFF000000) == 0x34000000 and (r is None or (w & 0x1F) == r)
def is_cbnz32(w, r=None): return (w & 0xFF000000) == 0x35000000 and (r is None or (w & 0x1F) == r)
def is_cb(w):           return is_cbz64(w) or is_cbnz64(w) or is_cbz32(w) or is_cbnz32(w)

def branch_target(base, w):
    imm19 = (w >> 5) & 0x7FFFF
    if imm19 & 0x40000: imm19 |= ~0x7FFFF
    return base + imm19 * 4

def disasm(w, off):
    if is_bl(w):
        imm26 = w & 0x3FFFFFF
        if imm26 & 0x2000000: imm26 |= ~0x3FFFFFF
        return f"BL   0x{(off + imm26*4) & 0xFFFFFFFF:08X}"
    for fn, mn in [(is_cbz64,'CBZ  X'),(is_cbnz64,'CBNZ X'),(is_cbz32,'CBZ  W'),(is_cbnz32,'CBNZ W')]:
        if fn(w):
            return f"{mn}{w&0x1F}, 0x{branch_target(off,w)&0xFFFFFFFF:08X}"
    return f"???? 0x{w:08X}"


# ---------------------------------------------------------------------------
# Signature emission
# ---------------------------------------------------------------------------

def _sig_tokens(w):
    b = [(w >> (8*i)) & 0xFF for i in range(4)]
    if is_bl(w):  return ['..', '..', '..', '..']
    if is_cb(w):  return ['..', '..', '..', f'{b[3]:02x}']
    return [f'{b[i]:02x}' for i in range(4)]

def emit_signature(text, off_cbz):
    start = max(0, off_cbz - SIG_WORDS_BEFORE * 4)
    end   = min(len(text), off_cbz + 4 + SIG_WORDS_AFTER * 4)
    toks  = []
    o = start
    while o < end and o + 4 <= len(text):
        toks.extend(_sig_tokens(_word(text, o))); o += 4
    return ' '.join(toks), off_cbz - start


# ---------------------------------------------------------------------------
# Search
# ---------------------------------------------------------------------------

# (name, description, want_x64, reg)
# want_x64=True  -> search for CBZ/CBNZ X0 (64-bit; clang often uses CBNZ X0
#                   even for u32 Result, since W0 zero-extends to X0)
# want_x64=False -> search for CBZ/CBNZ W0 (32-bit; bool return / W0 check)
SITES = [
    ('IsGameCardInserted_false_branch',
     'CBZ/CBNZ W0 after polling BL -- false return cuts rails',
     False, 0),
    ('GetGameCardHandle_power_gate_branch',
     'CBNZ X0 after handle-acquisition BL -- Result != 0 cuts rails',
     True,  0),
    ('GetGameCardAttribute_fail_branch',
     'CBNZ X0 after attribute-check BL -- bad card type cuts rails',
     True,  0),
]

def search(text, want_x64, reg):
    hits = []
    n = len(text) // 4
    for i in range(n - 1):
        wbl  = _word(text, i*4)
        wcbz = _word(text, i*4 + 4)
        if not is_bl(wbl): continue
        if want_x64:
            if not (is_cbnz64(wcbz, reg) or is_cbz64(wcbz, reg)): continue
        else:
            if not (is_cbnz32(wcbz, reg) or is_cbz32(wcbz, reg)): continue
        hits.append((i*4 + 4, wbl, wcbz))
    return hits


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    verbose  = '--verbose' in sys.argv or '-v' in sys.argv
    emit_sig = '--emit-signature' in sys.argv
    args     = [a for a in sys.argv[1:] if not a.startswith('-')]

    if not args:
        print(__doc__)
        sys.exit(1)

    path = Path(args[0])
    raw  = path.read_bytes()

    is_nso = raw[:4] == NSO_MAGIC
    print(f"Input: {path.name} ({len(raw):,} bytes) -- {'NSO format' if is_nso else 'flat binary'}")

    text, build_id = load_nso(raw)

    if build_id is not None:
        bid_hex   = build_id.hex().upper()
        bid_short = bid_hex[:16]   # first 8 bytes = Atmosphere IPS filename stem
        print(f"Build ID: {bid_hex}")
        print(f"IPS path: sdmc:/atmosphere/exefs_patches/fs/{bid_short}.ips")
    else:
        bid_short = None
        print("Build ID: unknown (pre-decompressed flat binary)")

    print(f"Searching .text ({len(text):,} bytes)")
    print(f"NOP patch: 1F 20 03 D5")
    print('=' * 72)

    for name, desc, want_x64, reg in SITES:
        hits = search(text, want_x64=want_x64, reg=reg)
        print(f"\n[{name}]")
        print(f"  {desc}")
        print(f"  {len(hits)} candidate(s)")
        if not hits:
            print("  -> NO MATCHES. Check input or try --verbose.")
            continue
        for off_cbz, wbl, wcbz in hits[:8]:
            off_bl = off_cbz - 4
            print(f"  0x{off_cbz:08X}  {disasm(wbl, off_bl):<38} {disasm(wcbz, off_cbz)}")
            if emit_sig:
                pat, poff = emit_signature(text, off_cbz)
                print(f"    sig: {pat}")
                print(f"    patch_offset: {poff}")
        if len(hits) > 8:
            print(f"  ... and {len(hits)-8} more (--verbose to see all)")
        if verbose:
            for off_cbz, wbl, wcbz in hits[8:]:
                print(f"  0x{off_cbz:08X}  {disasm(wbl, off_cbz-4):<38} {disasm(wcbz, off_cbz)}")
                if emit_sig:
                    pat, poff = emit_signature(text, off_cbz)
                    print(f"    sig: {pat}  patch_offset: {poff}")

    print()
    print('=' * 72)
    if bid_short is not None:
        print(f"To generate the IPS patch, run:")
        print(f"  python3 mk_ips.py offsets/<version>.json output.ips")
        print(f"Deploy to: sdmc:/atmosphere/exefs_patches/fs/{bid_short}.ips")
    print("For version-agnostic patching: paste --emit-signature output into")
    print("  software/fs-rail-keepalive/source/patches.h  (set enabled=true)")


if __name__ == '__main__':
    main()
