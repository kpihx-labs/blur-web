#!/usr/bin/env python3
"""Generate the Lua binding table from a locally dumped Blur runtime image."""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

IMAGE_BASE = 0x400000
TABLE_START = 0xEE3A08
TABLE_END = 0xEE4228
EXPECTED_BINDINGS = 255
NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


def read_c_string(image: bytes, pointer: int) -> str | None:
    offset = pointer - IMAGE_BASE
    if offset < 0 or offset >= len(image):
        return None
    end = image.find(b"\0", offset, offset + 96)
    if end < 0:
        return None
    try:
        name = image[offset:end].decode("ascii")
    except UnicodeDecodeError:
        return None
    return name if NAME_PATTERN.fullmatch(name) else None


def extract_bindings(image: bytes) -> list[tuple[int, str, int]]:
    bindings = []
    for slot, address in enumerate(range(TABLE_START, TABLE_END, 8)):
        name_pointer, function = struct.unpack_from("<II", image, address - IMAGE_BASE)
        if not name_pointer and not function:
            continue
        name = read_c_string(image, name_pointer)
        if name and IMAGE_BASE <= function < IMAGE_BASE + len(image):
            bindings.append((slot, name, function))
    if len(bindings) != EXPECTED_BINDINGS:
        raise ValueError(f"expected {EXPECTED_BINDINGS} Lua bindings, found {len(bindings)}")
    return bindings


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("runtime_image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    bindings = extract_bindings(args.runtime_image.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "".join(f"{slot:03x} {name} {function:08x}\n" for slot, name, function in bindings),
        encoding="ascii",
    )
    print(f"generated {len(bindings)} Lua bindings: {args.output}")


if __name__ == "__main__":
    main()
