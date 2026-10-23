#!/usr/bin/env python3
"""Verify timer writeback with the real m68k handler and repeated file reads.

    python3 tests/writeback.py --image pfs.hdf --driver ./pfs3aio

Uses disposable images. A snapshot is taken while the writer is still mounted,
without ACTION_FLUSH or unmount, then reopened to verify the committed data.
The local vamos timer stub is given deferred completion and a virtual relative
clock so a five-second deadline takes five simulated seconds, not five packets.
"""

import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import shutil
import tempfile
import time

from amifuse.fuse_fs import HandlerBridge
from timer_device import DeferredTimers


def read_file(bridge, name):
    handle = bridge.open_file(name, os.O_RDONLY)
    if handle is None:
        return None
    address, _ = handle
    try:
        data = bytearray()
        while True:
            block = bridge.read_handle(address, 4096)
            if not block:
                return bytes(data)
            data.extend(block)
    finally:
        bridge.close_file(address)


def run(args):
    payload = (bytes(range(256)) * (16 * 80) if args.continuous_writes else
               bytes(range(256)) * 3 + b"writeback")
    with tempfile.TemporaryDirectory(prefix="pfs-writeback-") as work:
        image = Path(work) / "writer.hdf"
        before = Path(work) / "before.hdf"
        after = Path(work) / "after.hdf"
        shutil.copyfile(args.image, image)

        timers = DeferredTimers()

        def tick(bridge):
            if args.real_time:
                time.sleep(0.1)
            else:
                timers.advance(bridge, 100_000)
            bridge._flush_pending_signals()

        with ExitStack() as patches:
            timers.install(patches, real_time=args.real_time)
            bridge = HandlerBridge(image, args.driver, read_only=False)
            try:
                # Commit startup housekeeping before the workload begins.
                bridge.flush_volume()
                assert read_file(bridge, "/" + args.read_file) is not None

                handle = bridge.open_file(
                    "/WRITEBACK-TEST", os.O_WRONLY | os.O_CREAT | os.O_TRUNC
                )
                assert handle is not None
                address, _ = handle
                try:
                    chunk_size = 4096 if args.continuous_writes else len(payload)
                    for offset in range(0, len(payload), chunk_size):
                        chunk = payload[offset:offset + chunk_size]
                        assert bridge.write_handle(address, chunk) == len(chunk)
                        if offset == 0:
                            bridge.backend.sync()
                            shutil.copyfile(image, before)
                        if args.continuous_writes:
                            tick(bridge)
                            assert read_file(bridge, "/" + args.read_file) is not None
                        assert not bridge.state.crashed
                finally:
                    bridge.close_file(address)

                for step in range(1, 61):
                    tick(bridge)
                    if step % 10 == 0:
                        assert read_file(bridge, "/" + args.read_file) is not None
                    assert not bridge.state.crashed

                bridge.backend.sync()
                shutil.copyfile(image, after)
                print(
                    f"timers: {timers.submitted} submitted, "
                    f"{timers.completed} completed, "
                    f"{len(timers.pending)} pending",
                    flush=True,
                )
            finally:
                bridge.close()

        # The snapshots cannot be changed by the writer's shutdown flush.
        for snapshot, expected in ((before, None), (after, payload)):
            reader = HandlerBridge(snapshot, args.driver, read_only=True)
            try:
                actual = read_file(reader, "/WRITEBACK-TEST")
                assert actual == expected, (
                    f"{snapshot.name}: expected "
                    f"{'no file' if expected is None else str(len(expected)) + ' bytes'}, "
                    f"got {'no file' if actual is None else str(len(actual)) + ' bytes'}"
                )
            finally:
                reader.close()
    print("PASS: pending file committed within six seconds of repeated reads")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--read-file", default="foo.md")
    parser.add_argument("--continuous-writes", action="store_true",
                        help="also write chunks across timer expirations")
    parser.add_argument("--real-time", action="store_true",
                        help="use elapsed host time instead of virtual time")
    run(parser.parse_args())
