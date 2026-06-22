#!/usr/bin/env python3
"""
mk_ips.py -- generate an Atmosphere IPS patch file for the fs service.

Usage:
    python3 mk_ips.py <offsets.json> <output.ips>

Reads a per-version offset spec (offsets/<version>.json) and emits a
valid IPS file ready to deploy to:
    sdmc:/atmosphere/exefs_patches/fs/<fs_build_id>/patch.ips

The fs_build_id is the first 8 bytes of the NSO build ID (at offset 0x40
in the raw .nso file), printed as uppercase hex. This script reads it from
the JSON and prints the full deploy path.

IPS format used:
  MAGIC     5 bytes  'PATCH'
  records   N * (3-byte BE offset | 2-byte BE length | data)
  EOF       3 bytes  'EOF'

All offsets are into the decompressed .text section (no NSO header).
"""
import json
import struct
import sys
from pathlib import Path

MAGIC_HEAD = b"PATCH"
MAGIC_TAIL = b"EOF"
NOP = bytes.fromhex("1F2003D5")  # ARM64 NOP


def encode_record(offset: int, data: bytes) -> bytes:
    """One IPS record: 3-byte big-endian offset, 2-byte big-endian length, data."""
    if offset > 0xFFFFFF:
        raise ValueError(f"IPS offset 0x{offset:X} exceeds 24-bit range -- "
                         "use IPS32 format or check the offset")
    off3 = struct.pack(">I", offset)[1:]  # drop the leading zero byte
    length = struct.pack(">H", len(data))
    return off3 + length + data


def build_ips(records: list) -> bytes:
    """records: list of (offset: int, data: bytes)"""
    body = b""
    for off, dat in records:
        body += encode_record(off, dat)
    return MAGIC_HEAD + body + MAGIC_TAIL


def load_spec(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    return json.loads(text)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    spec_path = Path(sys.argv[1])
    out_path  = Path(sys.argv[2])

    spec = load_spec(spec_path)

    hos = spec.get("hos_version", "unknown")
    build_id = spec.get("fs_build_id", "XXXXXXXXXXXXXXXX")
    print(f"HOS {hos}  |  fs build ID: {build_id}")
    print()

    records = []
    skipped = []

    for name, info in spec.get("patches", {}).items():
        if name.startswith("_"):
            continue  # skip comment keys
        offset = info.get("offset")
        if offset is None:
            skipped.append(name)
            continue
        # Support hex strings ("0x1A2B3C") or plain ints
        if isinstance(offset, str):
            offset = int(offset, 16) if offset.startswith("0x") else int(offset)
        data_hex = info.get("data", "1F2003D5")
        data = bytes.fromhex(data_hex)
        records.append((offset, data))
        print(f"  + {name}")
        print(f"      offset: 0x{offset:08X}  data: {data.hex().upper()}")

    if skipped:
        print()
        for name in skipped:
            print(f"  SKIP {name}: offset not yet found")

    if not records:
        print("\nNo offsets filled in -- nothing to write.")
        sys.exit(1)

    ips = build_ips(records)
    out_path.write_bytes(ips)
    
    print()
    print(f"Wrote {len(ips)} bytes -> {out_path}")
    print()
    print("Deploy to SD card:")
    print(f"  sdmc:/atmosphere/exefs_patches/fs/{build_id}/patch.ips")


if __name__ == "__main__":
    main()
