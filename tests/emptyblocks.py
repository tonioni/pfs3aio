#!/usr/bin/env python3
"""Directory blocks emptied by deletes, under AmiFUSE.

    uv run tests/emptyblocks.py [--driver pfs3aio] [--files 1000] [--keep]

Creates many files in a directory, deletes all but a few (oldest first, newest
first, in random order) or moves them to another directory, and reads the
image: the directory's chain of directory blocks must not keep the emptied
blocks. At most the first block of a directory may stay empty, and the removed
blocks must be free. The partition has 10 buffers, so most emptied blocks have
left the cache before the next update. A directory listing held open with
ExNext while its files are deleted has to go on with the remaining files, each
listed once. Copies of the image taken during the deletes without a flush (a
power cut) must hold a whole directory and mount. At the end the image is
mounted again in a new process: the files that were kept have to be listed,
and a directory can be emptied and deleted.

Needs amifuse and rdbtool.

Prints a line per check and, last, "== emptyblocks: <n> passed, <m> failed ==".
Exit status is 0 only if every check passed.
"""
import argparse
import os
import random
import shutil
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
ACTION_EXAMINE_OBJECT, ACTION_EXAMINE_NEXT = 23, 24
ERROR_NO_MORE_ENTRIES = 232
BUFFERS = 10
KEEP = 3
SNAPS = 5            # copies of the image taken during the first mass delete, without a flush


def make_image(path, driver):
    if os.path.exists(path):
        os.remove(path)
    subprocess.run(["rdbtool", "-f", path, "create", "size=64M", "+", "init", "+",
                    "add", "name=DH0", "size=95%", "dostype=0x50465303", "bs=512",
                    "num_buffer=%d" % BUFFERS],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, FI_CMD="none")
    r = subprocess.run([sys.executable, os.path.join(TESTS, "fi.py"), "format", path, "DH0", "V",
                        "--driver", driver], env=env, capture_output=True, text=True)
    if "format complete" not in (r.stdout + r.stderr).lower():
        sys.exit("format failed:\n" + r.stdout + r.stderr)


def chain(image, path):
    """[entries per block] of the directory's chain, first block first."""
    img = Image(image)
    try:
        return [len(e) for _, _, e in img.chain(img.lookup(path))], img.reserved_free, img.bitmap_free()
    finally:
        img.close()


def names(n):
    return ["F%04d" % i for i in range(n)]


class Lister:
    """ExNext on a directory lock, one entry per call."""

    def __init__(self, b, path):
        self.b = b
        self.lock, _ = b.locate(0, path)
        self.fib = b.vh.alloc.alloc_memory(L.FIB64, label="FIB")
        self.send(ACTION_EXAMINE_OBJECT)

    def send(self, ptype):
        b = self.b
        with b._lock:
            b.launcher.send_packet(b.state, ptype, [self.lock, self.fib.addr >> 2])
            r = b._run_until_replies()
        if not r or not r[-1][2]:
            return None, r[-1][3] if r else -1
        mem, a = b.mem, self.fib.addr
        return bytes(mem.r8(a + 9 + i) for i in range(mem.r8(a + 8))).decode("latin-1"), 0

    def next(self):
        return self.send(ACTION_EXAMINE_NEXT)

    def close(self):
        self.b.free_lock(self.lock)


def snapshot_name(image, i):
    return "%s.snap%d" % (image, i)


def blocks_check(d, before, after, free0, free, bitmap):
    """Checks on the chain of d after most of its entries are gone."""
    print("  %s: %d blocks before, %d after (%d empty)" % (d, len(before), len(after), after.count(0)))
    check("%s: no empty block behind the first" % d, after[1:].count(0), 0)
    check("%s: blocks left" % d, len(after) <= 2, True)
    check("%s: reserved bitmap agrees with the rootblock" % d, bitmap, free)
    check("%s: removed blocks are free" % d, free - free0 >= len(before) - len(after), True)


def mass_delete(b, v, image, d, order, n, snaps=False):
    """Create n files in /d, delete all but the first KEEP, read the chain.

    With snaps, the image is copied SNAPS times during the deletes, without a
    flush: what a power cut would leave."""
    lock, _ = b.create_dir(v.root, d)
    b.free_lock(lock)  # exclusive
    made = sum(v.put("/%s/%s" % (d, f), b"") == "ok" for f in names(n))
    check("%s: create %d files" % (d, n), made, n)
    b.flush_volume()
    before, free0, _ = chain(image, d)
    lock, _ = b.locate(0, d)
    gone = names(n)[KEEP:]
    if order == "newest first":
        gone.reverse()
    elif order == "random":
        random.Random(1).shuffle(gone)
    every = len(gone) // (SNAPS + 1)
    bad = []
    for i, f in enumerate(gone):
        if v.delete(lock, f) != "ok":
            bad.append(f)
        if snaps and (i + 1) % every == 0 and (i + 1) // every <= SNAPS:
            shutil.copyfile(image, snapshot_name(image, (i + 1) // every))
    check("%s: delete %d files %s" % (d, len(gone), order), bad[:5], [])
    b.free_lock(lock)
    b.flush_volume()
    after, free, bitmap = chain(image, d)
    check("%s: listed files" % d, sorted(e["name"] for e in b.list_dir_path("/" + d)), names(KEEP))
    blocks_check(d, before, after, free0, free, bitmap)


def mass_move(b, v, image, src, dst, n):
    """Create n files in /src, move all but the first KEEP to /dst."""
    for d in (src, dst):
        lock, _ = b.create_dir(v.root, d)
        b.free_lock(lock)  # exclusive
    made = sum(v.put("/%s/%s" % (src, f), b"") == "ok" for f in names(n))
    check("%s: create %d files" % (src, n), made, n)
    b.flush_volume()
    before, free0, _ = chain(image, src)
    slock, _ = b.locate(0, src)
    dlock, _ = b.locate(0, dst)
    bad = [f for f in names(n)[KEEP:] if v.rename(slock, f, dlock, f) != "ok"]
    check("%s: move %d files to %s" % (src, n - KEEP, dst), bad[:5], [])
    b.free_lock(slock)
    b.free_lock(dlock)
    b.flush_volume()
    after, free, bitmap = chain(image, src)
    check("%s: listed files" % src, sorted(e["name"] for e in b.list_dir_path("/" + src)), names(KEEP))
    check("%s: listed files" % dst, sorted(e["name"] for e in b.list_dir_path("/" + dst)), names(n)[KEEP:])
    print("  %s: %d blocks before, %d after (%d empty)" % (src, len(before), len(after), after.count(0)))
    check("%s: no empty block behind the first" % src, after[1:].count(0), 0)
    check("%s: blocks left" % src, len(after) <= 2, True)
    check("%s: reserved bitmap agrees with the rootblock" % src, bitmap, free)


def check_snapshot(path, d, n):
    """A copy taken during the deletes: the committed chain of d is whole."""
    tag = os.path.basename(path)
    img = Image(path)
    try:
        ch = img.chain(img.lookup(d))
        listed = sorted(name for _, _, ents in ch for name, _, _ in ents)
        check("%s: chain blocks allocated" % tag, [bn for _, bn, _ in ch if img.is_free(bn)], [])
        check("%s: reserved bitmap agrees with the rootblock" % tag, img.bitmap_free(), img.reserved_free)
        check("%s: entries are files of %s" % (tag, d), [f for f in listed if f not in names(n)], [])
        check("%s: kept files listed" % tag, [f for f in names(KEEP) if f not in listed], [])
    except (ValueError, KeyError) as e:
        check("%s: readable" % tag, str(e), "")
    finally:
        img.close()


def held_listing(b, v, d, n):
    """Delete files while a listing of the directory is half way through."""
    lock, _ = b.create_dir(v.root, d)
    b.free_lock(lock)  # exclusive
    made = sum(v.put("/%s/%s" % (d, f), b"") == "ok" for f in names(n))
    check("%s: create %d files" % (d, n), made, n)
    keep = {"F0000", "F%04d" % (n // 2), "F%04d" % (n - 1)}
    lock, _ = b.locate(0, d)
    ls = Lister(b, d)
    seen = [ls.next()[0] for _ in range(n // 4)]
    bad = [f for f in names(n) if f not in keep and v.delete(lock, f) != "ok"]
    check("%s: delete while listed" % d, bad[:5], [])
    b.flush_volume()
    rest, err = [], 0
    while True:
        name, err = ls.next()
        if name is None:
            break
        rest.append(name)
    ls.close()
    b.free_lock(lock)
    check("%s: listing ends with %d" % (d, ERROR_NO_MORE_ENTRIES), err, ERROR_NO_MORE_ENTRIES)
    check("%s: listed after the deletes are kept files" % d, [f for f in rest if f not in keep], [])
    allseen = seen + rest
    check("%s: no file listed twice" % d, len(allseen), len(set(allseen)))
    check("%s: every kept file listed" % d, sorted(keep - set(allseen)), [])


def run(b, image, n):
    v = Volume(b)
    mass_delete(b, v, image, "D1", "oldest first", n, snaps=True)
    mass_delete(b, v, image, "D2", "newest first", n)
    mass_delete(b, v, image, "D3", "random", n)
    held_listing(b, v, "D4", n // 2)
    mass_move(b, v, image, "D5", "D6", n)


def snap(b, n):
    """A snapshot mounted: D1 lists, takes a new file and deletes it."""
    v = Volume(b)
    listed = sorted(e["name"] for e in b.list_dir_path("/D1"))
    check("snapshot: D1 lists kept files", [f for f in names(KEEP) if f not in listed], [])
    check("snapshot: D1 lists only its files", [f for f in listed if f not in names(n)], [])
    check("snapshot: write a file", v.put("/D1/NEW", b"x"), "ok")
    lock, _ = b.locate(0, "D1")
    check("snapshot: delete it", v.delete(lock, "NEW"), "ok")
    b.free_lock(lock)


def reread(b, n):
    v = Volume(b)
    for d in ("D1", "D2", "D3", "D5"):
        check("remount: %s listed" % d, sorted(e["name"] for e in b.list_dir_path("/" + d)), names(KEEP))
    check("remount: D6 listed", sorted(e["name"] for e in b.list_dir_path("/D6")), names(n)[KEEP:])
    lock, _ = b.locate(0, "D1")
    bad = [f for f in names(KEEP) if v.delete(lock, f) != "ok"]
    b.free_lock(lock)
    check("remount: empty D1", bad, [])
    check("remount: delete D1", v.delete(v.root, "D1"), "ok")
    b.flush_volume()


def child(a, driver):
    b = L.open_bridge(Path(a.image), Path(driver), a.debug)
    try:
        if a.phase == "use":
            run(b, a.image, a.files)
        else:
            (reread if a.phase == "reread" else snap)(b, a.files)
    finally:
        b.flush_volume()
        b.close()
    sys.exit(1 if L.failed else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", help="handler, default pfs3aio in the repository root")
    ap.add_argument("--files", type=int, default=1000)
    ap.add_argument("--keep", action="store_true", help="keep the image")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("--phase", choices=("use", "reread", "snap"), help=argparse.SUPPRESS)
    ap.add_argument("--image", help=argparse.SUPPRESS)
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    driver = a.driver or os.path.join(ROOT, "pfs3aio")
    if not os.path.isfile(driver):
        sys.exit("no driver: " + driver + " (make)")
    if a.phase:
        child(a, driver)

    tmp = tempfile.mkdtemp(prefix="emptyblocks.", dir=L.scratch_dir())
    image = os.path.join(tmp, "eb.hdf")
    make_image(image, driver)
    snaps = [snapshot_name(image, i) for i in range(1, SNAPS + 1)]

    def child_run(phase, img):
        cmd = [sys.executable, os.path.abspath(__file__), "--driver", driver, "--files", str(a.files),
               "--phase", phase, "--image", img]
        if a.debug:
            cmd.append("--debug")
        return subprocess.run(cmd).returncode

    check("use", child_run("use", image), 0)
    for s in snaps:
        if check(os.path.basename(s) + ": taken", os.path.exists(s), True):
            check_snapshot(s, "D1", a.files)
            check(os.path.basename(s) + ": mounted", child_run("snap", s), 0)
    check("reread", child_run("reread", image), 0)
    if a.keep:
        print("image kept: " + image)
    else:
        for f in [image] + snaps:
            if os.path.exists(f):
                os.remove(f)
        os.rmdir(tmp)
    L.report("512 byte blocks, %d buffers, %s" % (BUFFERS, os.path.basename(driver)), "emptyblocks")
    sys.exit(1 if L.failed else 0)


if __name__ == "__main__":
    main()
