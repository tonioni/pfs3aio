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

The test binaries and extracted functions are written to `build/tests/live` (see
[Generated files](#generated-files)), never into `tests/`.

It needs only a host C compiler. It prints one line per assertion plus a
`live: N passed, M failed` summary and exits non-zero if anything failed. `run.sh`
finds the tests with `live/*_live.c`, so a new test needs no registration; the
`@src`/`@func` tags in its header name the functions it lifts (matched at column 0
of the source, as a definition). Each test is written to FAIL on a tree that still
has the bug it covers and to PASS once it is fixed. Covered:

| test | fix it proves |
|------|---------------|
| `reserved_bitmap_live.c` | Reject inconsistent reserved ranges and undersized root bitmaps before allocation; preserve exact-capacity and partial-word bitmaps |
| `blocksize_live.c`      | logical block size normalized to a power of two (blockshift/blocklogshift agree) |
| `partlimit_live.c`      | partition end saturates instead of wrapping past 2^32 sectors (no inverted partition) |
| `ds_retry_live.c`       | DirectSCSI retry restarts in range instead of re-applying blocklogshift and writing outside the partition |
| `ds_maxtransfer_live.c` | DirectSCSI maxtransfer in native sectors (a 16 KiB MaxTransfer with 4 KB blocks no longer rounds to 0) |
| `freeanode_live.c`      | FreeAnode marks the correct anodeblock in non-split mode |
| `update_cleanup_live.c` | Deferred reserved frees are persisted by a root-only write after the primary commit |
| `writeback_live.c` | Relative writeback deadline survives one-second reads and continuous packet traffic; idle flushing, retries and inhibit remain safe |
| `directory_bounds_live.c` | Reject oversized directory entries and out-of-entry names/comments before caching; safely rewrite valid entries with maximum-length strings |

Not covered by a live test: fixes that are one-liners inside large functions
(e.g. the `numblocks` formula in `NewVolume`) or that are inseparable from the
AmigaOS runtime (the remaining update/commit, cache, directory and geometry
paths). Those are covered by the black-box layer below and by code review in
the fix PR.

## black-box AmiFUSE suite — functional / regression / atomicity

Runs the real m68k driver against disk images through
[AmiFUSE](https://github.com/reinauer/AmiFUSE) (amitools/vamos emulation). It
checks that the driver mounts, reads, writes, formats, round-trips data
byte-exactly, and leaves a consistent volume after injected I/O errors, power
cuts and a killed emulator, on a small image and on a supermode image (>4 GB,
which takes the TD64/NSD path). It is a *no-regression* check, not a proof of
any individual fix (the `live/` tests are).

```sh
cp /path/to/pfs.hdf tests/fixtures/small.hdf   # a 512-byte-block PFS3 image with a few MB free
zsh tests/make-supermode.sh [--driver /path/to/pfs3aio]   # sparse supermode fixture
zsh tests/run.sh [--driver /path/to/pfs3aio] [--quick] [--stock /path/to/other/driver]
```

Without `--driver` the scripts use `pfs3aio` in the repository root, the output of `make`.

Needs zsh, python3 with `amifuse` importable (activate its venv), `cmp` and `awk`
(`rdbtool` and `amifuse` on PATH for `make-supermode.sh`). Images are copied
sparse-preserving (`cp -c` on macOS, `cp --reflink=auto --sparse=always`
elsewhere: GNU `cp -c` is not clonefile). `--quick` uses fewer fault/crash points
and skips T5.

What counts as a pass: a test only passes if the operation under test really
happened, and the verdict comes from the volume, not from "it mounted".
After a fault or crash the volume must remount (`verify` reports a responsive
filesystem) with the same directories and either exactly the original files and
byte total (rolled back) or exactly one more file whose bytes read back identical
to what was written (committed). Anything in between fails. The fault and crash
tests first do a fault-free control write on a fresh copy to learn how many
reads/writes it takes, place their faults relative to that, and FAIL if the fault
or power cut never fired (`FI-SUMMARY faults=`, `FI-CRASH` in fi.py's output). Each
image gets its own scratch directory and its own file names, so a failed step
cannot be satisfied by leftovers of another one.

- **T1** write an odd-sized and a multi-block file, read both back byte-exactly,
  and check the volume totals.
- **T2** format, then round-trip a file (on a copy; reformats the partition).
- **T3** read/write errors (cancel and retry, single and persistent) injected
  at different points of a write.
- **T4** power cut just before the Nth write of the write sequence (W writes;
  a full run samples about 40 points including the last three, `--quick` four),
  plus a cut point past the end that must commit.
- **T5** `kill -9` of the emulator once the driver has issued 25/50/75/100% of
  the control run's writes (counted through `FI_PROGRESS`, no sleep timing).
  The final commit is a burst of about ten writes that takes under a millisecond,
  so an external kill cannot land inside it: the 25-75% kills roll back and the
  100% kill finds it committed. Cuts inside the commit burst are T4's job.

`fi.py` is the fault-injection / crash harness the suite drives; see its docstring
for the `FI_*` knobs (`FI_SKIP` and `FI_PROGRESS` are what let faults and kills
land mid-operation). It hooks the classic, 64-bit TD and NSD (`NSCMD_TD_*64`)
transfer commands; supermode images use the NSD ones. `timer_device.py` gives the
vamos timer stub deferred completion using the host's monotonic clock, for
AmiFUSE releases that do not ship their own `amifuse.timer_device`.

## timer writeback regression

```sh
python3 tests/writeback.py --image /path/to/pfs.hdf --driver pfs3aio
```

This uses the real compiled m68k handler with a deferred timer.device: AmiFUSE's
own `AmiFuseTimerDevice` driven by a virtual clock, or the `timer_device.py` shim on
AmiFUSE releases without it. The run fails unless the handler's timer requests were
really intercepted and replied to, so it cannot pass without exercising the deadline.
It creates a small file, opens/reads/closes `foo.md` once per simulated second,
and snapshots the image after six seconds while the writer is still mounted.
Reopening that snapshot must recover the complete new file without relying on
an explicit flush or the writer's shutdown. The same test fails on the driver
without the independent writeback deadline. The image must contain the file read
each second: `foo.md` by default, use `--read-file NAME` for fixtures with a
different existing file (e.g. `--read-file A`). All image changes are confined to
temporary copies. The native `live/` test additionally exercises continuous
read/write traffic, failed commits, soft protection, inhibit and simultaneous
replies. Pass `--continuous-writes` to keep a larger file open and write chunks
across deadline expirations, or `--real-time` to use elapsed host time instead of
the virtual clock. `run-all.sh` does not run this test.

## everything

```sh
zsh tests/run-all.sh [--driver /path/to/pfs3aio] [--quick] [--strict]
```

Runs the `live/` tests, then, if `amifuse` and a built driver are available, builds
the supermode fixture (when missing) and runs the black-box suite. The last line
says whether the black-box layer ran or was SKIPPED; with `--strict` a skipped
black-box layer is a failure, so a green result cannot mean "only the live
tests ran".

## Generated files

`make` puts the objects in `build/normal` (or `build/largefile`) and the driver
in the repository root. These scripts keep their own files, the live-test
binaries (`build/tests/live`) and the black-box scratch space, in `build/tests`.
Choose another location with `TEST_BUILD_DIR`:

```sh
TEST_BUILD_DIR=/tmp/pfs3-tests zsh tests/run-all.sh
```

A non-empty `TEST_BUILD_DIR` that these scripts did not create (no `.build-dir`
marker) is refused instead of being written into. The disk-image fixtures stay in
`tests/fixtures`, because they are slow to recreate. `make clean` does not remove
`build/tests` or the fixtures; delete them by hand.
