#!/usr/bin/env python3
"""A file with more than 4096 hard links, under AmiFUSE.

    python3 tests/manylinks.py [--driver pfs3aio] [--links 4100] [--blocksize 512] [--keep]

Links one file 4100 times. Every link has to be made, and the functions that follow
the link list have to work on the result: rewriting the file with a new size (the new
size has to reach the first and the last link), moving it into a directory, deleting
the last link and deleting the original (the first link has to become the file). A
probe file is written after the links are made and after each of these steps, and
has to succeed. If a link or a step fails, the later steps still run, and the first
step after which the volume refuses writes (213 Disk not validated) is named. At the
end the image is mounted again, in a new process, the directory and a link have to
read and a file has to be writable.

Needs amifuse and rdbtool. The list is walked from its start for every new link,
so the run takes about 15 minutes.

Prints a line per check and, last, "== manylinks: <n> passed, <m> failed ==".
In GitHub Actions the failed checks are also annotations and a table in the job
summary. Exit status is 0 only if every check passed.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time
import traceback
from pathlib import Path

TESTS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TESTS)
sys.path.insert(0, TESTS)

import largefile64 as L  # noqa: E402  (also installs the timer shim)

check = L.check
ERRORS = {103: "Not enough memory", 202: "Object in use", 203: "Object already exists",
          205: "Object not found", 212: "Object wrong type", 213: "Disk not validated",
          221: "Disk full", 222: "Delete protected", 223: "Write protected"}
SMALL, BIG = 700, 1500
PROBES = 5           # files written between the steps


def err(code):
    return "%d %s" % (code, ERRORS.get(code, "error"))


class Volume:
    """The packets the test uses, each giving "ok" or the DOS error as number and text."""

    def __init__(self, b):
        self.b = b
        self.last = {}
        run_until, log = b._run_until_replies, b._log_replies
        # walking thousands of links takes the handler more than the default 50 bursts
        b._run_until_replies = lambda *a, **k: run_until(*a, **dict(dict(max_iters=100000), **k))

        def keep(label, replies):
            self.last[label] = replies
            return log(label, replies)
        b._log_replies = keep
        self.root, _ = b.locate(0, "")

    def res(self, label):
        r = self.last.get(label)
        return (r[-1][2], r[-1][3]) if r else (0, -1)

    def reply(self, r):
        return "ok" if r and r[0] else err(r[1] if r else -1)

    def put(self, path, data):
        b = self.b
        self.last.clear()
        opened = b.open_file(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
        if not opened:
            return "open: " + err(self.res("find")[1])
        fh, lock = opened
        n = b.write_handle(fh, data)
        wrote = self.res("write")
        b.close_file(fh)
        closed = self.res("end")
        b.free_lock(lock)
        if n != len(data):
            return "write: " + err(wrote[1])
        return "ok" if closed[0] else "close: " + err(closed[1])

    def link(self, name, to):
        b = self.b
        target, _ = b.locate(0, to)
        with b._lock:
            _, link_name = b._alloc_bstr(name)
            b.launcher.send_packet(b.state, L.ACTION_MAKE_LINK, [self.root, link_name, target, L.LINK_HARD])
            r = b._run_until_replies()
        b.free_lock(target)
        return self.reply((r[-1][2], r[-1][3]) if r else None)

    def delete(self, lock, name):
        return self.reply(self.b.delete_object(lock, name))

    def rename(self, lock, name, to_lock, to):
        return self.reply(self.b.rename_object(lock, name, to_lock, to))

    def entries(self):
        return {e["name"]: e for e in self.b.list_dir_path("/")}


def run(b, nlinks):
    v = Volume(b)
    probes = []

    def probe(after):
        name = "P%d" % (len(probes) + 1)
        probes.append(name)
        check("write a file after " + after, v.put("/" + name, L.pattern(9, 100)), "ok")

    small, big = L.pattern(1, SMALL), L.pattern(3, BIG)
    check("write F", v.put("/F", small), "ok")
    t0, made, why = time.time(), 0, "ok"
    for i in range(1, nlinks + 1):
        why = v.link("L%05d" % i, "F")
        if why != "ok":
            break
        made = i
        if i % 1000 == 0:
            print("  %d links, %.0f s" % (i, time.time() - t0), flush=True)
    check("link %d to F" % (made + 1 if made < nlinks else nlinks), why, "ok")
    check("links made", made, nlinks)
    probe("making %d links" % made)
    last = "L%05d" % made

    # UpdateLinks: the size of the file is copied into every link
    check("rewrite F with a new size", v.put("/F", big), "ok")
    sizes = v.entries()
    check("size of the first link", sizes.get("L00001", {}).get("size"), BIG)
    check("size of the last link", sizes.get(last, {}).get("size"), BIG)
    probe("rewriting F")

    # MoveLink: every link learns the new directory of the file
    d, e = v.b.create_dir(v.root, "D")
    check("make directory D", "ok" if d else err(e), "ok")
    check("move F into D", v.rename(v.root, "F", d, "F"), "ok")
    probe("moving F")

    # DeleteLink: the walk to the predecessor in the list
    check("delete the last link", v.delete(v.root, last), "ok")
    probe("deleting the last link")

    # RemapLinks and UpdateLinkDir: the first link becomes the file
    check("delete the original", v.delete(d, "F"), "ok")
    probe("deleting the original")
    v.b.free_lock(d)

    want = ["D", "Y"] + probes + ["L%05d" % i for i in range(1, made)]
    check("write a file at the end", v.put("/Y", L.pattern(2, 300)), "ok")
    v.b.flush_volume()
    ents = v.entries()
    check("directory", sorted(ents), sorted(want))
    check("first link is the file", ents.get("L00001", {}).get("dir_type"), -3)
    check("second link is a link", ents.get("L00002", {}).get("dir_type"), -4)
    for n in ("L00001", "L00002", "L%05d" % (made // 2), "L%05d" % (made - 1)):
        check("read " + n, v.b.read_file("/" + n, BIG, 0), big)
    v.b.free_lock(v.root)
    return sorted(want), big, made


def remount(a, driver):
    """The second process: mount the image the first one left and check what it holds."""
    b = L.open_bridge(Path(a.remount), Path(driver), a.debug)
    big = L.pattern(3, BIG)
    want = sorted(["D", "Y"] + ["P%d" % i for i in range(1, PROBES + 1)] + ["L%05d" % i for i in range(1, a.links)])
    try:
        v = Volume(b)
        check("directory after mounting again", sorted(v.entries()), want)
        check("first link is the file after mounting again", v.entries().get("L00001", {}).get("dir_type"), -3)
        check("read the second link after mounting again", b.read_file("/L00002", BIG, 0), big)
        check("write a file after mounting again", v.put("/Z", L.pattern(5, 200)), "ok")
        b.free_lock(v.root)
    except Exception as e:
        traceback.print_exc()
        check("the second mount ran to its end", "%s: %s" % (type(e).__name__, e), "no exception")
    finally:
        b.flush_volume()
        b.close()
    L.report("mounted again", "manylinks")
    sys.exit(1 if L.failed else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", help="handler, default pfs3aio in the repository root")
    ap.add_argument("--links", type=int, default=4100, help="hard links to make, above 4096 (default %(default)s)")
    ap.add_argument("--size", default="64Mi", help="image size")
    ap.add_argument("--blocksize", type=int, default=512,
                    help="logical block size of the volume (default %(default)s)")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("--remount", metavar="IMAGE", help=argparse.SUPPRESS)
    a = ap.parse_args()

    driver = a.driver or os.path.join(ROOT, "pfs3aio")
    if not os.path.isfile(driver):
        sys.exit("no driver: " + driver + " (make)")
    if a.remount:
        remount(a, driver)
    tmp = tempfile.mkdtemp(prefix="manylinks.", dir=L.scratch_dir())
    image = os.path.join(tmp, "links.hdf")
    print("%s, image %s, %d links" % (driver, a.size, a.links))
    L.make_image(image, a.size, driver, a.blocksize)
    b = L.open_bridge(Path(image), Path(driver), a.debug)
    want = big = made = None
    try:
        want, big, made = run(b, a.links)
    except Exception as e:
        # an error in the driver run is a failed check, with what it was
        traceback.print_exc()
        check("the test ran to its end", "%s: %s" % (type(e).__name__, e), "no exception")
    finally:
        b.flush_volume()
        b.close()
    if want:
        sys.stdout.flush()
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--driver", driver, "--blocksize",
                            str(a.blocksize), "--links", str(made), "--remount", image])
        check("the image mounted again in a new process", r.returncode, 0)
    if not a.keep:
        os.remove(image)
        os.rmdir(tmp)
    L.report("%d byte blocks, %s" % (a.blocksize, os.path.basename(driver)), "manylinks")
    sys.exit(1 if L.failed else 0)


if __name__ == "__main__":
    main()
