#!/usr/bin/env python3
"""
nzit.py - Tool for inspecting, unpacking, and packing Samsung Tizen / Exynos
          NZIT kernel container images (dzImage / boot.img / recovery.img).

Container structure:
  [0x0000 - 0x07FF] NZIT Header (2048 bytes)
  [0x0800 - K_END ] Raw ARM64 Kernel (Image), 2048-byte aligned
  [DT_START-DT_END] Samsung Multi-DTB Table ("EXYN" header + DTBs)
  [SIG_START-END  ] Trailing RSA Signature block (272 bytes: 16-byte header + 256-byte RSA)

Usage:
  python nzit.py info <dzImage>
  python nzit.py unpack <dzImage> [--out-dir <dir>]
  python nzit.py pack --kernel <Image> --dtb <dtb.img> --out <dzImage> [--pad-to <bytes>]
"""

import argparse
import json
import os
import struct
import sys

NZIT_MAGIC = 0x54495A4E  # "TIZN" little-endian ("NZIT")
EXYN_MAGIC = 0x4E595845  # "EXYN" little-endian


def parse_nzit_header(data):
    if len(data) < 2048:
        raise ValueError("File too short to contain NZIT header")
    magic, page_align, ksize, total_ksize, dt_size, ramdisk_size, hdr_size = struct.unpack(
        "<IIIIIII", data[:28]
    )
    if magic != NZIT_MAGIC:
        raise ValueError(f"Invalid NZIT magic: {hex(magic)} (expected {hex(NZIT_MAGIC)})")
    return {
        "magic": magic,
        "page_align": page_align,
        "ksize": ksize,
        "total_ksize": total_ksize,
        "dt_size": dt_size,
        "ramdisk_size": ramdisk_size,
        "hdr_size": hdr_size,
    }


def parse_dtb_table(dt_data):
    if len(dt_data) < 12:
        return []
    magic, ver, num = struct.unpack("<III", dt_data[:12])
    if magic != EXYN_MAGIC:
        return []
    entries = []
    offset = 12
    for i in range(num):
        if offset + 20 > len(dt_data):
            break
        plat, variant, rev, dt_off, size = struct.unpack("<IIIII", dt_data[offset : offset + 20])
        entries.append({
            "index": i,
            "platform": hex(plat),
            "variant": hex(variant),
            "rev": hex(rev),
            "offset": dt_off,
            "size": size,
        })
        offset += 20
    return entries


def cmd_info(args):
    with open(args.image, "rb") as f:
        data = f.read()
    hdr = parse_nzit_header(data)
    print(f"=== NZIT Image Info: {args.image} ===")
    print(f"Total file size : {len(data):,} bytes")
    print(f"Header size     : {hdr['hdr_size']} bytes")
    print(f"Kernel size     : {hdr['ksize']:,} bytes ({hex(hdr['ksize'])})")
    print(f"Aligned size    : {hdr['total_ksize']:,} bytes ({hex(hdr['total_ksize'])})")
    print(f"Page alignment  : {hdr['page_align']:,} bytes ({hex(hdr['page_align'])})")
    print(f"DT table size   : {hdr['dt_size']:,} bytes ({hex(hdr['dt_size'])})")
    print(f"Ramdisk size    : {hdr['ramdisk_size']:,} bytes")

    k_start = hdr["hdr_size"]
    k_end_aligned = k_start + ((hdr["ksize"] + 0x7FF) & ~0x7FF)
    dt_start = k_end_aligned
    dt_end = dt_start + hdr["dt_size"]

    print(f"Kernel offset   : {hex(k_start)} .. {hex(k_start + hdr['ksize'])}")
    print(f"DT table offset : {hex(dt_start)} .. {hex(dt_end)}")

    if dt_end <= len(data):
        dt_data = data[dt_start:dt_end]
        entries = parse_dtb_table(dt_data)
        print(f"Found {len(entries)} DTB entries in table:")
        for e in entries:
            print(f"  [{e['index']}] Platform: {e['platform']} Variant: {e['variant']} Rev: {e['rev']} Size: {e['size']:,} bytes (offset {hex(e['offset'])})")

    if dt_end < len(data):
        sig_data = data[dt_end:dt_end + 272]
        print(f"Signature trailer: {len(sig_data)} bytes at {hex(dt_end)}")


def cmd_unpack(args):
    with open(args.image, "rb") as f:
        data = f.read()
    hdr = parse_nzit_header(data)
    out_dir = args.out_dir or (os.path.splitext(args.image)[0] + "_unpacked")
    os.makedirs(out_dir, exist_ok=True)

    k_start = hdr["hdr_size"]
    k_end = k_start + hdr["ksize"]
    k_aligned = k_start + ((hdr["ksize"] + 0x7FF) & ~0x7FF)
    dt_start = k_aligned
    dt_end = dt_start + hdr["dt_size"]

    # Extract kernel Image
    kernel_path = os.path.join(out_dir, "Image")
    with open(kernel_path, "wb") as f:
        f.write(data[k_start:k_end])
    print(f"[+] Extracted kernel: {kernel_path} ({hdr['ksize']:,} bytes)")

    # Extract full multi-DTB table
    dt_data = data[dt_start:dt_end]
    dtb_table_path = os.path.join(out_dir, "dtb.img")
    with open(dtb_table_path, "wb") as f:
        f.write(dt_data)
    print(f"[+] Extracted multi-DTB table: {dtb_table_path} ({len(dt_data):,} bytes)")

    # Extract individual DTBs
    entries = parse_dtb_table(dt_data)
    dtb_dir = os.path.join(out_dir, "dtbs")
    os.makedirs(dtb_dir, exist_ok=True)
    for e in entries:
        dtb_blob = dt_data[e["offset"] : e["offset"] + e["size"]]
        name = f"dtb_{e['index']}_plat_{e['platform']}_var_{e['variant']}.dtb"
        p = os.path.join(dtb_dir, name)
        with open(p, "wb") as f:
            f.write(dtb_blob)
        print(f"    [-] DTB {e['index']}: {name} ({len(dtb_blob):,} bytes)")

    # Extract signature trailer if present
    sig_path = os.path.join(out_dir, "signature.bin")
    if dt_end < len(data):
        with open(sig_path, "wb") as f:
            f.write(data[dt_end:dt_end + 272])
        print(f"[+] Extracted signature block: {sig_path}")

    # Write manifest
    manifest = {
        "header": hdr,
        "dtb_entries": entries,
    }
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"[+] Unpack complete to {out_dir}")


def cmd_pack(args):
    with open(args.kernel, "rb") as f:
        k_data = f.read()
    with open(args.dtb, "rb") as f:
        dt_data = f.read()

    sig_data = b""
    if args.sig and os.path.isfile(args.sig):
        with open(args.sig, "rb") as f:
            sig_data = f.read()
    else:
        # 16 bytes header + 256 bytes dummy RSA
        sig_data = b"\x01" + b"\x00" * 15 + b"\x41" * 256

    ksize = len(k_data)
    hdr_size = 2048
    page_align = 0x8000
    total_ksize = ksize + page_align
    dt_size = len(dt_data)
    ramdisk_size = 0x1E00000

    # Build 2048-byte header
    hdr = struct.pack(
        "<IIIIIII",
        NZIT_MAGIC,
        page_align,
        ksize,
        total_ksize,
        dt_size,
        ramdisk_size,
        hdr_size,
    )
    hdr = hdr.ljust(hdr_size, b"\x00")

    # Align kernel to 2048 bytes
    k_aligned = k_data.ljust(((ksize + 0x7FF) & ~0x7FF), b"\x00")

    # Combine: header + aligned kernel + dtb table + signature
    out_data = hdr + k_aligned + dt_data + sig_data

    # Optional padding to eMMC partition size (e.g. 18874368)
    if args.pad_to and args.pad_to > len(out_data):
        out_data = out_data.ljust(args.pad_to, b"\x00")

    with open(args.out, "wb") as f:
        f.write(out_data)
    print(f"[+] Successfully packed {args.out} ({len(out_data):,} bytes)")


def main():
    parser = argparse.ArgumentParser(description="NZIT container packer/unpacker")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_info = sub.add_parser("info", help="Display NZIT image metadata")
    p_info.add_argument("image", help="Path to dzImage")
    p_info.set_defaults(func=cmd_info)

    p_unpack = sub.add_parser("unpack", help="Unpack dzImage into kernel and DTBs")
    p_unpack.add_argument("image", help="Path to dzImage")
    p_unpack.add_argument("--out-dir", "-o", help="Output directory")
    p_unpack.set_defaults(func=cmd_unpack)

    p_pack = sub.add_parser("pack", help="Pack kernel and DTBs into dzImage")
    p_pack.add_argument("--kernel", "-k", required=True, help="Path to raw kernel Image")
    p_pack.add_argument("--dtb", "-d", required=True, help="Path to multi-DTB dtb.img")
    p_pack.add_argument("--sig", "-s", help="Path to signature block (optional)")
    p_pack.add_argument("--pad-to", type=int, help="Pad image to exact size in bytes")
    p_pack.add_argument("--out", "-o", required=True, help="Output dzImage file")
    p_pack.set_defaults(func=cmd_pack)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
