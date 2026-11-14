#!/usr/bin/env python3
"""A file in the deldir right after its delete, under AmiFUSE.

    uv run tests/deldirloss.py [--driver pfs3aio] [--keep]

Deletes a file and lists .DELDIR before the next update: the file has to be
listed and read back with its content, and stay listed after a flush. Its
blocks are only freed by that update, so they are not taken by another file
before it. The same for a file with a hard link: deleting the file leaves it
as the link and out of the deldir, deleting the link puts it there. Once the
volume is filled after the update, the blocks are reused and the entries
have to go.

Needs amifuse and rdbtool.

Prints a line per check and, last, "== deldirloss: <n> passed, <m> failed ==".
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

check = L.check
SIZE = 3000


def make_image(path, driver):
    subprocess.run(["rdbtool", "-f", path, "create", "size=4M", "+", "init", "+",
                    "add", "name=DH0", "size=95%", "dostype=0x50465303", "bs=512"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run([sys.executable, os.path.join(TESTS, "fi.py"), "format", path, "DH0", "V",
                        "--driver", driver], env=dict(os.environ, FI_CMD="none"), capture_output=True, text=True)
    if "format complete" not in (r.stdout + r.stderr).lower():
        sys.exit("format failed:\n" + r.stdout + r.stderr)


def deldir(b, name):
    return [e["name"] for e in b.list_dir_path("/.DELDIR") if e["name"].split("@")[0] == name]


def run(b):
    v = Volume(b)
    data = L.pattern(7, SIZE)
    check("write A", v.put("/A", data), "ok")
    b.flush_volume()
    check("delete A", v.delete(v.root, "A"), "ok")
    listed = deldir(b, "A")
    check("A in .DELDIR before the update", len(listed), 1)
    if listed:
        check("read A from .DELDIR", b.read_file("/.DELDIR/" + listed[0], SIZE, 0) == data, True)
    b.flush_volume()
    check("A in .DELDIR after the update", len(deldir(b, "A")), 1)

    # a file with a hard link: deleting the file keeps it as the link, only the last delete is real
    data = L.pattern(9, SIZE)
    check("write B", v.put("/B", data), "ok")
    check("hard link C to B", v.link("C", "B"), "ok")
    b.flush_volume()
    check("delete B", v.delete(v.root, "B"), "ok")
    check("B not in .DELDIR, C holds it", deldir(b, "B"), [])
    check("read C", b.read_file("/C", SIZE, 0) == data, True)
    check("delete C", v.delete(v.root, "C"), "ok")
    listed = deldir(b, "C")
    check("C in .DELDIR before the update", len(listed), 1)
    if listed:
        check("read C from .DELDIR", b.read_file("/.DELDIR/" + listed[0], SIZE, 0) == data, True)
    b.flush_volume()
    check("C in .DELDIR after the update", len(deldir(b, "C")), 1)

    # fill the volume: A's freed blocks are taken by other files
    n = 0
    while v.put("/F%04d" % n, L.pattern(n, 16384)) == "ok":
        n += 1
    b.flush_volume()
    check("volume filled", n > 0, True)
    check("A gone from .DELDIR once its blocks are reused", deldir(b, "A"), [])
    check("C gone from .DELDIR once its blocks are reused", deldir(b, "C"), [])
    b.free_lock(v.root)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", help="handler, default pfs3aio in the repository root")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()

    driver = os.path.abspath(a.driver or os.path.join(ROOT, "pfs3aio"))
    if not os.path.isfile(driver):
        sys.exit("no driver: " + driver + " (make)")
    tmp = tempfile.mkdtemp(prefix="deldirloss.", dir=L.scratch_dir())
    image = os.path.join(tmp, "deldir.hdf")
    make_image(image, driver)
    b = L.open_bridge(Path(image), Path(driver))
    try:
        run(b)
    finally:
        b.flush_volume()
        b.close()
    if a.keep:
        print(image)
    else:
        os.remove(image)
        os.rmdir(tmp)
    L.report(os.path.basename(driver), "deldirloss")
    sys.exit(1 if L.failed else 0)


if __name__ == "__main__":
    main()
