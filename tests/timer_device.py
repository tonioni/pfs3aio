"""Deferred classic timer.device completions for handler regression tests."""

import time
from unittest.mock import patch

from amifuse.fuse_fs import HandlerBridge
from amitools.vamos.lib.ExecLibrary import ExecLibrary
from amitools.vamos.lib.TimerDevice import TimerDevice
from amitools.vamos.libstructs import IORequestStruct


class DeferredTimers:
    def __init__(self):
        self.now = 0
        self.pending = {}
        self.releasing = False
        self.real_time = False
        self.submitted = 0
        self.completed = 0
        self.original_begin = TimerDevice.BeginIO
        self.original_send = ExecLibrary.SendIO
        self.original_get = ExecLibrary.GetMsg

    def begin(self, device, ctx, address):
        request = IORequestStruct(ctx.mem, address)
        if request.command.val != 9 or self.releasing:
            return self.original_begin(device, ctx, address)
        assert address not in self.pending, "timer IORequest submitted twice"
        # Classic timer.device uses the 32-byte IORequest header, followed
        # by timeval. Some vamos versions incorrectly use IOStdReq (48
        # bytes) in TimeRequestStruct, so decode the actual NDK ABI here.
        delay = ctx.mem.r32(address + 32) * 1_000_000 + ctx.mem.r32(address + 36)
        assert delay > 0
        if self.real_time:
            self.now = time.monotonic_ns() // 1000
        self.pending[address] = (self.now + delay, ctx)
        self.submitted += 1
        request.flags.val &= ~1
        request.error.val = 0
        return 0

    def abort(self, device, ctx, address):
        self.pending.pop(address, None)
        request = IORequestStruct(ctx.mem, address)
        request.flags.val |= 1
        request.error.val = -2
        return 0

    def complete(self, exec_impl):
        due = sorted(
            (deadline, address, ctx)
            for address, (deadline, ctx) in self.pending.items()
            if deadline <= self.now
        )
        for _, address, ctx in due:
            del self.pending[address]
            self.completed += 1
            self.releasing = True
            try:
                self.original_send(exec_impl, ctx, address)
            finally:
                self.releasing = False

    def advance(self, bridge, micros):
        assert not self.real_time
        self.now += micros
        self.complete(bridge.vh.slm.exec_impl)

    def install(self, stack, *, real_time=False):
        self.real_time = real_time
        timers = self

        def begin(self, ctx, address):
            return timers.begin(self, ctx, address)

        def abort(self, ctx, address):
            return timers.abort(self, ctx, address)

        def get_message(self, ctx, port):
            if timers.real_time:
                timers.now = time.monotonic_ns() // 1000
                timers.complete(self)
            return timers.original_get(self, ctx, port)

        # The vamos library scanner uses these annotations to marshal ports
        # and message pointers when entering and leaving the Exec trap.
        get_message.__annotations__ = self.original_get.__annotations__.copy()
        stack.enter_context(patch.object(TimerDevice, "BeginIO", begin))
        stack.enter_context(patch.object(TimerDevice, "AbortIO", abort, create=True))
        stack.enter_context(patch.object(ExecLibrary, "GetMsg", get_message))
        # AmiFUSE normally discards the timer stub's immediate replies. Retain
        # deferred replies so the driver can receive real timer events.
        stack.enter_context(
            patch.object(HandlerBridge, "_drain_non_essential_ports", lambda *_: None)
        )
