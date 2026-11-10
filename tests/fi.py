#!/usr/bin/env python3
"""Fault-injection / crash harness for testing pfs3aio under AmiFUSE.

It monkeypatches the emulated scsi.device (block I/O) and intuition.library
(the driver's error requester), then runs a normal `amifuse` sub-command.
Everything after the recognised env vars is passed straight to amifuse, e.g.

    FI_CMD=w FI_COUNT=1 FI_REQ=1 python3 fi.py write img.hdf --file X --in f --driver D

Config (all via environment):
  FI_CMD    r|w|rw|none  which transfers to fault (read/write/both/neither). default none
  FI_LO     int          lowest block to fault (inclusive). default 0
  FI_HI     int          highest block to fault (inclusive). default 2**31
  FI_SKIP   int          let this many matching transfers through before the first fault, so a
                         fault can land mid-operation instead of on the first access. default 0
  FI_COUNT  int          number of matching transfers to fail (0 = all). default 0
  FI_ERR    int          io_Error value to report on a faulted transfer. default 30
  FI_REQ    0|1          requester answer: 1=Retry, 0=Cancel. default 1
  FI_REQCAP int          force Cancel after this many requesters (loop guard). default 40
  FI_CRASHN int          hard-exit (simulated power cut) just before the Nth write. 0=off
  FI_FLUSH  0|1          flush every write straight to the image file, so an external
                         SIGKILL (kill -9) also leaves a faithful partial-write image. default off
  FI_PROGRESS path       append one byte to this file for every write the driver issues, so a
                         supervisor can kill -9 the process at a chosen write instead of
                         guessing a wall-clock delay

Emits one machine-readable line to stderr on exit:
  FI-SUMMARY faults=<n> matched=<n> requesters=<n> writes=<n> reads=<n> rc=<n>
(faults = transfers actually failed, matched = transfers that fell in the
FI_LO..FI_HI range, writes/reads = transfers the driver issued). A test that
injects a fault should require faults >= 1: a spec that never matches is a pass
that proved nothing.
and, on a simulated crash / uncaught exception:
  FI-CRASH after write #<n>     /     FI-EXCEPTION: <type>: <msg>
An amifuse error message (SystemExit with a string) is echoed to stderr.
"""
import os
import sys
from contextlib import ExitStack

from timer_device import DeferredTimers

# Immediate timer replies are not valid hardware behaviour: servicing them
# between packets would expire a five-second deadline on the next packet.
# Pump deferred replies at GetMsg boundaries using a monotonic host clock.
# AmiFUSE releases that ship their own deferred timer.device (amifuse.timer_device)
# already do this, and the shim would patch methods that device overrides.
try:
    import amifuse.timer_device  # noqa: F401
except ImportError:
    _timer_patches = ExitStack()
    _timers = DeferredTimers()
    _timers.install(_timer_patches, real_time=True)

FI_CMD    = os.environ.get("FI_CMD", "none")
FI_LO     = int(os.environ.get("FI_LO", "0"))
FI_HI     = int(os.environ.get("FI_HI", str(2**31)))
FI_SKIP   = int(os.environ.get("FI_SKIP", "0"))
FI_COUNT  = int(os.environ.get("FI_COUNT", "0"))
FI_ERR    = int(os.environ.get("FI_ERR", "30"))
FI_REQ    = int(os.environ.get("FI_REQ", "1"))
FI_REQCAP = int(os.environ.get("FI_REQCAP", "40"))
FI_CRASHN = int(os.environ.get("FI_CRASHN", "0"))
FI_FLUSH  = os.environ.get("FI_FLUSH", "0") == "1"
FI_PROGRESS = os.environ.get("FI_PROGRESS", "")
_progress_fd = (os.open(FI_PROGRESS, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_APPEND, 0o644)
                if FI_PROGRESS else None)

import amifuse.scsi_device as sd
import amifuse.driver_runtime as drt
from amitools.vamos.lib import IntuitionLibrary as IL

CMD_READ, CMD_WRITE = 2, 3
TD_READ64, TD_WRITE64 = 24, 25
# Disks over 4 GB (supermode fixtures) are accessed with the NSD commands instead
NSCMD_TD_READ64, NSCMD_TD_WRITE64 = 0xC000, 0xC001
READS64 = (TD_READ64, NSCMD_TD_READ64)
WRITES64 = (TD_WRITE64, NSCMD_TD_WRITE64)

stats = {"faults": 0, "matched": 0, "reqs": 0, "writes": 0, "reads": 0}

_orig_beginio = sd.ScsiDevice.BeginIO


def _patched_beginio(self, ctx, io_request):
    mem = ctx.mem
    ior = sd.IORequestStruct(mem, io_request)
    cmd = ior.command.val
    is_write = cmd == CMD_WRITE or cmd in WRITES64
    is_read = cmd == CMD_READ or cmd in READS64

    if is_read:
        stats["reads"] += 1
    if is_write:
        stats["writes"] += 1
        if FI_CRASHN and stats["writes"] >= FI_CRASHN:
            sys.stderr.write("FI-CRASH after write #%d\n" % stats["writes"])
            sys.stderr.flush()
            os._exit(70)  # simulated power cut: no unwind, no further writes
        if _progress_fd is not None:
            os.write(_progress_fd, b".")

    want = (is_write and "w" in FI_CMD) or (is_read and "r" in FI_CMD)
    if want and self.backend.block_size:
        offset = ior.offset.val
        if cmd in READS64 or cmd in WRITES64:
            offset |= (ior.actual.val << 32)
        blk = offset // self.backend.block_size
        nblk = max(1, ior.length.val // self.backend.block_size)
        if blk <= FI_HI and (blk + nblk - 1) >= FI_LO:  # ranges intersect
            stats["matched"] += 1
            if stats["matched"] > FI_SKIP and (FI_COUNT == 0 or stats["faults"] < FI_COUNT):
                stats["faults"] += 1
                ior.error.val = FI_ERR
                ior.actual.val = 0
                ior.flags.val |= sd.IOF_QUICK
                return
    return _orig_beginio(self, ctx, io_request)


def _patched_easyreq(self, ctx, window, easy_struct, idcmp_ptr, args):
    stats["reqs"] += 1
    if stats["reqs"] > FI_REQCAP:
        return 0  # force Cancel to break any retry loop
    return FI_REQ


sd.ScsiDevice.BeginIO = _patched_beginio
IL.IntuitionLibrary.EasyRequestArgs = _patched_easyreq

# Flush every write through to the image file so that a crash (in-process
# os._exit, or an external SIGKILL) leaves a faithful partial-write image -
# a real power cut - rather than a lost Python-level buffer. Always on in
# crash mode; opt-in via FI_FLUSH for the external-kill test.
if FI_CRASHN or FI_FLUSH:
    _orig_write = drt.BlockDeviceBackend.write_blocks

    def _flushing_write(self, blk_num, data, num_blks=1):
        r = _orig_write(self, blk_num, data, num_blks)
        try:
            self.blkdev.flush()
        except Exception:
            pass
        return r

    drt.BlockDeviceBackend.write_blocks = _flushing_write

import amifuse.fuse_fs as ff

rc = 0
try:
    ff.main(sys.argv[1:])
except SystemExit as e:
    if isinstance(e.code, str):
        sys.stderr.write("%s\n" % e.code)
    rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
except BaseException as e:  # emulator/driver blew up: report, don't traceback-spam
    sys.stderr.write("FI-EXCEPTION: %s: %s\n" % (type(e).__name__, e))
    rc = 99

sys.stderr.write("FI-SUMMARY faults=%d matched=%d requesters=%d writes=%d reads=%d rc=%s\n"
                 % (stats["faults"], stats["matched"], stats["reqs"], stats["writes"],
                    stats["reads"], rc))
sys.exit(rc if isinstance(rc, int) else 0)
