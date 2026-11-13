#!/usr/bin/env python3
"""Create an 880K FFS Amiga floppy image (ADF) holding the given files.

usage: mkadf.py OUTPUT.adf VOLUME_NAME FILE...
"""
import os
import struct
import sys
import time

BS = 512
BLOCKS = 1760
ROOT = BLOCKS // 2
BITMAP = ROOT + 1
HT_SIZE = 72
T_HEADER, T_DATA, T_LIST = 2, 8, 16
ST_ROOT, ST_FILE = 1, -3
AMIGA_EPOCH = 252460800  # 1978-01-01 relative to the Unix epoch


def amiga_date():
    secs = max(0, int(time.time()) - AMIGA_EPOCH)
    return secs // 86400, (secs % 86400) // 60, (secs % 60) * 50


def checksum_fix(blk, pos):
    struct.pack_into(">I", blk, pos, 0)
    total = sum(struct.unpack(">128I", blk)) & 0xFFFFFFFF
    struct.pack_into(">I", blk, pos, (-total) & 0xFFFFFFFF)


def bcpl(blk, off, text, maxlen):
    raw = text.encode("latin-1")[:maxlen]
    blk[off] = len(raw)
    blk[off + 1:off + 1 + len(raw)] = raw


def hash_name(name):
    h = len(name)
    for c in name.upper().encode("latin-1"):
        h = (h * 13 + c) & 0x7FF
    return h % HT_SIZE


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out, label, files = sys.argv[1], sys.argv[2], sys.argv[3:]

    img = bytearray(BLOCKS * BS)
    used = {ROOT, BITMAP}
    nextfree = [2]

    def alloc():
        while nextfree[0] in used:
            nextfree[0] += 1
        if nextfree[0] >= BLOCKS:
            sys.exit("mkadf: files do not fit on a floppy")
        n = nextfree[0]
        used.add(n)
        return n

    def block(n):
        return memoryview(img)[n * BS:(n + 1) * BS]

    days, mins, ticks = amiga_date()
    root_ht = [0] * HT_SIZE
    names = set()

    for path in files:
        name = os.path.basename(path)
        if name.upper() in names:
            sys.exit("mkadf: duplicate name " + name)
        names.add(name.upper())
        with open(path, "rb") as f:
            data = f.read()
        chunks = [data[i:i + BS] for i in range(0, len(data), BS)]
        dblocks = []
        for c in chunks:
            n = alloc()
            img[n * BS:n * BS + len(c)] = c
            dblocks.append(n)

        # Header holds 72 data pointers, each extension block 72 more.
        groups = [dblocks[i:i + HT_SIZE] for i in range(0, len(dblocks), HT_SIZE)] or [[]]
        hdr = alloc()
        ext = [alloc() for _ in groups[1:]]
        owners = [hdr] + ext

        for idx, (owner, grp) in enumerate(zip(owners, groups)):
            b = bytearray(BS)
            struct.pack_into(">I", b, 4, owner)
            struct.pack_into(">I", b, 8, len(grp))
            for i, d in enumerate(grp):
                struct.pack_into(">I", b, 24 + (HT_SIZE - 1 - i) * 4, d)
            struct.pack_into(">I", b, 500, ROOT if idx == 0 else hdr)
            if idx + 1 < len(owners):
                struct.pack_into(">I", b, 504, owners[idx + 1])
            if idx == 0:
                struct.pack_into(">I", b, 0, T_HEADER)
                struct.pack_into(">I", b, 16, dblocks[0] if dblocks else 0)
                struct.pack_into(">I", b, 324, len(data))
                struct.pack_into(">3I", b, 420, days, mins, ticks)
                bcpl(b, 432, name, 30)
                h = hash_name(name)
                struct.pack_into(">I", b, 496, root_ht[h])
                root_ht[h] = hdr
                struct.pack_into(">i", b, 508, ST_FILE)
            else:
                struct.pack_into(">I", b, 0, T_LIST)
                struct.pack_into(">i", b, 508, ST_FILE)
            checksum_fix(b, 20)
            block(owner)[:] = b

    # Root block.
    r = bytearray(BS)
    struct.pack_into(">I", r, 0, T_HEADER)
    struct.pack_into(">I", r, 12, HT_SIZE)
    for i, h in enumerate(root_ht):
        struct.pack_into(">I", r, 24 + i * 4, h)
    struct.pack_into(">i", r, 312, -1)
    struct.pack_into(">I", r, 316, BITMAP)
    struct.pack_into(">3I", r, 420, days, mins, ticks)
    bcpl(r, 432, label, 30)
    struct.pack_into(">3I", r, 472, days, mins, ticks)
    struct.pack_into(">3I", r, 484, days, mins, ticks)
    struct.pack_into(">i", r, 508, ST_ROOT)
    checksum_fix(r, 20)
    block(ROOT)[:] = r

    # Bitmap block: bit set = free; bit 0 is block 2.
    bm = bytearray(BS)
    for n in range(2, BLOCKS):
        if n not in used:
            bit = n - 2
            word = 1 + bit // 32
            cur = struct.unpack_from(">I", bm, word * 4)[0]
            struct.pack_into(">I", bm, word * 4, cur | (1 << (bit % 32)))
    checksum_fix(bm, 0)
    block(BITMAP)[:] = bm

    # Non-bootable FFS boot block with a valid checksum.
    img[0:4] = b"DOS\x01"
    struct.pack_into(">I", img, 8, ROOT)
    total = 0
    for w in struct.unpack(">256I", bytes(img[:1024])):
        total += w
        if total > 0xFFFFFFFF:
            total = (total & 0xFFFFFFFF) + 1
    struct.pack_into(">I", img, 4, (~total) & 0xFFFFFFFF)

    with open(out, "wb") as f:
        f.write(img)
    print("%s: %d blocks free" % (out, BLOCKS - len(used)))


if __name__ == "__main__":
    main()
