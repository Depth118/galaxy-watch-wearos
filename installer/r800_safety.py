"""Fail-closed, *offline* SM-R800 archive and PIT validation.

No sockets, device writes or bootloader operations. A successful result does
NOT establish device identity, image authenticity or retail S-Boot compatibility.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import struct
import tarfile


class SafetyError(ValueError):
    """Refuse an operation with uncertain partition identity or package content."""


# Known SM-R800 layout from the upstream project; never used as a PIT fallback.
EXPECTED_IMAGES = {
    "sboot.bin": 80,
    "param.bin": 9,
    "dzImage": 11,
    "dzImage-recovery": 12,
    "ramdisk.img": 8,
    "ramdisk-recovery.img": 7,
    "rootfs.img": 18,
    "system-data.img": 16,
    "user.img": 17,
    "csc.img": 15,
    "nad_refer.bin": 3,
    "nad_fw.bin": 13,
}
PIT_MAGIC = 0x12349876
PIT_HEADER_SIZE = 28
PIT_ENTRY_SIZE = 132
MAX_PIT_ENTRIES = 128
MAX_TOTAL_BYTES = 4 * 1024**3


def parse_pit_strict(data: bytes) -> dict[str, int]:
    """Parse live Samsung PIT; never supplement missing entries from defaults."""
    if len(data) < PIT_HEADER_SIZE:
        raise SafetyError("PIT header truncated or absent")
    magic, count = struct.unpack_from("<II", data)
    if magic != PIT_MAGIC:
        raise SafetyError(f"Wrong PIT magic: {magic:#x}")
    if not 1 <= count <= MAX_PIT_ENTRIES:
        raise SafetyError(f"Implausible PIT entry count: {count}")
    need = PIT_HEADER_SIZE + count * PIT_ENTRY_SIZE
    if len(data) < need:
        raise SafetyError(f"PIT truncated: got {len(data)}, expected at least {need}")
    mapping: dict[str, int] = {}
    identifiers: dict[int, str] = {}
    for index in range(count):
        start = PIT_HEADER_SIZE + index * PIT_ENTRY_SIZE
        entry = data[start:start + PIT_ENTRY_SIZE]
        identifier = struct.unpack_from("<I", entry, 8)[0]
        rawname = entry[68:100].split(b"\x00", 1)[0]
        try:
            name = rawname.decode("ascii")
        except UnicodeDecodeError as exc:
            raise SafetyError("Non-ASCII PIT filename") from exc
        if not name or name == "-":
            continue
        if name in mapping:
            raise SafetyError(f"Repeated PIT filename: {name}")
        if identifier in identifiers:
            raise SafetyError(f"Repeated PIT ID: {identifier}")
        mapping[name] = identifier
        identifiers[identifier] = name
    if not mapping:
        raise SafetyError("PIT has no flashable filenames")
    return mapping


def validate_r800_pit(pit: dict[str, int]) -> None:
    """Fail on any missing or unexpected ID; does NOT prove device is SM-R800."""
    for name, expected_id in EXPECTED_IMAGES.items():
        if name not in pit:
            raise SafetyError(f"Expected PIT entry absent: {name}")
        if pit[name] != expected_id:
            raise SafetyError(
                f"Unexpected PIT mapping for {name}: {pit[name]}, expected {expected_id}")


def verify_tar_md5(filename: str | os.PathLike[str]) -> None:
    """Detect accidental corruption; MD5 is NOT authentication/signature proof."""
    path = Path(filename)
    if not path.name.lower().endswith(".tar.md5"):
        raise SafetyError("A .tar.md5 archive is required")
    size = path.stat().st_size
    if size < 1024:
        raise SafetyError("Firmware archive too small")
    with path.open("rb") as fh:
        tail_len = min(512, size)
        fh.seek(size - tail_len)
        tail = fh.read(tail_len)
        match = re.search(rb"([0-9a-fA-F]{32})  ([A-Za-z0-9_.-]+\.tar)\r?\n?\Z", tail)
        if match is None:
            raise SafetyError("Missing or invalid Odin MD5 trailer")
        trailing_start = size - tail_len + match.start()
        fh.seek(0)
        digest = hashlib.md5()
        left = trailing_start
        while left:
            chunk = fh.read(min(left, 4 * 1024 * 1024))
            if not chunk:
                raise SafetyError("Unexpected end of archive")
            digest.update(chunk)
            left -= len(chunk)
    if digest.hexdigest().encode("ascii") != match.group(1).lower():
        raise SafetyError("Firmware MD5 mismatch")


def validate_archive(archive: tarfile.TarFile) -> list[tarfile.TarInfo]:
    """Allow only the twelve known image names; reject protected/unknown paths."""
    files = archive.getmembers()
    if not files:
        raise SafetyError("Empty firmware archive")
    found: set[str] = set()
    total = 0
    for member in files:
        name = member.name
        if not member.isfile() or name not in EXPECTED_IMAGES:
            raise SafetyError(f"Unexpected image or archive member: {name!r}")
        if name in found:
            raise SafetyError(f"Duplicate image: {name}")
        if not 0 < member.size <= MAX_TOTAL_BYTES:
            raise SafetyError(f"Invalid size for {name}: {member.size}")
        total += member.size
        if total > MAX_TOTAL_BYTES:
            raise SafetyError("Firmware image data exceed 4 GiB limit")
        found.add(name)
    missing = set(EXPECTED_IMAGES) - found
    if missing:
        raise SafetyError("Missing required images: " + ", ".join(sorted(missing)))
    return files


def preflight_file(filename: str | os.PathLike[str]) -> list[tarfile.TarInfo]:
    """Verify a local archive without opening a network socket or device."""
    verify_tar_md5(filename)
    try:
        with tarfile.open(filename, "r:") as archive:
            return validate_archive(archive)
    except (tarfile.TarError, OSError) as exc:
        raise SafetyError(f"Unreadable firmware archive: {exc}") from exc
