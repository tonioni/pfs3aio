#!/usr/bin/env python3
"""A file above 4G through the 64 bit packets of AmigaOS 4, under AmiFUSE.

    python3 tests/largefile64.py [--driver pfs3aio-largefile] [--blocksize 4096] [--keep]

Formats a sparse image, switches the large file flag on with ACTION_SET_LARGEFILE
(unless the driver formatted it that way already) and grows a file past 4G with
ACTION_CHANGE_FILE_SIZE64. It then writes, seeks, reads, renames, links, truncates
and deletes around the 4G line and checks the neighbours in the directory and the
free space after each step.

The image has 4096 byte blocks; --blocksize 512 formats 512 byte blocks with a
LARGE_FILE_SIZE 2 build. Needs a large file build (LARGE_FILE_SIZE 1 or 2),
amifuse and rdbtool. Takes a few minutes with 4096 byte blocks.

Prints a line per check and, last, "== largefile64: <n> passed, <m> failed ==".
In GitHub Actions the failed checks are also annotations and a table in the job
summary. Exit status is 0 only if every check passed.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import traceback

TESTS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TESTS)
sys.path.insert(0, TESTS)

from timer_device import DeferredTimers  # noqa: E402
from contextlib import ExitStack  # noqa: E402

# Same timer shim as fi.py and dirops.py: immediate timer replies are not valid
# hardware behaviour.
try:
    import amifuse.timer_device  # noqa: F401
except ImportError:
    _timer_patches = ExitStack()
    _timers = DeferredTimers()
    _timers.install(_timer_patches, real_time=True)

ACTION_CHANGE_FILE_POSITION64 = 8001
ACTION_GET_FILE_POSITION64 = 8002
ACTION_CHANGE_FILE_SIZE64 = 8003
ACTION_GET_FILE_SIZE64 = 8004
ACTION_EXAMINE_OBJECT = 23
ACTION_EXAMINE_OBJECT64 = 26408
ACTION_EXAMINE_NEXT64 = 26409
ACTION_EXAMINE_FH64 = 26410
FIB64 = 260
OFFSET_BEGINNING, OFFSET_CURRENT, OFFSET_END = -1, 0, 1
DP64_INIT = 0xFFFFFFFD
DOSTRUE = (1 << 64) - 1
DOSTRUE32 = 0xFFFFFFFF
ACTION_MAKE_LINK = 1021
ACTION_SET_LARGEFILE = 2223
ID_PFS2_DISK = 0x50465302
LINK_HARD = 0
G = 1 << 30
FOUR_G = 4 * G
PKT = 96

failed = 0
passed = 0
failures = []        # (check, got, want) of every failed check


def check(what, got, want):
    global failed, passed
    ok = got == want
    g, w = repr(got)[:60], repr(want)[:60]
    if ok:
        passed += 1
    else:
        failed += 1
        failures.append((what, g, w))
    print("%-40s %-24s %s" % (what, g, "ok" if ok else "FAIL (want %s)" % w))
    return ok


def report(title):
    """The last line, and in GitHub Actions the failed checks as annotations
    and as a table in the job summary."""
    print("== largefile64: %d passed, %d failed ==" % (passed, failed))
    if not os.environ.get("GITHUB_ACTIONS"):
        return
    for what, g, w in failures:
        msg = "%s: got %s, want %s" % (what, g, w)
        msg = msg.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print("::error title=largefile64::" + msg)
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        cell = lambda t: t.replace("|", "\\|").replace("`", "'")
        with open(path, "a") as f:
            f.write("### Large file packets: %s\n\n%d passed, %d failed\n" % (title, passed, failed))
            if failures:
                f.write("\n| check | got | want |\n|---|---|---|\n")
                for what, g, w in failures:
                    f.write("| %s | `%s` | `%s` |\n" % (cell(what), cell(g), cell(w)))


def pattern(tag, n):
    return bytes((tag * 37 + i * 7 + (i >> 8)) & 0xFF for i in range(n))


class Packets:
    """The four 64 bit packets, laid out as dos.library sends them."""

    def __init__(self, bridge):
        from amitools.vamos.libstructs.dos import FileHandleStruct
        globals()["FileHandleStruct"] = FileHandleStruct
        self.b = bridge
        # the standard packet scratch is 48 bytes, this one is longer
        bridge.launcher._pkt_size = PKT

    def call(self, fh_addr, ptype, arg2=0, arg3=0, iters=50):
        b = self.b
        mem = b.mem
        fe = FileHandleStruct(mem, fh_addr).args.val
        with b._lock:
            pkt, _ = b.launcher.send_packet(b.state, ptype, [])
            mem.w32(pkt + 12, 0)
            mem.w32(pkt + 32, fe)
            mem.w32(pkt + 40, (arg2 >> 32) & 0xFFFFFFFF)
            mem.w32(pkt + 44, arg2 & 0xFFFFFFFF)
            mem.w32(pkt + 48, arg3 & 0xFFFFFFFF)
            replies = b._run_until_replies(max_iters=iters)
            if not replies:
                return None
            res0 = mem.r32(pkt + 12)
            res2 = mem.r32(pkt + 16)
            res1 = (mem.r32(pkt + 24) << 32) | mem.r32(pkt + 28)
        return res0, res1, res2

    def size(self, fh):
        return self.call(fh, ACTION_GET_FILE_SIZE64)

    def pos(self, fh):
        return self.call(fh, ACTION_GET_FILE_POSITION64)

    def seek(self, fh, off, mode=OFFSET_BEGINNING):
        return self.call(fh, ACTION_CHANGE_FILE_POSITION64, off, mode & 0xFFFFFFFF)

    def setsize(self, fh, size, mode=OFFSET_BEGINNING):
        # allocating gigabytes of blocks takes the handler millions of cycles
        return self.call(fh, ACTION_CHANGE_FILE_SIZE64, size, mode & 0xFFFFFFFF, iters=200000)


class Examine64:
    """ACTION_EXAMINE_OBJECT64, _NEXT64 and _FH64 with the block they carry."""

    def __init__(self, bridge):
        self.b = bridge
        self.fib = bridge.vh.alloc.alloc_memory(FIB64, label="FIB64")

    def send(self, ptype, obj):
        b = self.b
        with b._lock:
            b.launcher.send_packet(b.state, ptype, [obj, self.fib.addr >> 2])
            r = b._run_until_replies()
            ok = bool(r) and r[-1][2] != 0
            return self.read() if ok else None

    def read(self):
        mem, a = self.b.mem, self.fib.addr
        n = mem.r8(a + 8)
        name = bytes(mem.r8(a + 9 + i) for i in range(n)).decode("latin-1")
        return {"name": name, "type": mem.r32(a + 4) - (1 << 32 if mem.r32(a + 4) >> 31 else 0),
                "size": (mem.r32(a + 228) << 32) | mem.r32(a + 232), "size32": mem.r32(a + 124),
                "blocks": (mem.r32(a + 236) << 32) | mem.r32(a + 240), "date": mem.r32(a + 132),
                "extflags": mem.r8(a + 258), "key": mem.r32(a)}

    def path(self, name):
        lock, _ = self.b.locate(0, name)
        try:
            return self.send(ACTION_EXAMINE_OBJECT64, lock)
        finally:
            self.b.free_lock(lock)

    def path32(self, name):
        lock, _ = self.b.locate(0, name)
        try:
            return self.send(ACTION_EXAMINE_OBJECT, lock)
        finally:
            self.b.free_lock(lock)

    def handle(self, fh_addr):
        fe = FileHandleStruct(self.b.mem, fh_addr).args.val
        return self.send(ACTION_EXAMINE_FH64, fe)

    def walk(self):
        """name -> size of the root directory, in directory order."""
        lock, _ = self.b.locate(0, "")
        out = []
        try:
            r = self.send(ACTION_EXAMINE_OBJECT64, lock)
            while r is not None:
                r = self.send(ACTION_EXAMINE_NEXT64, lock)
                if r is not None:
                    out.append((r["name"], r["size"]))
        finally:
            self.b.free_lock(lock)
        return out


def make_image(path, size, driver, blocksize):
    if os.path.exists(path):
        os.remove(path)
    # 4096 byte logical blocks make the formatter choose PFS2, the only type
    # that can carry the large file flag. With 512 byte blocks the volume is
    # PFS1 unless the driver formats PFS2 itself (LARGE_FILE_SIZE 2).
    subprocess.run(["rdbtool", "-f", path, "create", "size=" + size, "+", "init", "+",
                    "add", "name=DH0", "size=95%", "dostype=0x50465303", "bs=%d" % blocksize],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, FI_CMD="none")
    r = subprocess.run([sys.executable, os.path.join(TESTS, "fi.py"), "format", path, "DH0", "V",
                        "--driver", driver], env=env, capture_output=True, text=True)
    if "format complete" not in (r.stdout + r.stderr).lower():
        sys.exit("format failed:\n" + r.stdout + r.stderr)


def open_bridge(image, driver, debug=False):
    from amitools.vamos.lib import IntuitionLibrary as IL
    from amifuse.fuse_fs import HandlerBridge

    if not getattr(open_bridge, "patched", False):
        IL.IntuitionLibrary.EasyRequestArgs = lambda self, ctx, w, e, i, a: 0
        open_bridge.patched = True
    return HandlerBridge(image, driver, read_only=False, debug=debug)


def largefile_flag(bridge, enable):
    """ACTION_SET_LARGEFILE: (Res1, Res2), Res2 being the flag after the call.

    Switches the flag on if enable is set, only reports it otherwise. Res1 is
    DOSTRUE for a driver that knows the packet.
    """
    b = bridge
    with b._lock:
        pkt, _ = b.launcher.send_packet(b.state, ACTION_SET_LARGEFILE,
                                        [ID_PFS2_DISK, 1 if enable else 0])
        if not b._run_until_replies():
            return None
        return b.mem.r32(pkt + 12), b.mem.r32(pkt + 16)


def put(bridge, name, data):
    fh, _ = bridge.open_file("/" + name, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
    n = bridge.write_handle(fh, data)
    bridge.close_file(fh)
    return n == len(data)


def scratch_dir():
    """The test scratch space of tests/builddir.zsh: <repo>/build/tests or TEST_BUILD_DIR."""
    d = os.path.abspath(os.environ.get("TEST_BUILD_DIR") or os.path.join(ROOT, "build", "tests"))
    marker = os.path.join(d, ".build-dir")
    if not os.path.exists(marker):
        if os.path.isdir(d) and os.listdir(d):
            sys.exit("%s exists but is not a pfs3aio test directory (no .build-dir marker)" % d)
        os.makedirs(d, exist_ok=True)
        open(marker, "w").close()
    return d


def names(bridge):
    return sorted(e["name"] for e in bridge.list_dir_path("/"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", help="large file driver, default pfs3aio-largefile in the repository root")
    ap.add_argument("--size", default="6Gi", help="image size, must hold the file")
    ap.add_argument("--blocksize", type=int, default=4096,
                    help="logical block size of the volume (default %(default)s)")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--debug", action="store_true")
    a = ap.parse_args()

    driver = a.driver or os.path.join(ROOT, "pfs3aio-largefile")
    if not os.path.isfile(driver):
        sys.exit("no driver: " + driver + " (make largefile)")
    tmp = tempfile.mkdtemp(prefix="lf64.", dir=scratch_dir())
    image = os.path.join(tmp, "big.hdf")
    print("%s, image %s" % (driver, a.size))
    make_image(image, a.size, driver, a.blocksize)

    from pathlib import Path
    b = open_bridge(Path(image), Path(driver), a.debug)
    flag = largefile_flag(b, False)
    if flag is None or flag[0] == 0:
        sys.exit("the driver does not know ACTION_SET_LARGEFILE: not a large file build")
    if not flag[1]:
        flag = largefile_flag(b, True)
        if flag is None or flag[0] == 0:
            sys.exit("cannot switch the large file flag on: with %d byte blocks the volume is PFS1 "
                     "unless the driver formats PFS2 itself (LARGE_FILE_SIZE 2)" % a.blocksize)
        check("switch the large file flag on", flag, (DOSTRUE32, 1))
    # freeing gigabytes of blocks keeps the handler busy for millions of cycles
    run_until = b._run_until_replies
    b._run_until_replies = lambda max_iters=20000, **kw: run_until(max_iters=max_iters, **kw)
    pk = Packets(b)
    try:
        run(b, pk)
    except Exception as e:
        # an error in the driver run is a failed check, with what it was
        traceback.print_exc()
        check("the test ran to its end", "%s: %s" % (type(e).__name__, e), "no exception")
    finally:
        b.flush_volume()
        b.close()
    if not a.keep:
        os.remove(image)
        os.rmdir(tmp)
    report("%d byte blocks, %s" % (a.blocksize, os.path.basename(driver)))
    sys.exit(1 if failed else 0)


def run(b, pk):
    free0 = None
    di = b.get_disk_info()
    if di:
        free0 = di["num_blocks"] - di["num_blocks_used"]
    # neighbours around the big file in the directory block
    small = {"A1": pattern(1, 700), "A2": pattern(2, 1300), "A3": pattern(3, 90)}
    for n, d in small.items():
        check("write " + n, put(b, n, d), True)

    fh, _ = b.open_file("/BIG", os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
    head = pattern(4, 4096)
    check("write head", b.write_handle(fh, head), len(head))
    check("size is 4096", pk.size(fh), (DP64_INIT, 4096, 0))
    # created after BIG, so its entry follows BIG's: what a longer BIG entry would overwrite
    small["Z1"] = pattern(6, 400)
    check("write Z1 behind BIG", put(b, "Z1", small["Z1"]), True)

    big = FOUR_G + 1000
    check("grow past 4G", pk.setsize(fh, big), (DP64_INIT, DOSTRUE, 0))
    check("size past 4G", pk.size(fh), (DP64_INIT, big, 0))

    cross = FOUR_G - 100
    check("seek to 4G - 100", pk.seek(fh, cross), (DP64_INIT, DOSTRUE, 0))
    check("position", pk.pos(fh), (DP64_INIT, cross, 0))
    mid = pattern(5, 300)
    check("write across 4G", b.write_handle(fh, mid), len(mid))
    check("position after", pk.pos(fh), (DP64_INIT, cross + 300, 0))
    check("size unchanged", pk.size(fh), (DP64_INIT, big, 0))
    check("seek from end", pk.seek(fh, -1000, OFFSET_END), (DP64_INIT, DOSTRUE, 0))
    check("position from end", pk.pos(fh), (DP64_INIT, FOUR_G, 0))
    r = pk.seek(fh, big + 1)
    check("seek past end fails", r[0:2] if r else r, (DP64_INIT, 0))
    b.close_file(fh)
    b.flush_volume()

    st = b.stat_path("/BIG")
    check("examine size clamps to 32 bit", st and st["size"], 0xFFFFFFFF)
    check("directory lists all", names(b), sorted(list(small) + ["BIG"]))
    for n, d in small.items():
        check("read " + n, b.read_file("/" + n, len(d), 0), d)

    ex = Examine64(b)
    r = ex.path("BIG")
    check("examine64 size", r and r["size"], big)
    check("examine64 name and type", r and (r["name"], r["type"]), ("BIG", -3))
    check("examine64 small file", (ex.path("A2") or {}).get("size"), 1300)
    check("examine64 fib_Size is 0 above 2G", r and r["size32"], 0)
    check("examine64 fib_Size below 2G", (ex.path("A2") or {}).get("size32"), 1300)
    check("examine64 ext flags", r and r["extflags"], 0)
    r32 = ex.path32("BIG")
    check("examine64 date and name as the 32 bit examine", r and (r["date"], r["name"]), r32 and (r32["date"], r32["name"]))
    fh, _ = b.open_file("/BIG", os.O_RDONLY)
    r = ex.handle(fh)
    check("examinefh64 size", r and r["size"], big)
    b.close_file(fh)
    want = dict((n, len(d)) for n, d in small.items())
    want["BIG"] = big
    listing = ex.walk()
    check("examine next64 names", sorted(n for n, _ in listing), sorted(want))
    check("examine next64 sizes", dict(listing), want)

    fh, _ = b.open_file("/BIG", os.O_RDWR)
    check("size after reopen", pk.size(fh), (DP64_INIT, big, 0))
    check("seek to 4G - 100 again", pk.seek(fh, cross), (DP64_INIT, DOSTRUE, 0))
    check("read across 4G", b.read_handle(fh, 300), mid)
    check("seek to 0", pk.seek(fh, 0), (DP64_INIT, DOSTRUE, 0))
    check("read head", b.read_handle(fh, 4096), head)

    # below 4G again: the entry keeps its fsizex field, now zero
    below = FOUR_G - 1000
    check("shrink below 4G", pk.setsize(fh, below), (DP64_INIT, DOSTRUE, 0))
    check("size below 4G", pk.size(fh), (DP64_INIT, below, 0))
    b.close_file(fh)
    b.flush_volume()
    st = b.stat_path("/BIG")
    check("examine size below 4G", st and st["size"], below)
    check("directory after shrink", names(b), sorted(list(small) + ["BIG"]))
    for n, d in small.items():
        check("read " + n + " after shrink", b.read_file("/" + n, len(d), 0), d)
    fh, _ = b.open_file("/BIG", os.O_RDWR)
    check("read head again", b.read_handle(fh, 4096), head)
    check("grow past 4G again", pk.setsize(fh, big), (DP64_INIT, DOSTRUE, 0))
    check("size past 4G again", pk.size(fh), (DP64_INIT, big, 0))
    b.close_file(fh)
    b.flush_volume()

    # rename and delete the file while it is large
    root, _ = b.locate(0, "")
    res = b.rename_object(root, "BIG", root, "BIGGER_NAME_FOR_A_BIG_FILE")
    check("rename large file", res[0] != 0, True)
    fh, _ = b.open_file("/BIGGER_NAME_FOR_A_BIG_FILE", os.O_RDONLY)
    check("size after rename", pk.size(fh), (DP64_INIT, big, 0))
    b.close_file(fh)
    for n, d in small.items():
        check("read " + n + " after rename", b.read_file("/" + n, len(d), 0), d)

    def make_link(name, to):
        target, _ = b.locate(0, to)
        with b._lock:
            _, link_name = b._alloc_bstr(name)
            b.launcher.send_packet(b.state, ACTION_MAKE_LINK, [root, link_name, target, LINK_HARD])
            r = b._run_until_replies()
        b.free_lock(target)
        return bool(r) and r[-1][2] != 0

    # the directory lists a link with its own copy of the size
    bigname = "BIGGER_NAME_FOR_A_BIG_FILE"
    check("hard link to the large file", make_link("LNK", bigname), True)
    sizes = {e["name"]: e["size"] for e in b.list_dir_path("/")}
    check("listed size of the link", sizes.get("LNK"), 0xFFFFFFFF)
    res = b.delete_object(root, "LNK")
    check("delete the link", res[0] != 0, True)

    # deleting the original promotes a link to the file
    check("second hard link", make_link("LNK2", bigname), True)
    res = b.delete_object(root, bigname)
    check("delete the original", res[0] != 0, True)
    b.flush_volume()
    check("directory after promotion", names(b), sorted(list(small) + ["LNK2"]))
    fh, _ = b.open_file("/LNK2", os.O_RDONLY)
    check("size of the promoted link", pk.size(fh), (DP64_INIT, big, 0))
    check("read head of the promoted link", b.read_handle(fh, 4096), head)
    b.close_file(fh)
    res = b.delete_object(root, "LNK2")
    check("delete large file", res[0] != 0, True)
    b.flush_volume()
    check("directory after delete", names(b), sorted(small))
    for n, d in small.items():
        check("read " + n + " after delete", b.read_file("/" + n, len(d), 0), d)
    di = b.get_disk_info()
    if di and free0 is not None:
        # a deleted file stays in the deldir, so this is only reported
        print("free blocks: %d at start, %d at end" % (free0, di["num_blocks"] - di["num_blocks_used"]))
    b.free_lock(root)


if __name__ == "__main__":
    main()
