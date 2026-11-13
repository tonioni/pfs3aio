#!/usr/bin/env python3
"""A directory with a damaged directory block, under AmiFUSE.

    uv run tests/damageddir.py [--driver pfs3aio] [--keep]

Creates a directory whose files fill three directory blocks and clears the id
of the second block in the image. The image is then mounted in a new process:
the files of the first block are deleted, and deleting the directory has to
fail with 213 Disk not validated and leave it in place. Older versions answer
216 Directory not empty here, while the directory lists empty.

Needs amifuse and rdbtool.

Prints a line per check and, last, "== damageddir: <n> passed, <m> failed ==".
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

import largefile64 as L  # noqa: E402  (also installs the timer shim)
from manylinks import Volume  # noqa: E402
from pfs3img import Image  # noqa: E402

check = L.check
FILES = 40
ERRORS = {213: "Disk not validated", 216: "Directory not empty"}


def reply(r):
    return "ok" if r[0] else "%d %s" % (r[1], ERRORS.get(r[1], "error"))


def name(i):
    return ("F%02d" % i).ljust(30, "x")


def make(image, driver):
    """/D with FILES files; returns the entries of each of its blocks."""
    subprocess.run(["rdbtool", "-f", image, "create", "size=16M", "+", "init", "+",
                    "add", "name=DH0", "size=95%", "dostype=0x50465303", "bs=512"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run([sys.executable, os.path.join(TESTS, "fi.py"), "format", image, "DH0", "V",
                        "--driver", driver], env=dict(os.environ, FI_CMD="none"), capture_output=True, text=True)
    if "format complete" not in (r.stdout + r.stderr).lower():
        sys.exit("format failed:\n" + r.stdout + r.stderr)
    b = L.open_bridge(Path(image), Path(driver))
    try:
        v = Volume(b)
        lock, _ = b.create_dir(v.root, "D")
        b.free_lock(lock)
        for i in range(FILES):
            check("create " + name(i), v.put("/D/" + name(i), L.pattern(i, 100)), "ok")
        b.free_lock(v.root)
    finally:
        b.flush_volume()
        b.close()


def damage(image):
    """Clears the id of /D's second block; returns the names in its first block."""
    img = Image(image)
    try:
        blocks = img.chain(img.lookup("D"))
        offset = img.base + blocks[1][1] * img.secsize
    finally:
        img.close()
    check("/D has three blocks", len(blocks), 3)
    with open(image, "r+b") as f:
        f.seek(offset)
        f.write(b"\0\0")
    return [n for n, _, _ in blocks[0][2]]


def damaged(image, driver, first):
    """The second process: delete what the first block holds, then /D."""
    b = L.open_bridge(Path(image), Path(driver))
    try:
        v = Volume(b)
        lock, _ = b.locate(v.root, "D")
        for n in first:
            check("delete " + n, v.delete(lock, n), "ok")
        b.free_lock(lock)
        check("delete /D with a damaged block", reply(b.delete_object(v.root, "D")), "213 Disk not validated")
        check("/D lists empty", [e["name"] for e in b.list_dir_path("/D")], [])
        check("/D is still there", "D" in [e["name"] for e in b.list_dir_path("/")], True)
        b.free_lock(v.root)
    finally:
        b.close()
    L.report("damaged block, " + os.path.basename(driver), "damageddir")
    sys.exit(1 if L.failed else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", help="handler, default pfs3aio in the repository root")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--damaged", metavar="IMAGE", help=argparse.SUPPRESS)
    ap.add_argument("--first", nargs="*", help=argparse.SUPPRESS)
    a = ap.parse_args()

    driver = os.path.abspath(a.driver or os.path.join(ROOT, "pfs3aio"))
    if not os.path.isfile(driver):
        sys.exit("no driver: " + driver + " (make)")
    if a.damaged:
        damaged(a.damaged, driver, a.first)
    tmp = tempfile.mkdtemp(prefix="damageddir.", dir=L.scratch_dir())
    image = os.path.join(tmp, "damaged.hdf")
    make(image, driver)
    first = damage(image)
    sys.stdout.flush()
    r = subprocess.run([sys.executable, os.path.abspath(__file__), "--driver", driver,
                        "--damaged", image, "--first"] + first)
    check("the damaged image ran in a new process", r.returncode, 0)
    if a.keep:
        print(image)
    else:
        os.remove(image)
        os.rmdir(tmp)
    L.report(os.path.basename(driver), "damageddir")
    sys.exit(1 if L.failed else 0)


if __name__ == "__main__":
    main()
