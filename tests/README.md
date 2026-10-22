# pfs3aio test rig

Two layers of tests for the pfs3aio driver.

## `live/` — proofs that the fixes changed behaviour (fail on broken, pass on fixed)

Each `live/*_live.c` lifts a **real function out of this checkout's driver source**,
compiles it with a small mock, and asserts *correct* behaviour — nothing about
"old vs new". So on a tree that still has the bug the test FAILS, and once the
corresponding fix is applied it PASSES. That is what makes it a proof the fix did
something, rather than a demonstration that always passes.

```sh
zsh tests/live/run.sh                        # test this checkout's source (../..)
SRCROOT=/path/to/other/tree zsh tests/live/run.sh
```

It needs only a host C compiler. On unpatched upstream master all of these fail;
apply the fixes and they pass. Covered:

| test | fix it proves |
|------|---------------|
| `blocksize_live.c`      | logical block size normalized to a power of two (blockshift/blocklogshift agree) |
| `partlimit_live.c`      | partition end saturates instead of wrapping past 2^32 sectors (no inverted partition) |
| `ds_retry_live.c`       | DirectSCSI retry restarts in range instead of re-applying blocklogshift and writing outside the partition |
| `ds_maxtransfer_live.c` | DirectSCSI maxtransfer in native sectors (a 16 KiB MaxTransfer with 4 KB blocks no longer rounds to 0) |
| `freeanode_live.c`      | FreeAnode marks the correct anodeblock in non-split mode |
| `update_cleanup_live.c` | Deferred reserved frees are persisted by a root-only write after the primary commit |
| `writeback_live.c` | Relative writeback deadline survives one-second reads and continuous packet traffic; idle flushing, retries and inhibit remain safe |

Not covered by a live test: fixes that are one-liners inside large functions
(e.g. the `numblocks` formula in `NewVolume`) or that are inseparable from the
AmigaOS runtime (the remaining update/commit, cache, directory and geometry
paths). Those are covered by the black-box layer below and by code review in
the fix PR.

## black-box AmiFUSE suite — functional / regression / atomicity

Runs the real m68k driver against disk images through
[AmiFUSE](https://github.com/reinauer/AmiFUSE) (amitools/vamos emulation). This
layer passes on both the old and fixed driver — it proves the driver mounts,
reads, writes, formats, round-trips data byte-exactly, survives injected I/O
errors and power cuts, and covers supermode and the >4 GB TD64 path. It is a
strong *no-regression* check, not a proof of any individual fix.

```sh
cp /path/to/pfs.hdf tests/fixtures/small.hdf   # a 512-byte-block PFS3 image
zsh tests/make-supermode.sh --driver /path/to/pfs3aio   # sparse supermode fixture
zsh tests/run.sh --driver /path/to/pfs3aio
```

Requires `amifuse` (+ `rdbtool`) on PATH and a built driver. `fi.py` is the
fault-injection / crash harness the suite drives; see its docstring for the
`FI_*` knobs. `timer_device.py` gives the vamos timer stub deferred completion
using the host's monotonic clock, instead of expiring requests immediately.

## timer writeback regression

```sh
python3 tests/writeback.py --image /path/to/pfs.hdf --driver ./pfs3aio
```

This uses the real compiled m68k handler with a deferred timer.device shim.
It creates a small file, opens/reads/closes `foo.md` once per simulated second,
and snapshots the image after six seconds while the writer is still mounted.
Reopening that snapshot must recover the complete new file without relying on
an explicit flush or the writer's shutdown. The same test fails on the driver
without the independent writeback deadline. Use `--read-file` for fixtures
with a different existing file. All image changes are confined to temporary
copies. The native `live/` test additionally exercises continuous read/write
traffic, failed commits, soft protection, inhibit and simultaneous replies.
Pass `--continuous-writes` to keep a larger file open and write chunks across
deadline expirations, or `--real-time` to use elapsed host time instead of the
virtual clock.

## everything

```sh
zsh tests/run-all.sh --driver /path/to/pfs3aio
```
