#!/usr/bin/env python3
"""Deleting the original of a hard link keeps the data under the link.

    python3 tests/m68k/hardlink.py --driver compare/gcc13/020/pfs3aio [--cpu 68020]

Makes two hard links to a file and deletes the file. The first link has to
become the file, with its size and content, and the second link has to follow
it. A compiler that lets CreateLink skip the link field of the file fails this
by leaving the link dangling. Needs amifuse and rdbtool, like the other tests
here.
"""
import argparse
import os
import sys
import tempfile
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import largefile64 as L  # noqa: E402

SIZE = 700


def make_link(b, root, name, to):
    target, _ = b.locate(0, to)
    with b._lock:
        _, link_name = b._alloc_bstr(name)
        b.launcher.send_packet(b.state, L.ACTION_MAKE_LINK, [root, link_name, target, L.LINK_HARD])
        r = b._run_until_replies()
    b.free_lock(target)
    return bool(r) and r[-1][2] != 0


def run(b):
    check = L.check
    data = L.pattern(1, SIZE)
    check("write F", L.put(b, "F", data), True)
    root, _ = b.locate(0, "")
    check("link LNK", make_link(b, root, "LNK", "F"), True)
    check("link LNK2", make_link(b, root, "LNK2", "F"), True)
    check("read through LNK", b.read_file("/LNK", SIZE, 0), data)

    res = b.delete_object(root, "F")
    check("delete F", res[0] != 0, True)
    b.flush_volume()
    check("directory", L.names(b), ["LNK", "LNK2"])
    ents = {e["name"]: e for e in b.list_dir_path("/")}
    check("LNK is a file", ents["LNK"].get("dir_type"), -3)
    check("LNK2 is a link", ents["LNK2"].get("dir_type"), -4)
    check("size of LNK", ents["LNK"].get("size"), SIZE)
    check("read LNK", b.read_file("/LNK", SIZE, 0), data)
    check("read LNK2", b.read_file("/LNK2", SIZE, 0), data)

    check("delete LNK", b.delete_object(root, "LNK")[0] != 0, True)
    b.flush_volume()
    check("LNK2 after LNK", L.names(b), ["LNK2"])
    ents = {e["name"]: e for e in b.list_dir_path("/")}
    check("LNK2 is a file", ents["LNK2"].get("dir_type"), -3)
    check("read LNK2 alone", b.read_file("/LNK2", SIZE, 0), data)
    b.free_lock(root)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--driver", required=True)
    ap.add_argument("--cpu", default="68020")
    ap.add_argument("--size", default="32Mi")
    ap.add_argument("--debug", action="store_true")
    a = ap.parse_args()

    if not os.path.isfile(a.driver):
        sys.exit("no driver: " + a.driver)
    tmp = tempfile.mkdtemp(prefix="hardlink.", dir=os.path.join(L.TESTS, ".tmp")
                           if os.path.isdir(os.path.join(L.TESTS, ".tmp")) else None)
    image = os.path.join(tmp, "link.hdf")
    L.make_image(image, a.size, a.driver, a.cpu)
    b = L.open_bridge(Path(image), Path(a.driver), a.cpu, a.debug)
    try:
        run(b)
    finally:
        b.flush_volume()
        b.close()
    os.remove(image)
    os.rmdir(tmp)
    print("== hardlink: %d failed ==" % L.failed)
    sys.exit(1 if L.failed else 0)


if __name__ == "__main__":
    main()
