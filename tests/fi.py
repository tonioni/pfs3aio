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
  FI_COUNT  int          number of matching transfers to fail (0 = all). default 0
  FI_ERR    int          io_Error value to report on a faulted transfer. default 30
  FI_REQ    0|1          requester answer: 1=Retry, 0=Cancel. default 1
  FI_REQCAP int          force Cancel after this many requesters (loop guard). default 40
  FI_CRASHN int          hard-exit (simulated power cut) just before the Nth write. 0=off
  FI_FLUSH  0|1          flush every write straight to the image file, so an external
                         SIGKILL (kill -9) also leaves a faithful partial-write image. default off

Emits one machine-readable line to stderr on exit:
  FI-SUMMARY faults=<n> matched=<n> requesters=<n> writes=<n> rc=<n>
and, on a simulated crash / uncaught exception:
  FI-CRASH after write #<n>     /     FI-EXCEPTION: <type>: <msg>
"""
import os
import sys
from contextlib import ExitStack

from timer_device import DeferredTimers

# Immediate timer replies are not valid hardware behaviour: servicing them
# between packets would expire a five-second deadline on the next packet.
# Pump deferred replies at GetMsg boundaries using a monotonic host clock.
_timer_patches = ExitStack()
_timers = DeferredTimers()
_timers.install(_timer_patches, real_time=True)

FI_CMD    = os.environ.get("FI_CMD", "none")
FI_LO     = int(os.environ.get("FI_LO", "0"))
FI_HI     = int(os.environ.get("FI_HI", str(2**31)))
FI_COUNT  = int(os.environ.get("FI_COUNT", "0"))
FI_ERR    = int(os.environ.get("FI_ERR", "30"))
FI_REQ    = int(os.environ.get("FI_REQ", "1"))
FI_REQCAP = int(os.environ.get("FI_REQCAP", "40"))
FI_CRASHN = int(os.environ.get("FI_CRASHN", "0"))
FI_FLUSH  = os.environ.get("FI_FLUSH", "0") == "1"

import amifuse.scsi_device as sd
import amifuse.driver_runtime as drt
from amitools.vamos.lib import IntuitionLibrary as IL

CMD_READ, CMD_WRITE = 2, 3
TD_READ64, TD_WRITE64 = 24, 25

stats = {"faults": 0, "matched": 0, "reqs": 0, "writes": 0}

_orig_beginio = sd.ScsiDevice.BeginIO


def _patched_beginio(self, ctx, io_request):
    mem = ctx.mem
    ior = sd.IORequestStruct(mem, io_request)
    cmd = ior.command.val
    is_write = cmd in (CMD_WRITE, TD_WRITE64)
    is_read = cmd in (CMD_READ, TD_READ64)

    if FI_CRASHN and is_write:
        stats["writes"] += 1
        if stats["writes"] >= FI_CRASHN:
            sys.stderr.write("FI-CRASH after write #%d\n" % stats["writes"])
            sys.stderr.flush()
            os._exit(70)  # simulated power cut: no unwind, no further writes

    want = (is_write and "w" in FI_CMD) or (is_read and "r" in FI_CMD)
    if want and self.backend.block_size:
        offset = ior.offset.val
        if cmd in (TD_READ64, TD_WRITE64):
            offset |= (ior.actual.val << 32)
        blk = offset // self.backend.block_size
        nblk = max(1, ior.length.val // self.backend.block_size)
        if blk <= FI_HI and (blk + nblk - 1) >= FI_LO:  # ranges intersect
            stats["matched"] += 1
            if FI_COUNT == 0 or stats["faults"] < FI_COUNT:
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
    rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
except BaseException as e:  # emulator/driver blew up: report, don't traceback-spam
    sys.stderr.write("FI-EXCEPTION: %s: %s\n" % (type(e).__name__, e))
    rc = 99

sys.stderr.write("FI-SUMMARY faults=%d matched=%d requesters=%d writes=%d rc=%s\n"
                 % (stats["faults"], stats["matched"], stats["reqs"], stats["writes"], rc))
sys.exit(rc if isinstance(rc, int) else 0)
