#!/usr/bin/env python3
"""Directory, link and label operations on a pfs3aio volume, under AmiFUSE.

The `amifuse` command line can only read and write whole files, and mounting
needs libfuse. So this talks to the driver the way dos.library does: it builds
the same packet-level bridge `amifuse write` uses and sends the AmigaDOS
packets itself (mkdir, rename, delete, hard links, relabel, ...).

    python3 dirops.py IMAGE --driver DRIVER

The verdict is what the volume really contains: after the operations the
volume is closed and mounted again from the image, and the whole tree, every
file's bytes and the volume name are compared with a model kept here. Refusals
(an existing name, a non-empty directory, a rename into itself, an invalid
label) must come with an error AND leave the tree as it was.

Prints a line per check and, last,
  DIROPS-TREE files=<n> dirs=<n> bytes=<n>     (what the model expects)
  DIROPS-SUMMARY checks=<n> failures=<n>
Exit status is 0 only if every check passed.
"""
import argparse
import os
import random
import sys
from contextlib import ExitStack
from pathlib import Path

from timer_device import DeferredTimers

# Same timer shim as fi.py: immediate timer replies are not valid hardware behaviour.
try:
    import amifuse.timer_device  # noqa: F401
except ImportError:
    _timer_patches = ExitStack()
    _timers = DeferredTimers()
    _timers.install(_timer_patches, real_time=True)

import amifuse.fuse_fs as ff

ACTION_RENAME_DISK = 9
ACTION_MAKE_LINK = 1021
LINK_HARD = 0
ERROR_OBJECT_EXISTS = 203
ERROR_OBJECT_NOT_FOUND = 205
ERROR_DIRECTORY_NOT_EMPTY = 216

checks = 0
failures = 0


def check(ok, what):
    global checks, failures
    checks += 1
    if not ok:
        failures += 1
        print("  FAIL: %s" % what, flush=True)
    return ok


def data(seed, n):
    return random.Random(seed).randbytes(n)


def split(path):
    parent, _, name = path.rpartition("/")
    return (parent or "/"), name


class Vol:
    def __init__(self, image, driver, read_only):
        ns = argparse.Namespace(image=Path(image), partition=None, driver=Path(driver),
                                block_size=None, debug=False, json=False)
        self.br, self.temp = ff._create_bridge_from_args(ns, "ls" if read_only else "write",
                                                         read_only=read_only)
        self.read_only = read_only

    def close(self):
        if not self.read_only:
            self.br.flush_volume()
        ff._cleanup_bridge(self.br, self.temp)

    def _forget(self):
        # the bridge remembers "not found" answers; the driver is what is under test
        getattr(self.br, "_neg_cache", {}).clear()

    def _locks(self, path):
        lock, res2, locks = self.br.locate_path(path)
        return lock, res2, locks

    def _free(self, locks):
        for lock in reversed(locks):
            self.br.free_lock(lock)

    def _packet(self, action, args, label):
        with self.br._lock:
            pkt, _ = self.br.launcher.send_packet(self.br.state, action, args)
            reply = self.br._run_until_reply(pkt, label)
        return reply if reply is not None else (0, -1)

    def _bstr(self, text):
        with self.br._lock:
            return self.br._alloc_bstr(text)[1]

    def mkdir(self, path):
        self._forget()
        parent, name = split(path)
        lock, res2, locks = self._locks(parent)
        try:
            if lock == 0 and parent != "/":
                return res2 or -1
            new, res2 = self.br.create_dir(lock, name)
            if new:
                self.br.free_lock(new)
                return 0
            return res2 or -1
        finally:
            self._free(locks)

    def put(self, path, payload):
        self._forget()
        opened, res2 = self.br.open_file_reply(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
        if opened is None:
            return res2 or -1
        fh, dir_lock = opened
        rc = 0
        for off in range(0, len(payload), 16384):
            chunk = payload[off:off + 16384]
            n, res2 = self.br.write_handle_reply(fh, chunk)
            if n != len(chunk):
                rc = res2 or -1
                break
        self.br.close_file(fh)
        self.br.free_lock(dir_lock)
        return rc

    def get(self, path):
        self._forget()
        opened, _ = self.br.open_file_reply(path, os.O_RDONLY)
        if opened is None:
            return None
        fh, dir_lock = opened
        payload = self.br.read_handle(fh, 1 << 26)
        self.br.close_file(fh)
        self.br.free_lock(dir_lock)
        return payload

    def ls(self, path):
        self._forget()
        return self.br.list_dir_path(path)

    def delete(self, path):
        self._forget()
        parent, name = split(path)
        lock, res2, locks = self._locks(parent)
        try:
            if lock == 0 and parent != "/":
                return res2 or -1
            res1, res2 = self.br.delete_object(lock, name)
            return 0 if res1 else (res2 or -1)
        finally:
            self._free(locks)

    def rename(self, old, new):
        self._forget()
        sp, sn = split(old)
        dp, dn = split(new)
        sl, r1, slocks = self._locks(sp)
        dl, r2, dlocks = self._locks(dp)
        try:
            if (sl == 0 and sp != "/") or (dl == 0 and dp != "/"):
                return r1 or r2 or -1
            res1, res2 = self.br.rename_object(sl, sn, dl, dn)
            return 0 if res1 else (res2 or -1)
        finally:
            self._free(slocks)
            self._free(dlocks)

    def hardlink(self, link, target):
        self._forget()
        lp, ln = split(link)
        pl, r1, plocks = self._locks(lp)
        tl, r2, tlocks = self._locks(target)
        try:
            if (pl == 0 and lp != "/") or tl == 0:
                return r1 or r2 or -1
            res1, res2 = self._packet(ACTION_MAKE_LINK, [pl, self._bstr(ln), tl, LINK_HARD], "link")
            return 0 if res1 else (res2 or -1)
        finally:
            self._free(plocks)
            self._free(tlocks)

    def relabel(self, name):
        res1, res2 = self._packet(ACTION_RENAME_DISK, [self._bstr(name)], "relabel")
        return 0 if res1 else (res2 or -1)

    def snapshot(self, path="/", depth=0):
        """{path: None for a directory, bytes for a file}; None where a read failed."""
        out = {}
        for e in self.ls(path):
            p = path.rstrip("/") + "/" + e["name"]
            if e["dir_type"] > 0:
                out[p] = None
                if depth < 8:
                    out.update(self.snapshot(p, depth + 1))
            else:
                out[p] = self.get(p)
        return out


def expect(rc, want, what):
    """rc is 0 for success, else the AmigaDOS error. want: 0, an error, or 'fail'."""
    if want == "fail":
        return check(rc != 0, "%s must be refused (it returned success)" % what)
    return check(rc == want, "%s: expected %s, got %s" % (what, "success" if want == 0 else "error %d" % want,
                                                          "success" if rc == 0 else "error %d" % rc))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--driver", required=True)
    args = ap.parse_args()

    model = {}                       # path -> None (directory) or bytes (file)

    def sub(path):                   # every model entry below path
        return {p: v for p, v in model.items() if p.startswith(path + "/")}

    v = Vol(args.image, args.driver, read_only=False)
    base = v.snapshot()
    model.update(base)               # a fixture that is not empty: what is there must survive

    print("  directories", flush=True)
    for d in ("/a", "/a/b", "/a/b/c", "/d"):
        if expect(v.mkdir(d), 0, "mkdir " + d):
            model[d] = None
    expect(v.mkdir("/a/b"), ERROR_OBJECT_EXISTS, "mkdir of an existing directory")
    expect(v.mkdir("/nodir/x"), "fail", "mkdir below a missing directory")

    files = {"/a/f1": data(1, 777), "/a/b/f2": data(2, 3000), "/a/b/c/f3": data(3, 70000),
             "/top": data(4, 100), "/empty": b""}
    for p, payload in files.items():
        if expect(v.put(p, payload), 0, "create " + p):
            model[p] = payload
    expect(v.put("/a/b", b"x"), "fail", "creating a file over a directory")

    print("  delete", flush=True)
    expect(v.delete("/a"), ERROR_DIRECTORY_NOT_EMPTY, "deleting a non-empty directory")
    expect(v.delete("/missing"), ERROR_OBJECT_NOT_FOUND, "deleting a missing name")

    print("  rename and move", flush=True)
    if expect(v.rename("/a/f1", "/d/f1moved"), 0, "move a file to another directory"):
        model["/d/f1moved"] = model.pop("/a/f1")
    expect(v.rename("/top", "/d/f1moved"), ERROR_OBJECT_EXISTS, "rename onto an existing name")
    if expect(v.rename("/a/b", "/d/b2"), 0, "move a directory with its contents"):
        for p in [p for p in model if p == "/a/b" or p.startswith("/a/b/")]:
            model["/d/b2" + p[len("/a/b"):]] = model.pop(p)
    expect(v.rename("/d", "/d/b2/inner"), "fail", "moving a directory into itself")
    expect(v.rename("/d", "/d/b2/c/deeper"), "fail", "moving a directory into its own grandchild")
    if expect(v.rename("/top", "/top2"), 0, "rename in place"):
        model["/top2"] = model.pop("/top")

    print("  hard links", flush=True)
    if expect(v.hardlink("/d/link_to_top", "/top2"), 0, "hard link to a file"):
        model["/d/link_to_top"] = model["/top2"]
    expect(v.hardlink("/d/link_to_top", "/top2"), ERROR_OBJECT_EXISTS, "hard link onto an existing name")
    check(v.get("/d/link_to_top") == model["/top2"], "the link reads the target's bytes")
    if expect(v.delete("/top2"), 0, "delete the link target"):
        del model["/top2"]
    check(v.get("/d/link_to_top") == model["/d/link_to_top"],
          "the link still reads the data after its target is deleted")
    if expect(v.rename("/d/link_to_top", "/was_a_link"), 0, "rename the surviving link"):
        model["/was_a_link"] = model.pop("/d/link_to_top")
    if expect(v.hardlink("/dlink", "/d/b2"), 0, "hard link to a directory"):
        for p, val in list(sub("/d/b2").items()):
            model["/dlink" + p[len("/d/b2"):]] = val
        model["/dlink"] = None
    check(v.get("/dlink/f2") == model.get("/dlink/f2"), "a file is reachable through the directory link")
    if expect(v.delete("/dlink"), 0, "delete a directory link"):
        for p in [p for p in model if p == "/dlink" or p.startswith("/dlink/")]:
            del model[p]
    check(v.get("/d/b2/f2") == model["/d/b2/f2"], "deleting a directory link keeps the target's contents")

    print("  many entries in one directory", flush=True)
    if expect(v.mkdir("/many"), 0, "mkdir /many"):
        model["/many"] = None
    for i in range(150):
        p = "/many/file%03d_%s" % (i, "x" * (i % 17))
        payload = data(100 + i, (i * 13) % 700)
        if v.put(p, payload) == 0:
            model[p] = payload
        else:
            check(False, "create " + p)
    check(len([p for p in model if p.startswith("/many/")]) == 150, "all 150 files created")
    for i in range(1, 150, 2):
        p = "/many/file%03d_%s" % (i, "x" * (i % 17))
        if expect(v.delete(p), 0, "delete " + p):
            del model[p]
    for i in range(150, 170):
        p = "/many/new%03d" % i
        payload = data(100 + i, 50 + i)
        if v.put(p, payload) == 0:
            model[p] = payload
        else:
            check(False, "create " + p)

    print("  an emptied directory", flush=True)
    if expect(v.mkdir("/gone"), 0, "mkdir /gone"):
        model["/gone"] = None
    names = ["/gone/g%03d" % i for i in range(60)]
    for p in names:
        if v.put(p, data(7, 40)) != 0:
            check(False, "create " + p)
    for p in names:
        if v.delete(p) != 0:
            check(False, "delete " + p)
    if expect(v.delete("/gone"), 0, "delete the emptied directory"):
        del model["/gone"]

    print("  volume label", flush=True)
    expect(v.relabel("Renamed"), 0, "rename the volume")
    for bad in ("", "a:b", "a/b", "x" * 31):
        expect(v.relabel(bad), "fail", "rename the volume to %r" % bad)

    check(v.snapshot() == model, "the tree before closing is what the model expects")
    v.close()

    print("  after remount", flush=True)
    r = Vol(args.image, args.driver, read_only=True)
    got = r.snapshot()
    missing = sorted(set(model) - set(got))
    extra = sorted(set(got) - set(model))
    wrong = sorted(p for p in model if p in got and got[p] != model[p])
    check(not missing, "missing after remount: %s" % missing[:5])
    check(not extra, "unexpected after remount: %s" % extra[:5])
    check(not wrong, "contents differ after remount: %s" % wrong[:5])
    name = r.br.volume_name()
    check(name == "Renamed", "the volume name after remount is %r, expected 'Renamed'" % name)
    r.close()

    files_n = sum(1 for p, val in model.items() if val is not None)
    dirs_n = sum(1 for p, val in model.items() if val is None)
    size = sum(len(val) for val in model.values() if val is not None)
    print("DIROPS-TREE files=%d dirs=%d bytes=%d" % (files_n, dirs_n, size))
    print("DIROPS-SUMMARY checks=%d failures=%d" % (checks, failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:
        sys.stderr.write("DIROPS-EXCEPTION: %s: %s\n" % (type(e).__name__, e))
        import traceback
        traceback.print_exc()
        sys.exit(99)
