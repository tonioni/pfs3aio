#!/usr/bin/env python3
"""pfsdoctor on damaged PFS3 volumes in FS-UAE.

    python3 tests/pfsdoctor_damage.py --handler pfs3aio --doctor pfsdoctor/pfsdoctor [--scenario NAME ...]
    python3 tests/pfsdoctor_damage.py --handler pfs3aio-largefile --doctor pfsdoctor/pfsdoctor \
        --largefile tools/setlargefile

Runs in an amiga-fsuae OS image: AMIGA_SYSTEM is the system directory it
boots, AMIGA_KICKSTART the ROM and AMIGA_KICKSTART_EXT its second half, if
any. The first boot formats a volume with the handler from the RDB, fills it
and checks it. Each scenario damages a copy of that volume, then one boot runs
pfsdoctor CHECK, REPAIR and CHECK again and lists the files. A scenario passes
when the first CHECK reports the damage, REPAIR fixes it, the second CHECK is
clean and no file is lost but those of a damaged dirblock or entry. On an
undamaged volume both CHECKs are clean and REPAIR fixes nothing. The deldir
slots of the rootblock extension have to match the deldir size after every
scenario. With --largefile the volume has 4096 byte blocks, so it is PFS\\2,
and setlargefile turns on its large file mode.
"""

import argparse
import hashlib
import os
import queue
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pfs3img import Image, u16, u32

DOSTYPE = "0x50465303"
ROOTBLOCK = 2
DELENTRIES = 31
ID_PFS2_DISK = 0x50465302
MODE_LARGEFILE = 2048

FILL = """\
SYS:System/Format DRIVE PFS0: NAME Test QUICK NOICONS <RAM:enter >OUT:format.txt
{largefile}Copy C: PFS0:c ALL QUIET
MakeDir PFS0:d
{many}
Copy C:List PFS0:gone
Delete PFS0:gone QUIET
OUT:pfsdoctor PFS0: CHECK NONINTERACTIVE >OUT:check.txt
"""

DOCTOR = """\
OUT:pfsdoctor PFS0: CHECK NONINTERACTIVE >OUT:check1.txt
OUT:pfsdoctor PFS0: REPAIR NONINTERACTIVE >OUT:repair.txt
OUT:pfsdoctor PFS0: CHECK NONINTERACTIVE >OUT:check2.txt
"""

LIST = 'List PFS0: ALL FILES LFORMAT "%p%n %l" >OUT:list.txt\n'


# Damage on the host: each takes the image and returns what it changed and,
# for a damaged dirblock, the paths of the files it held.


def reserved_leak(path):
    """A free reserved block marked used."""
    img = Image(path)
    n = (img.lastreserved - img.firstreserved) // img.rescluster + 1
    blk = next(
        b
        for b in (img.firstreserved + t * img.rescluster for t in range(n - 1, 0, -1))
        if img.is_free(b)
    )
    img.close()
    reserved_bit(path, blk, False)
    return f"reserved block {blk} marked used"


def reserved_unallocated(path):
    """The first dirblock of /c marked free."""
    img = Image(path)
    blk = img.chain(img.lookup("c"))[0][1]
    img.close()
    reserved_bit(path, blk, True)
    return f"dirblock {blk} of /c marked free"


def data_leak(path):
    """A free data block marked used."""
    blk = main_bit(path, None)
    main_bit(path, blk, False)
    return f"data block {blk} marked used"


def data_unallocated(path):
    """The first data block of a file in /c marked free."""
    img = Image(path)
    an = next(
        an
        for _, _, ents in img.chain(img.lookup("c"))
        for _, typ, an in ents
        if typ < 0
    )
    blk = img.anode(an)[1]
    img.close()
    main_bit(path, blk, True)
    return f"data block {blk} of anode {an} marked free"


class BootStalled(RuntimeError):
    """A boot that did not reach its Startup-Sequence."""


def bootblock_id(path):
    """The bootblock lost its disk type."""
    poke(path, 0, b"\0\0\0\0")
    return "bootblock id cleared"


def volume_name(path):
    """The rootblock lost the volume name."""
    img = Image(path)
    off = ROOTBLOCK * img.secsize + 20
    img.close()
    poke(path, off, b"\0")
    return "volume name length 0"


def deldir_block(path):
    """The first deldir block lost its id."""
    img = Image(path)
    blk = u32(img.reserved(img.extension), 144)
    off = blk * img.secsize
    img.close()
    poke(path, off, b"\0\0")
    return f"id of deldir block {blk} cleared"


def extension_id(path):
    """The rootblock extension lost its id."""
    img = Image(path)
    off = img.extension * img.secsize
    img.close()
    poke(path, off, b"\0\0")
    return "rootblock extension id cleared"


def dirblock_id(path, nr=1):
    """The second dirblock of /d lost its id."""
    img = Image(path)
    _, blk, ents = img.chain(img.lookup("d"))[nr]
    off = blk * img.secsize
    img.close()
    poke(path, off, b"\0\0")
    return f"id of dirblock {blk} of /d cleared", {
        "PFS0:d/" + name for name, _, _ in ents
    }


def dirblock_head(path):
    """The first dirblock of /d lost its id."""
    return dirblock_id(path, 0)


def deldir_size(path):
    """The rootblock extension gives the deldir more blocks than it has, and
    its roving pointer points behind the real ones."""
    img = Image(path)
    off = img.extension * img.secsize + 52
    img.close()
    poke(path, off, struct.pack(">HH", 30 * DELENTRIES, 31))
    return f"deldirsize 31, roving {30 * DELENTRIES}"


def deldir_roving(path):
    """The roving pointer of the deldir points behind its blocks, the size
    is right."""
    img = Image(path)
    off = img.extension * img.secsize + 52
    size = u16(img.reserved(img.extension), 54)
    img.close()
    poke(path, off, struct.pack(">H", size * DELENTRIES))
    return f"roving {size * DELENTRIES}"


def extension_id_uncommitted(path, ahead=1):
    """The rootblock extension lost its id, and a free reserved block holds
    a superblock from an update that never committed: newer than the
    rootblock, with an empty index."""
    img = Image(path)
    rootds = u32(img.sectors(ROOTBLOCK, 1), 8)
    sup = img.superindex[0]
    blk = next(
        b
        for b in range(
            img.lastreserved - img.rescluster, img.firstreserved, -img.rescluster
        )
        if img.is_free(b)
    )
    data = bytearray(img.reserved(sup)[:12]) + bytes(img.resblksize - 12)
    struct.pack_into(">I", data, 4, rootds + ahead)
    ext, secsize = img.extension, img.secsize
    img.close()
    poke(path, blk * secsize, bytes(data))
    poke(path, ext * secsize, b"\0\0")
    return f"rootblock extension id cleared, uncommitted superblock {blk}"


def extension_id_lost_rext(path):
    """The rootblock extension lost its id, and the newest backup is the
    extension of an update that never committed: free blocks with the
    datestamp of the rootblock, its deldir block 0 empty."""
    img = Image(path)
    rootds = u32(img.sectors(ROOTBLOCK, 1), 8)
    ext = img.extension
    rext = bytearray(img.reserved(ext))
    dd = img.reserved(u32(rext, 144))
    free = [
        b
        for b in range(
            min(ext + 64, img.lastreserved), img.firstreserved, -img.rescluster
        )
        if img.is_free(b)
    ]
    newext, newdd = free[0], free[1]
    deldir = bytearray(dd[:32]) + bytes(img.resblksize - 32)
    struct.pack_into(">I", deldir, 4, rootds)
    struct.pack_into(">I", rext, 8, rootds)
    struct.pack_into(">I", rext, 144, newdd)
    secsize = img.secsize
    img.close()
    poke(path, newdd * secsize, bytes(deldir))
    poke(path, newext * secsize, bytes(rext))
    poke(path, ext * secsize, b"\0\0")
    return f"rootblock extension id cleared, uncommitted extension {newext} with deldir {newdd}"


def extension_id_lost_update(path):
    """As extension_id_uncommitted, but the update was lost before a
    remount, whose first commit wrote the rootblock with the datestamp of
    the lost update."""
    return extension_id_uncommitted(path, ahead=0)


def filesize_high(path):
    """A file in /c gets bits 32-47 of its size in the extra fields of its
    directory entry, which no longer match its blocks."""
    img = Image(path)
    for _, blk, _ in img.chain(img.lookup("c")):
        b = bytearray(img.reserved(blk))
        o, end, hit = 20, 20, None
        while end < len(b) and b[end]:
            end += b[end]
        while o < end:
            size, typ, nl = b[o], struct.unpack_from(">b", b, o + 1)[0], b[o + 17]
            bare = (20 + nl + b[o + 18 + nl]) & ~1
            # no extra fields, or only their flags word, which is then 0
            if (
                typ < 0
                and (size == bare or size == bare + 2 and not u16(b, o + bare))
                and end + 4 < len(b)
            ):
                hit = o
                break
            o += size
        if hit is not None:
            break
    else:
        raise RuntimeError("no file entry in /c with room for extra fields")
    name = b[hit + 18 : hit + 18 + b[hit + 17]].decode("latin-1")
    # fsizex is the eleventh word of the extra fields, the flags word ends the entry
    entry = b[hit : hit + bare] + struct.pack(">HH", 1, 1 << 10)
    entry[0] = len(entry)
    b[hit:end] = entry + b[hit + size : end]
    off = blk * img.secsize
    img.close()
    poke(path, off, bytes(b[: end + len(entry) - size + 1]))
    return f"fsizex of /c/{name} 1", {"PFS0:c/" + name}


def deldir_filesize_high(path):
    """The deleted file gets bits 32-47 of its size in its deldir entry,
    which no longer match its blocks."""
    img = Image(path)
    blk = u32(img.reserved(img.extension), 144)
    dd = img.reserved(blk)
    o = next(
        32 + 32 * i
        for i in range((len(dd) - 32) // 32)
        if u32(dd, 32 + 32 * i) and dd[46 + 32 * i] < 16
    )
    name = dd[o + 15 : o + 15 + dd[o + 14]].decode("latin-1")
    off = blk * img.secsize + o + 30
    img.close()
    poke(path, off, struct.pack(">H", 1))
    return f"fsizex of deleted {name} 1"


def deldir_slots(path):
    """What is wrong with the deldir slots of the rootblock extension: a
    slot of the deldir has to name a used block, a slot behind it none."""
    img = Image(path)
    ext = img.reserved(img.extension)
    size, errs = u16(ext, 54), []
    for i in range(32):
        blk = u32(ext, 144 + 4 * i)
        if i < size and (not blk or img.is_free(blk)):
            errs.append(
                f"deldir slot {i} of {size} names "
                + (f"free block {blk}" if blk else "no block")
            )
        elif i >= size and blk:
            errs.append(
                f"deldir slot {i} behind the deldir of {size} names block {blk}"
            )
    roving = u16(ext, 52)
    if roving and roving >= size * DELENTRIES:
        errs.append(f"deldir roving {roving} behind the deldir of {size}")
    img.close()
    return errs


def deldirsize(path):
    img = Image(path)
    n = u16(img.reserved(img.extension), 54)
    img.close()
    return n


def deleted(path):
    """Names of the files in the deldir."""
    img = Image(path)
    ext = img.reserved(img.extension)
    names = []
    for i in range(min(u16(ext, 54), 32)):
        dd = img.reserved(u32(ext, 144 + 4 * i))
        for o in range(32, len(dd) - 31, 32):
            if u32(dd, o):
                names.append(dd[o + 15 : o + 15 + dd[o + 14]].decode("latin-1"))
    img.close()
    return sorted(names)


def superindex(path):
    img = Image(path)
    n = [b for b in img.superindex if b]
    img.close()
    return n


def dir_loop(path):
    """The last anode of /d links back to its first."""
    img = Image(path, write=True)
    head = img.lookup("d")
    last = img.chain(head)[-1][0]
    img.set_next(last, head)
    img.close()
    return f"anode {last} of /d links to {head}"


def reserved_bit(path, blk, free):
    img = Image(path)
    t = (blk - img.firstreserved) // img.rescluster
    off = ROOTBLOCK * img.secsize + 512 + 12 + t // 32 * 4
    img.close()
    set_bit(path, off, t % 32, free)


def main_bit(path, blk, free=False):
    """Sets the main bitmap bit of blk (set is free); with blk None returns
    a free block."""
    img = Image(path)
    rb = img.sectors(ROOTBLOCK, 1)
    perblock = (img.resblksize - 12) // 4
    first = img.lastreserved + 1

    def word(n):
        seq, w = divmod(n, perblock)
        index = img.reserved(u32(rb, 96 + 4 * (seq // perblock)))
        return u32(index, 12 + 4 * (seq % perblock)) * img.secsize + 12 + 4 * w

    if blk is None:
        # disksize counts device sectors
        n = (u32(rb, 84) * img.devsize // img.secsize - first) // 64
        while True:
            img.f.seek(img.base + word(n))
            w = struct.unpack(">I", img.f.read(4))[0]
            if w:
                img.close()
                return first + n * 32 + next(b for b in range(32) if w >> (31 - b) & 1)
            n += 1
    n, bit = divmod(blk - first, 32)
    off = word(n)
    img.close()
    set_bit(path, off, bit, free)


def set_bit(path, off, bit, free):
    """Bit of the bitmap word at partition offset off: set is free."""
    img = Image(path)
    off += img.base
    img.close()
    with open(path, "r+b") as f:
        f.seek(off)
        w = struct.unpack(">I", f.read(4))[0]
        m = 1 << (31 - bit)
        f.seek(off)
        f.write(struct.pack(">I", w | m if free else w & ~m))


def poke(path, off, data):
    img = Image(path)
    off += img.base
    img.close()
    with open(path, "r+b") as f:
        f.seek(off)
        f.write(data)


# error: in the first CHECK; fixed: in REPAIR; same: a value of the image
# that REPAIR has to bring back to the one of the undamaged volume;
# largefile: only on a large file volume, the others never see fsizex;
# powercut: FS-UAE is killed right after a copy instead, as at a power cut,
# and nothing may need repair.
SCENARIOS = [
    {"name": "clean"},
    {"name": "powercut", "powercut": True},
    {
        "name": "reserved-leak",
        "damage": reserved_leak,
        "error": "reserved blocks not used but allocated",
        "fixed": "reserved blocks not used but allocated",
    },
    {
        "name": "reserved-unallocated",
        "damage": reserved_unallocated,
        "error": "reserved blocks used but not allocated",
        "fixed": "reserved blocks used but not allocated",
    },
    {
        "name": "data-leak",
        "damage": data_leak,
        "error": "data blocks not used but allocated",
        "fixed": "data blocks not used but allocated",
    },
    {
        "name": "data-unallocated",
        "damage": data_unallocated,
        "error": "data blocks used but not allocated",
        "fixed": "data blocks used but not allocated",
    },
    {
        "name": "bootblock-id",
        "damage": bootblock_id,
        "error": "Bootblock has wrong ID",
        "fixed": "Bootblock had wrong ID",
    },
    {
        "name": "volume-name",
        "damage": volume_name,
        "error": "Volume has no name",
        "fixed": "Volume had no name",
    },
    {
        "name": "extension-id",
        "damage": extension_id,
        "error": "Extension block has wrong id",
        "fixed": "with backup",
        "same": deleted,
        "requester": True,
    },
    {
        "name": "extension-id-uncommitted",
        "damage": extension_id_uncommitted,
        "error": "Extension block has wrong id",
        "fixed": "with backup",
        "same": superindex,
        "requester": True,
        "largefile": True,
    },
    {
        "name": "extension-id-lost-rext",
        "damage": extension_id_lost_rext,
        "error": "Extension block has wrong id",
        "fixed": "with backup",
        "same": deleted,
        "requester": True,
    },
    {
        "name": "extension-id-lost-update",
        "damage": extension_id_lost_update,
        "error": "Extension block has wrong id",
        "fixed": "with backup",
        "same": superindex,
        "requester": True,
        "largefile": True,
    },
    {
        "name": "deldir-block",
        "damage": deldir_block,
        "error": "Deldirblock error",
        "fixed": "Deldir block 0 removed",
    },
    {
        "name": "deldir-size",
        "damage": deldir_size,
        "error": "Deldir size incorrect",
        "fixed": "Deldir size incorrect",
        "same": deldirsize,
    },
    {
        "name": "deldir-roving",
        "damage": deldir_roving,
        "error": "Deldir roving pointer outside the deldir",
        "fixed": "Deldir roving pointer outside the deldir",
    },
    {
        "name": "dir-loop",
        "damage": dir_loop,
        "error": "Directory anode chain loops",
        "fixed": "chain loops, cut",
    },
    {
        "name": "dirblock-id",
        "damage": dirblock_id,
        "error": "Corrupt directory block",
        "fixed": "Removing corrupt directory block",
    },
    {
        "name": "dirblock-head",
        "damage": dirblock_head,
        "error": "Corrupt directory block",
        "fixed": "Removing corrupt directory block",
    },
    {
        "name": "filesize-high",
        "damage": filesize_high,
        "error": "Invalid filesize",
        "fixed": "Removing corrupt directory entry",
        "largefile": True,
    },
    {
        "name": "deldir-filesize-high",
        "damage": deldir_filesize_high,
        "error": "Delfile anode",
        "fixed": "Delfile anode",
        "largefile": True,
    },
]


class Machine:
    def __init__(self, work, tools, limit, slot=0):
        """tools are copied to OUT: of every boot. Each slot has its own
        system copy, for its Startup-Sequence, and its own X displays: a boot
        killed at a power cut leaves the lock of its display behind."""
        self.work, self.tools, self.limit = work, tools, limit
        self.sys = work / f"sys-{slot}"
        self.displays = iter(range(100 + 100 * slot, 200 + 100 * slot))
        if not self.sys.exists():
            shutil.copytree(os.environ["AMIGA_SYSTEM"], self.sys, symlinks=True)
            subprocess.run(["chmod", "-R", "u+w", str(self.sys)], check=True)
        self.rom = ["kickstart_file = {}".format(os.environ["AMIGA_KICKSTART"])]
        if os.environ.get("AMIGA_KICKSTART_EXT"):
            self.rom.append(
                "kickstart_ext_file = {}".format(os.environ["AMIGA_KICKSTART_EXT"])
            )

    def boot(self, name, hdf, script, kill=False, requester=False):
        """Boots the system with hdf on ide0, runs script and returns OUT:.
        FS-UAE gets SIGTERM and writes what it holds; with kill SIGKILL, which
        loses those writes, as at a power cut. With requester a requester is
        expected: when OUT: does not change for a while it gets left Amiga V.
        A boot that stops before its Startup-Sequence, as the emulator on a
        busy host at times does, runs once more."""
        try:
            return self.boot_once(name, hdf, script, kill, requester)
        except BootStalled as e:
            print(f"{e}, once more", flush=True)
            return self.boot_once(name, hdf, script, kill, requester)

    def boot_once(self, name, hdf, script, kill, requester):
        d = self.work / name
        shutil.rmtree(d, ignore_errors=True)
        out = d / "out"
        out.mkdir(parents=True)
        for tool in self.tools:
            shutil.copyfile(tool, out / tool.name)
        (self.sys / "S/Startup-Sequence").write_text(
            "FailAt 1000\nMakeDir RAM:T RAM:ENV\nAssign >NIL: ENV: RAM:ENV\nAssign >NIL: T: RAM:T\n"
            'Echo "" >RAM:enter\nEcho "start" >OUT:log.txt\n'
            + script
            + 'Echo "DONE" >OUT:done\n'
        )
        cfg = d / "run.fs-uae"
        cfg.write_text(
            "\n".join(
                [
                    "[fs-uae]",
                    "amiga_model = A1200",
                    "cpu = 68040-NOMMU",
                    "chip_memory = 2048",
                    "fast_memory = 8192",
                    *self.rom,
                    f"hard_drive_0 = {out}",
                    "hard_drive_0_label = OUT",
                    "hard_drive_0_priority = -128",
                    f"hard_drive_1 = {hdf}",
                    "hard_drive_1_type = rdb",
                    "hard_drive_1_controller = ide0",
                    f"hard_drive_2 = {self.sys}",
                    "hard_drive_2_label = Boot",
                    "hard_drive_2_priority = 10",
                    "joystick_port_1 = none",
                    "uae_cpu_speed = max",
                    f"base_dir = {d}",
                    "",
                ]
            )
        )
        with open(d / "fsuae.log", "wb") as log:
            p = subprocess.Popen(
                [
                    "xvfb-run",
                    "-n",
                    str(next(self.displays)),
                    "-f",
                    str(d / "xauth"),
                    "fs-uae",
                    str(cfg),
                ],
                stdout=log,
                stderr=log,
                start_new_session=True,
            )
        t0 = time.time()
        seen, keyed = None, t0
        while (
            not (out / "done").exists()
            and time.time() - t0 < self.limit
            and p.poll() is None
        ):
            time.sleep(0.2)
            now = sorted((f.name, f.stat().st_size) for f in out.iterdir())
            if now != seen:
                seen, keyed = now, time.time()
            elif requester and time.time() - keyed > 15:
                keyed = time.time()
                subprocess.run(
                    ["xdotool", "key", "super+v"], env=self.x11(d), check=False
                )
                print(
                    f"{name}: no output for a while, answered a requester", flush=True
                )
        took = time.time() - t0
        if not (out / "done").exists():
            self.screenshot(d, out / "screen.png")
        self.stop(p, cfg, signal.SIGKILL if kill else signal.SIGTERM)
        if not (out / "done").exists():
            log = text(out / "log.txt").split()
            where = (
                "after its Startup-Sequence started"
                if log
                else "before its Startup-Sequence"
            )
            raise (RuntimeError if log else BootStalled)(
                f"boot {name} did not finish in {took:.0f} s, {where}, screen in {out / 'screen.png'}"
            )
        return out

    @staticmethod
    def x11(d):
        """Environment for an X client on the display FS-UAE of boot d runs on."""
        auth = subprocess.run(
            ["xauth", "-f", str(d / "xauth"), "list"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
        display = auth.split()[0].split("/unix")[-1] if auth else ""
        return dict(os.environ, DISPLAY=display, XAUTHORITY=str(d / "xauth"))

    @staticmethod
    def screenshot(d, png):
        """The screen of a boot that did not finish."""
        from PIL import ImageGrab

        env = Machine.x11(d)
        os.environ["XAUTHORITY"] = env["XAUTHORITY"]
        try:
            ImageGrab.grab(xdisplay=env["DISPLAY"]).save(png)
        except OSError as e:
            print(f"{d.name}: no screenshot, {e}", flush=True)

    @staticmethod
    def stop(p, cfg, sig):
        pat = f"^fs-uae {re.escape(str(cfg))}$"
        for pid in subprocess.run(
            ["pgrep", "-f", pat], capture_output=True, text=True, check=False
        ).stdout.split():
            os.kill(int(pid), sig)
        t0 = time.time()
        while (
            subprocess.run(
                ["pgrep", "-f", pat], capture_output=True, check=False
            ).returncode
            == 0
            and time.time() - t0 < 30
        ):
            time.sleep(0.5)
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        p.wait()


def cores():
    """Physical cores this process may use: an emulator keeps more than one
    hardware thread busy. Capped by a cgroup CPU quota."""
    allowed = os.sched_getaffinity(0)
    seen, cpu, phys = set(), None, "0"
    for ln in Path("/proc/cpuinfo").read_text().splitlines():
        key, _, val = (x.strip() for x in ln.partition(":"))
        if key == "processor":
            cpu, phys = int(val), "0"
        elif key == "physical id":
            phys = val
        elif key == "core id" and cpu in allowed:
            seen.add((phys, val))
    n = len(seen) or len(allowed)
    try:
        quota, period = Path("/sys/fs/cgroup/cpu.max").read_text().split()
        if quota != "max":
            n = min(n, -(-int(quota) // int(period)))
    except (OSError, ValueError):
        pass
    return max(1, n)


def text(path):
    return path.read_text(errors="replace").replace("\r", "\n") if path.exists() else ""


def lines(path, prefix):
    return [ln for ln in text(path).splitlines() if ln.startswith(prefix)]


def run(m, sc, base, work):
    """Runs one scenario; returns what was damaged and what went wrong."""
    hdf = work / (sc["name"] + ".hdf")
    shutil.copyfile(base, hdf)
    if sc.get("powercut"):
        m.boot(
            sc["name"] + "-copy",
            hdf,
            "Copy C: PFS0:c.bak ALL QUIET\nWait 5\n",
            kill=True,
        )
        what = "killed after a copy and its update"
    else:
        what = sc["damage"](hdf) if sc.get("damage") else "nothing"
    what, lost = what if isinstance(what, tuple) else (what, set())
    out = m.boot(sc["name"], hdf, DOCTOR + LIST, requester=sc.get("requester", False))
    check1, repair = (
        lines(out / "check1.txt", "ERROR:"),
        lines(out / "repair.txt", "FIXED:"),
    )
    errs = [
        f"pfsdoctor did not run for {f}"
        for f in ("check1.txt", "repair.txt", "check2.txt")
        if "Checking rootblock" not in text(out / f)
    ]
    errs += [
        f"{f}: {ln}"
        for f in ("check1.txt", "repair.txt", "check2.txt")
        for ln in lines(out / f, "DOCTOR ERROR")
    ]
    if "error" in sc:
        if not any(sc["error"] in ln for ln in check1):
            errs.append("CHECK did not report '{}'".format(sc["error"]))
        if not any(sc["fixed"] in ln for ln in repair):
            errs.append("REPAIR did not report '{}'".format(sc["fixed"]))
    else:
        errs += ["CHECK: " + ln for ln in check1] + ["REPAIR: " + ln for ln in repair]
    errs += ["second CHECK: " + ln for ln in lines(out / "check2.txt", "ERROR:")]
    have = set(text(out / "list.txt").splitlines())
    if not have:
        errs.append("no files listed")
    gone = sorted(
        f
        for f in set(text(work / "base/out/list.txt").splitlines()) - have
        if f.rsplit(" ", 1)[0] not in lost
    )
    if gone:
        errs.append(f"{len(gone)} files lost, e.g. {gone[0]}")
    kept = {f.rsplit(" ", 1)[0] for f in have} & lost
    if kept:
        errs.append(
            f"{len(kept)} files of the damaged block still listed, e.g. {min(kept)}"
        )
    errs += deldir_slots(hdf)
    if "same" in sc and sc["same"](hdf) != sc["same"](base):
        errs.append(
            "{} is {} after REPAIR, {} before the damage".format(
                sc["same"].__name__, sc["same"](hdf), sc["same"](base)
            )
        )
    return what, errs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--handler", required=True)
    ap.add_argument("--doctor", required=True)
    ap.add_argument(
        "--largefile",
        metavar="SETLARGEFILE",
        help="4096 byte blocks and large file mode, set with this setlargefile",
    )
    ap.add_argument(
        "--work", help="default build/tests/pfsdoctor/<system>-<normal or largefile>"
    )
    ap.add_argument(
        "--scenario", action="append", help="run only this scenario, repeatable"
    )
    ap.add_argument(
        "--reuse",
        action="store_true",
        help="keep the volume of an earlier run with the same handler",
    )
    ap.add_argument(
        "--limit", type=int, default=600, help="seconds per boot (default 600)"
    )
    ap.add_argument(
        "--jobs",
        type=int,
        default=cores(),
        help="scenarios run at the same time (default: physical cores)",
    )
    a = ap.parse_args()

    for var in ("AMIGA_SYSTEM", "AMIGA_KICKSTART"):
        if not os.environ.get(var):
            sys.exit(f"{var} is not set, run in an amiga-fsuae OS image")
    scenarios = [sc for sc in SCENARIOS if a.largefile or not sc.get("largefile")]
    names = [sc["name"] for sc in scenarios]
    for n in a.scenario or []:
        if n not in names:
            sys.exit("unknown scenario {}, known: {}".format(n, " ".join(names)))
    variant = "largefile" if a.largefile else "normal"
    work = Path(
        a.work
        or "build/tests/pfsdoctor/{}-{}".format(
            Path(os.environ["AMIGA_SYSTEM"]).name.lstrip(".") or "system", variant
        )
    ).resolve()
    work.mkdir(parents=True, exist_ok=True)
    tools = [Path(a.doctor).resolve()] + (
        [Path(a.largefile).resolve()] if a.largefile else []
    )
    m = Machine(work, tools, a.limit)

    base, key = work / "base.hdf", work / "base.key"
    # the handler lives in the RDB of the volume
    made = f"{hashlib.sha256(Path(a.handler).read_bytes()).hexdigest()} {variant}"
    if not (a.reuse and base.exists() and key.exists() and key.read_text() == made):
        base.unlink(missing_ok=True)
        key.unlink(missing_ok=True)
        subprocess.run(
            [
                "rdbtool",
                "-f",
                str(base),
                "create",
                "size=32Mi",
                "+",
                "init",
                "rdb_cyls=10",
                "+",
                "add",
                "name=PFS0",
                "size=95%",
                "dostype=" + DOSTYPE,
                f"bs={4096 if a.largefile else 512}",
                "+",
                "fsadd",
                str(Path(a.handler).resolve()),
                "dostype=" + DOSTYPE,
            ],
            check=True,
            capture_output=True,
        )
        # /d spans several dirblocks, which are larger with larger blocks
        many = "\n".join(
            f"Copy C:List TO PFS0:d/file-with-a-long-name-{i}"
            for i in range(160 if a.largefile else 40)
        )
        largefile = (
            f"OUT:{Path(a.largefile).name} PFS0: ENABLE >OUT:setlargefile.txt\n"
            if a.largefile
            else ""
        )
        out = m.boot("base", base, FILL.format(many=many, largefile=largefile) + LIST)
        bad = lines(out / "check.txt", "ERROR:")
        if bad or not text(out / "list.txt"):
            sys.exit("the filled volume is not clean:\n" + "\n".join(bad))
        key.write_text(made)
    img = Image(base)
    disktype = u32(img.sectors(ROOTBLOCK, 1), 0)
    img.close()
    large = disktype == ID_PFS2_DISK and bool(img.options & MODE_LARGEFILE)
    if large != bool(a.largefile):
        sys.exit(
            f"the volume is PFS\\{disktype & 0xFF}, options 0x{img.options:x}, not a {variant} volume"
        )

    failed = 0
    rows = [
        "{}, {} volume".format(Path(os.environ["AMIGA_SYSTEM"]).name, variant),
        "",
        "| scenario | damage | result |",
        "|---|---|---|",
    ]
    todo = [sc for sc in scenarios if not a.scenario or sc["name"] in a.scenario]
    machines = queue.Queue()
    machines.put(m)
    for slot in range(1, max(1, min(a.jobs, len(todo)))):
        machines.put(Machine(work, tools, a.limit, slot))

    def one(sc):
        mm = machines.get()
        try:
            return run(mm, sc, base, work)
        except Exception as e:  # noqa: BLE001
            return "", [f"{type(e).__name__}: {e}"]
        finally:
            machines.put(mm)

    with ThreadPoolExecutor(machines.qsize()) as pool:
        results = [pool.submit(one, sc) for sc in todo]
        for sc, res in zip(todo, results, strict=True):
            what, errs = res.result()
            failed += bool(errs)
            status = "FAIL" if errs else "ok  "
            print(f"{status} {sc['name']}: {what}", flush=True)
            for e in errs:
                print("     " + e, flush=True)
            rows.append(f"| {sc['name']} | {what} | {'<br>'.join(errs) or 'ok'} |")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write("\n".join(rows) + "\n\n")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
