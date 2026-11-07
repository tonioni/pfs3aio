#!/usr/bin/env python3
"""Reject executables that violate the old RDB/custom loader contracts.

This deliberately accepts only the stripped, single CODE hunk format we
produce. Unsupported records must fail the build, not silently bypass the
custom loader's assumptions.
"""

import argparse
from pathlib import Path
import struct
import sys


HUNK_CODE = 1001
HUNK_RELOC32 = 1004
HUNK_END = 1010
HUNK_HEADER = 1011


def read_hunk(path):
    data = Path(path).read_bytes()
    if len(data) < 36 or len(data) % 4:
        raise ValueError(f"{path}: truncated or unaligned hunk file")
    words = struct.unpack(f">{len(data) // 4}I", data)
    if words[:5] != (HUNK_HEADER, 0, 1, 0, 0):
        raise ValueError(f"{path}: expected one hunk, no resident library names")
    if words[6] != HUNK_CODE or words[5] != words[7]:
        raise ValueError(f"{path}: expected fully initialized CODE allocation")
    end = 8 + words[7]
    if end >= len(words):
        raise ValueError(f"{path}: truncated CODE hunk")
    code = data[32:end * 4]
    relocs = []
    groups = 0
    position = end

    def take():
        nonlocal position
        if position >= len(words):
            raise ValueError(f"{path}: truncated relocation record")
        result = words[position]
        position += 1
        return result

    record = take()
    if record == HUNK_RELOC32:
        while True:
            count = take()
            if count == 0:
                break
            groups += 1
            if take() != 0:
                raise ValueError(f"{path}: relocation targets another hunk")
            if count > len(words) - position:
                raise ValueError(f"{path}: truncated relocation offsets")
            for _ in range(count):
                offset = take()
                if offset % 2 or offset + 4 > len(code):
                    raise ValueError(f"{path}: relocation outside CODE hunk")
                relocs.append(offset)
        record = take()
    if record != HUNK_END or position != len(words):
        raise ValueError(f"{path}: unexpected records after CODE/RELOC32")
    if len(set(relocs)) != len(relocs):
        raise ValueError(f"{path}: duplicate relocations")
    return data, code, relocs, groups


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("normal", "custom"))
    parser.add_argument("normal", type=Path)
    parser.add_argument("custom", type=Path, nargs="?")
    args = parser.parse_args()
    data, _, relocs, groups = read_hunk(args.normal)
    if groups != 1 or not relocs:
        raise ValueError(f"{args.normal}: custom loader needs one relocation group")
    if args.kind == "custom":
        if args.custom is None:
            parser.error("custom mode requires both normal and custom binaries")
        _, code, custom_relocs, custom_groups = read_hunk(args.custom)
        if custom_relocs or custom_groups:
            raise ValueError(f"{args.custom}: custom executable has relocations")
        if not code.endswith(data) or len(code) <= len(data):
            raise ValueError(f"{args.custom}: normal executable not embedded verbatim")
        print(f"{args.custom}: one CODE hunk, no relocations, payload matches")
    else:
        print(f"{args.normal}: one CODE hunk, {len(relocs)} self relocations")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        sys.exit(str(error))
