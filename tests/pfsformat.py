#!/usr/bin/env python3
"""ACTION_PFS_FORMAT (pfsformat): a format with the size of the reserved area, under AmiFUSE.

    uv run tests/pfsformat.py [--driver pfs3aio] [--keep]

Sends the packet as pfsformat does, between ACTION_INHIBIT on and off, and
reads the reserved area of the new volume from its rootblock: the default
without a size, the size in reserved blocks (1 KiB with 512 byte blocks,
4 KiB with 4096 byte blocks) rounded up to 32 otherwise, also below the
default, and the minimum for a smaller one: the bitmap twice, the blocks of
the format and RESERVED_BUFFER. A size larger than the partition or than a
volume holds is refused and leaves the volume as it was, and a mounted
volume does not take the packet. Each new volume is mounted again and takes
a file. The one with the minimum takes a file over its whole data area and
deletes it without the deldir, one commit that changes every bitmap block.

Needs amifuse and rdbtool.

Prints a line per check and, last, "== pfsformat: <n> passed, <m> failed ==".
Exit status is 0 only if every check passed.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

TESTS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TESTS)
sys.path.insert(0, TESTS)

import largefile64 as L  # noqa: E402  (also applies amifuse_fixes)
from manylinks import Volume  # noqa: E402
from pfs3img import Image  # noqa: E402

check = L.check
ACTION_INHIBIT = 31
ACTION_PFS_FORMAT = 2225
ACTION_SET_DELDIR = 2221
ID_PFS2_DISK = 0x50465302
ERROR_BAD_NUMBER, ERROR_ACTION_NOT_KNOWN, ERROR_DISK_FULL = 115, 209, 221


def make_image(path, driver, size, blocksize):
    subprocess.run(["rdbtool", "-f", path, "create", "size=" + size, "+", "init", "+",
                    "add", "name=DH0", "size=95%", "dostype=0x%08x" % L.PFS3_DOSTYPE, "bs=%d" % blocksize],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run([sys.executable, os.path.join(TESTS, "fi.py"), "format", path, "DH0", "V",
                        "--driver", driver], env=dict(os.environ, FI_CMD="none"), capture_output=True, text=True)
    if "format complete" not in (r.stdout + r.stderr).lower():
        sys.exit("format failed:\n" + r.stdout + r.stderr)


def reserved(image):
    """Reserved blocks and the size of one in bytes."""
    img = Image(image)
    try:
        return (img.lastreserved - img.firstreserved + 1) // img.rescluster, img.resblksize
    finally:
        img.close()


def minimum(image):
    """The smallest reserved area the handler makes for the partition."""
    img = Image(image)
    try:
        # disksize counts device sectors, the handler blocks
        numblocks, size = img.disksize * img.devsize // img.secsize, img.resblksize
    finally:
        img.close()
    per = size // 4 - 3
    bitmap = ((numblocks >> 5) + 1 + per - 1) // per
    index = (bitmap + per - 1) // per
    m = 2 * (bitmap + index) + 7 + 10
    longs = (m + 96) // 32
    kib = 1 + ((longs - 125 + 255) // 256 if longs > 125 else 0)
    m += (kib * 1024 + size - 1) // size
    return (m + 31) & ~31


def fill(image, driver, tag):
    """A file over the whole data area, then its delete without the deldir,
    one commit that changes every bitmap block the file used."""
    img = Image(image)
    try:
        n = (img.blocksfree - img.alwaysfree - 64) * img.secsize
    finally:
        img.close()
    data = (L.pattern(9, 65536) * (n // 65536 + 1))[:n]
    b = L.open_bridge(Path(image), Path(driver))
    try:
        v = Volume(b)
        check("%s: write %d bytes" % (tag, n), v.put("/fill", data), "ok")
        b.free_lock(v.root)
    finally:
        b.flush_volume()
        b.close()
    b = L.open_bridge(Path(image), Path(driver))
    try:
        check("%s: read %d bytes" % (tag, n), b.read_file("/fill", n, 0) == data, True)
        check("%s: deldir off" % tag, bool(packet(b, ACTION_SET_DELDIR, [ID_PFS2_DISK, 0])[0]), True)
        v = Volume(b)
        check("%s: delete" % tag, v.delete(v.root, "fill"), "ok")
        b.free_lock(v.root)
    finally:
        b.flush_volume()
        b.close()
    mounts(image, driver, tag + ", after the delete", "f", L.pattern(4, 3000))


def packet(b, ptype, args):
    with b._lock:
        b.launcher.send_packet(b.state, ptype, args)
        r = b._run_until_replies(max_iters=2000, cycles=2_000_000)
    return (r[-1][2], r[-1][3]) if r else (None, None)


def pfs_format(image, driver, name, kib, inhibit=True):
    """(res1, res2) of ACTION_PFS_FORMAT on the mounted image."""
    b = L.open_bridge(Path(image), Path(driver))
    try:
        if inhibit:
            check("%s: inhibit" % name, bool(packet(b, ACTION_INHIBIT, [-1])[0]), True)
        with b._lock:
            _, bname = b._alloc_bstr(name)
        r = packet(b, ACTION_PFS_FORMAT, [bname, 0, kib])
        if inhibit:
            check("%s: inhibit off" % name, bool(packet(b, ACTION_INHIBIT, [0])[0]), True)
    finally:
        b.flush_volume()
        b.close()
    return r


def mounts(image, driver, tag, name, data):
    """The volume mounts, takes a file and lists it."""
    b = L.open_bridge(Path(image), Path(driver))
    try:
        v = Volume(b)
        check("%s: write %s" % (tag, name), v.put("/" + name, data), "ok")
        check("%s: read %s" % (tag, name), b.read_file("/" + name, len(data), 0) == data, True)
        b.free_lock(v.root)
    finally:
        b.flush_volume()
        b.close()


def run(image, driver, blocksize):
    tag = "%d byte blocks" % blocksize
    default, size = reserved(image)
    perkib = size // 1024
    print("%s: the default reserved area is %d blocks of %d bytes" % (tag, default, size))

    check("%s, no size: formats" % tag, bool(pfs_format(image, driver, "A", 0)[0]), True)
    check("%s, no size: the default" % tag, reserved(image)[0], default)
    mounts(image, driver, tag + ", no size", "a", L.pattern(1, 3000))

    kib = (default + 1000) * perkib
    check("%s, %d KiB: formats" % (tag, kib), bool(pfs_format(image, driver, "B", kib)[0]), True)
    check("%s, %d KiB: blocks" % (tag, kib), reserved(image)[0], (kib // perkib + 31) & ~31)
    mounts(image, driver, "%s, %d KiB" % (tag, kib), "b", L.pattern(2, 3000))

    least = minimum(image)
    kib = (least + 100) * perkib
    check("%s, %d KiB: formats" % (tag, kib), bool(pfs_format(image, driver, "M", kib)[0]), True)
    check("%s, %d KiB: below the default, the size" % (tag, kib), reserved(image)[0], (least + 100 + 31) & ~31)

    kib = 1
    check("%s, %d KiB: formats" % (tag, kib), bool(pfs_format(image, driver, "N", kib)[0]), True)
    check("%s, %d KiB: below the minimum, the minimum" % (tag, kib), reserved(image)[0], least)
    fill(image, driver, "%s, the minimum" % tag)

    kib = 100 * perkib
    check("%s, %d KiB: formats" % (tag, kib), bool(pfs_format(image, driver, "C", kib)[0]), True)
    check("%s, %d KiB: blocks" % (tag, kib), reserved(image)[0], max((100 + 31) & ~31, least))
    mounts(image, driver, "%s, %d KiB" % (tag, kib), "c", L.pattern(3, 3000))

    # larger than the partition, or than a volume holds: refused, the volume
    # is the one before
    img = Image(image)
    blocks = os.path.getsize(image) // img.secsize
    img.close()
    for what, kib, err in (("larger than the partition", (blocks + 64) * perkib, ERROR_DISK_FULL),
                           ("larger than a volume holds", 0x7fffffff, ERROR_BAD_NUMBER)):
        r = pfs_format(image, driver, "D", kib)
        check("%s, %s: refused" % (tag, what), r, (0, err))
        check("%s, %s: the reserved area stays" % (tag, what), reserved(image)[0], max(128, least))
        b = L.open_bridge(Path(image), Path(driver))
        try:
            check("%s, %s: the file stays" % (tag, what), b.read_file("/c", 3000, 0) == L.pattern(3, 3000), True)
        finally:
            b.close()

    # a mounted volume does not take it
    r = pfs_format(image, driver, "E", 0, inhibit=False)
    check("%s, mounted: not known" % tag, r, (0, ERROR_ACTION_NOT_KNOWN))
    check("%s, mounted: the reserved area stays" % tag, reserved(image)[0], max(128, least))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", help="handler, default pfs3aio in the repository root")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()

    driver = os.path.abspath(a.driver or os.path.join(ROOT, "pfs3aio"))
    if not os.path.isfile(driver):
        sys.exit("no driver: " + driver + " (make)")
    tmp = tempfile.mkdtemp(prefix="pfsformat.", dir=L.scratch_dir())
    for blocksize, size in ((512, "160M"), (4096, "128M")):
        image = os.path.join(tmp, "pfsformat-%d.hdf" % blocksize)
        make_image(image, driver, size, blocksize)
        run(image, driver, blocksize)
        if a.keep:
            print(image)
        else:
            os.remove(image)
    if not a.keep:
        os.rmdir(tmp)
    L.report(os.path.basename(driver), "pfsformat")
    sys.exit(1 if L.failed else 0)


if __name__ == "__main__":
    main()
