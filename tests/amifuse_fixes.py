"""Fixes for AmiFUSE 1.0.0 on amitools 0.9.1, applied on import.

timer.device: dos.library opens timer.device for dl_TimeReq while it starts,
before AmiFUSE registers its AmiFuseTimerDevice, so the handler gets the vamos
TimerDevice. The runtime makes its timer device for dos.library's Delay()
just before that, so it is registered as timer.device there.

Memory pools: vamos frees the puddles of a pool in Pool.__del__, which writes
the emulated memory. Collected after the runtime shut down, when that memory
is gone, it crashes Python at exit. The pools are freed while the runtime
still runs, in its shutdown, which HandlerBridge.close and the AmiFUSE
commands both call; the later __del__ finds nothing to free.
"""
from amifuse.vamos_runner import VamosHandlerRuntime


def _get_timer_device(self, _get=VamosHandlerRuntime._get_timer_device):
    device = _get(self)
    self.slm.lib_mgr.add_impl_cls("timer.device", self._get_timer_device)
    return device


def _shutdown(self, _shutdown=VamosHandlerRuntime.shutdown):
    try:
        exec_impl = getattr(getattr(self, "slm", None), "exec_impl", None)
        if exec_impl is not None:
            for pool in list(exec_impl._pools.values()):
                pool.__del__()
            exec_impl._pools.clear()
    finally:
        _shutdown(self)


if not getattr(VamosHandlerRuntime.shutdown, "amifuse_fixes", False):
    _get_timer_device.amifuse_fixes = _shutdown.amifuse_fixes = True
    VamosHandlerRuntime._get_timer_device = _get_timer_device
    VamosHandlerRuntime.shutdown = _shutdown
