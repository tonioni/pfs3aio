# pfs3aio test rig

Two layers of tests for the pfs3aio driver.

## `live/` — proofs that the fixes changed behaviour (fail on broken, pass on fixed)

Each `live/*_live.c` lifts a **real function out of this checkout's driver source**,
compiles it with a small mock, and asserts *correct* behaviour — nothing about
"old vs new". So on a tree that still has the bug the test FAILS, and once the
corresponding fix is applied it PASSES. That is what makes it a proof the fix did
something, rather than a demonstration that always passes.

```sh
bash tests/live/run.sh                       # test this checkout's source (../..)
SRCROOT=/path/to/other/tree bash tests/live/run.sh
```

The test binaries and extracted functions are written to `build/tests/live` (see
[Generated files](#generated-files)), never into `tests/`.

It needs only a host C compiler. It prints one line per assertion plus a
`live: N passed, M failed` summary and exits non-zero if anything failed. `run.sh`
finds the tests with `live/*_live.c`, so a new test needs no registration; the
`@src`/`@func` tags in its header name the functions it lifts (a later `@src` switches the file; matched at column 0
of the source, as a definition of exactly that name; if a name is defined more than
once, such as a `DEBUGMODE` stub, the last definition is used). Each test is written to FAIL on a tree that still
has the bug it covers and to PASS once it is fixed. Covered:

| test | fix it proves |
|------|---------------|
| `reserved_bitmap_live.c` | Reject inconsistent reserved ranges and undersized root bitmaps before allocation; preserve exact-capacity and partial-word bitmaps |
| `blocksize_live.c`      | logical block size normalized to a power of two (blockshift/blocklogshift agree) |
| `partlimit_live.c`      | partition end saturates instead of wrapping past 2^32 sectors (no inverted partition) |
| `ds_retry_live.c`       | DirectSCSI retry restarts in range instead of re-applying blocklogshift and writing outside the partition |
| `ds_maxtransfer_live.c` | DirectSCSI maxtransfer in native sectors (a 16 KiB MaxTransfer with 4 KB blocks no longer rounds to 0) |
| `freeanode_live.c`      | FreeAnode marks the correct anodeblock (non-split and split mode, reserved anodes), reports a failed SaveAnode as FALSE without marking, ignores out-of-range seqnrs |
| `update_cleanup_live.c` | Deferred reserved frees are persisted by a root-only write after the primary commit |
| `pending_retry_live.c` | A block whose relocation fails marks its volume; `RetryPendingBlocks` relocates it and clears the mark, keeps the mark when the retry fails, and reads no cache list while nothing is marked |
| `writeback_live.c` | Relative writeback deadline survives one-second reads and continuous packet traffic; idle flushing, retries and inhibit remain safe |
| `directory_bounds_live.c` | Reject oversized directory entries and out-of-entry names/comments before caching; safely rewrite valid entries with maximum-length strings |
| `dd_close_live.c` | `dd_Close` always removes the fileentry (failed volume check, flushed dirblock, ghost volume), so a ghost volume can die |
| `allocation_live.c` | `InitAllocation`: corrupt free-space counters cannot make the allocator wrap, and the bitmap block count stays within what `GetBitmapBlock` can address |
| `anodechain_live.c` | `MakeAnodeChain` fails on an anode chain whose `next` links loop instead of allocating until memory runs out; long valid chains still build |
| `blocksize_zero_live.c` | `CalculateBlockSize` with a zero (or wrapping) `de_SizeBlock` falls back to 512 bytes instead of spinning forever |
| `dir_integrity_live.c` | `IsChildOf` treats a `GetParent` failure or a cyclic parent chain as an error, and walks a valid chain of any depth |
| `lru_alloc_live.c` | `AllocLRU` never evicts the block it just handed out (no double hand-out) and gives up instead of looping when the cache cannot grow |
| `partlimit_failclosed_live.c` | `SetPartitionLimits` fails closed (firstblock > lastblock) for invalid geometry; valid partitions keep their limits |
| `dosenvec_short_live.c` | `PadDosEnvec`: a DosEnvec shorter than de_DosType (but with the cylinder range) is copied and padded with defaults, never read or written past its end, and holds no DosType (so it is neither registered under, nor tested for, a garbage DosType); tables too short to locate the partition are refused |
| `bitmapindex_live.c` | `CheckBitmapIndex` rejects a supermode rootblock whose bitmap index points outside the reserved area at mount; formatter-written layouts are accepted |
| `dd_notify_live.c` | `RemoveNotify` (ACTION_REMOVE_NOTIFY and `Quit`) takes a pending notify request off whichever volume it is queued on (also an older, non-current one, with no current volume, or in `Quit`'s own list) and frees it; our messages still queued at the client's port are taken back and the counters follow, so nothing is left for the caller to free under it |
| `dd_relabel_live.c` | `dd_Relabel` refuses empty names, `:` and `/` and names over 30 characters before touching the volume; the new name is an `AllocVec` block that `FreeDosEntry` can free, and a failed write frees it again |
| `newfile_live.c` | `NewFile` on an overwrite whose `AddToDeldir` fails puts the old chain's head back in the original anode and frees the replacement, instead of leaving the old data blocks orphaned |
| `format_reject_live.c` | `FDSFormat` writes nothing to the disk on any failure (bad DosEnvec, tiny partition, reserved area larger than the partition, a failing bitmap, root directory or deldir) and a rejected DosEnvec leaves the geometry in use alone; the boot block is written last |
| `anode_range_live.c` | `GetAnode`/`SaveAnodeInternal` reject exactly the anode numbers the old divide-based test rejected (every `anodesperblock` 1..0xFFFF, brute-forced), without a 32-bit divide; split mode ignores the limit. Passes on master too: a guard that the 68000 rewrite changed nothing |
| `anode_walk_live.c` | `CorrectAnode` stops on a looping or self-linked anode chain instead of walking forever; long valid chains still resolve and reduce the offset correctly |
| `dblk_walk_live.c` | `GetAnodeOfDBlk` fails on a broken or looping chain and reports the previous anode; `UpdateDBLK` and `RemoveEmptyDBlocks` honour the failure |
| `resizelru_live.c` | The real `ResizeLRU` clamps to `MIN_BUFFERS`/`MAX_BUFFERS`, never grows past the maximum, keeps its counts consistent through growth, shrinking and a failed allocation, and leaves locked, unlinked and pending blocks alone |
| `dir_links_live.c` | The link-list walks (`UpdateLinks`, `UpdateLinkDir`, `MoveLink`, `CreateLink`) follow a valid list of any length and stop on a cyclic one; `CreateLink` refuses a volume, soft-link or dir-link target with `ERROR_OBJECT_WRONG_TYPE` and reports `ERROR_DISK_FULL` (freeing the anode); `CreateRollover` reports `ERROR_DISK_FULL` on each failure path |
| `dir_deletelink_live.c` | `DeleteLink` follows a list of any length, fetches the master before changing anything (an unreadable master fails with nothing modified, a missing one is a dangling link and fine) and refetches it afterwards; `RemapLinks` sets the new parent of a promoted directory, not of a file |
| `dir_setparent_live.c` | `SetDirParent` updates every dirblock of a multi-block, multi-anode directory and fails on an unreadable anode or block; `RenameAndMove` refuses a move into the directory's own subtree or a broken parent chain, and fails with `metadata_error` if the new parent cannot be written |
| `dir_misc_live.c` | `KillEmpty` deletes real directories only (not a link to one); `DeleteDir` deletes a chain of any length and ends on a looping one; `NewDir` reports `ERROR_OBJECT_IN_USE` and releases the anode and entry when it cannot be listed |
| `dir_isempty_live.c` | `DirIsEmpty` follows a chain of empty directory blocks of any length and ends on a looping one with `PFS_ERROR_INCONSISTENT` |
| `dir_entries_live.c` | `ExamineAll`/`FillInData` return `PFS_ERROR_INCONSISTENT` for an entry whose name and comment do not fit it instead of retrying it forever (a plain "does not fit the buffer" stays 0); `SearchInDir` and `SearchInDeldir` handle maximum-length names without overflowing their buffer |
| `dd_openfromlock_live.c` | `dd_OpenFromLock` adds the new fileentry before removing the lock (a failed open leaves the lock usable), hiding the lock from its own conflict check meanwhile |
| `dd_makelink_live.c` | `dd_MakeLink` refuses a hard link to a volume lock, soft link, link to a dir or deldir file with `ERROR_OBJECT_WRONG_TYPE` before `CreateLink` is called; files, real dirs and file links still get through |
| `dd_signalidle_live.c` | `dd_SignalIdle` reports a failed allocation as `ERROR_NO_FREE_STORE` and queues nothing; valid handles are queued and unlinked from the head, middle and tail |
| `packet_dispatch_live.c` | `PacketMutatesFilesystem` reads the `rolloverinfo` of `ACTION_SET_ROLLOVER` from the right argument; `NormalCommands` does not overwrite the reply of FINDUPDATE/FINDOUTPUT/CREATE_DIR with a metadata-error rejection (which would leak the entry they returned) but still rejects every other mutation |
| `volume_probe_live.c` | `ProbeRootBlock` refuses a rootblock without `MODE_HARDDISK` or with `rblkcluster` outside 1..521, retries with 512-byte blocks once, resets the block size first and never touches the data cache |
| `getcurrentroot_live.c` | `GetCurrentRoot` restores block size, softprotect, protectkey and the error handler after a probe and reports the block size it found; `CheckCurrentVolumeBack` accepts any readable disk when there is no current volume |
| `newvolume_live.c` | `SwitchBlockSize` rolls geometry and data cache back when it cannot allocate; `NewVolume` invalidates only clean data-cache slots (dirty ones still belong to the old volume), switches block size only when it differs, and inserts nothing if that fails |
| `drivegeom_live.c` | `GetDriveGeometry` saturates the total sector count instead of wrapping (which would defeat the maximum-size check of `FDSFormat`) and tolerates a zero cylinder size |
| `init_checks_live.c` | `DoPostponed` fails a corrupt "blocks done" count instead of wrapping the free-space counters, and `alloc_available` saturates at zero |
| `initialize_live.c` | `Initialize` rejects a DosEnvec too short to locate the partition and a `de_SizeBlock` outside 1..1024 before allocating anything; defaults `de_MaxTransfer` 0 to 0x20000 and keeps other values; assigns the allocator pointers before every failure return (`Quit` uses them after a failed init); works on `envcopy` for a table without a DosType and in place for one that has it; calls `AddToFSResource` only when `de_TableSize >= DE_DOSTYPE` |
| `fsresource_live.c` | `AddToFSResource` ignores a zero DosType or a NULL seglist; a valid pair is registered, a duplicate DosType keeps the old entry, and failed allocations leave nothing half-built |
| `datacache_live.c` | `InitDataCache` frees both blocks and resets the size after a failed allocation; `FreeDataCache` resets the size, so a later loop over the slots cannot touch freed memory |

Not covered by a live test: code that cannot run without AmigaOS or the m68k
assembler: the startup path (`startup.s`, `FailStartup`), `Quit`, the stack
release in `assroutines.asm`, and the large-file
(`EXTENDED_PACKETS_OS4`) packet paths. `largefile64.py` below covers the large-file
packets; the rest the black-box layer only exercises indirectly (every run mounts
the handler and shuts it down again); the failure branches there rest on code
review in the fix PR.

## black-box AmiFUSE suite — functional / regression / atomicity

Runs the real m68k driver against disk images through
[AmiFUSE](https://github.com/reinauer/AmiFUSE) (amitools/vamos emulation). It
checks that the driver mounts, reads, writes, formats, round-trips data
byte-exactly, and leaves a consistent volume after injected I/O errors, power
cuts and a killed emulator, on a small image and on a supermode image (>4 GB,
which takes the TD64/NSD path). It is a *no-regression* check, not a proof of
any individual fix (the `live/` tests are), with the one exception noted at T6.

```sh
cp /path/to/pfs.hdf tests/fixtures/small.hdf   # a 512-byte-block PFS3 image with a few MB free
uv run zsh tests/make-supermode.sh [--driver /path/to/pfs3aio]   # sparse supermode fixture
uv run zsh tests/run.sh [--driver /path/to/pfs3aio] [--quick] [--stock /path/to/other/driver]
```

Without `--driver` the scripts use `pfs3aio` in the repository root, the output of `make`.

Needs zsh, python3 with `amifuse` importable (`uv run`, see `tests/requirements.txt`), `cmp` and `awk`
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
- **T6** directories, hard links, rename/move and relabel. The `amifuse` command
  line can only read and write whole files, so `dirops.py` builds the same bridge
  `amifuse write` does and sends the AmigaDOS packets itself (create dir, delete,
  rename, `ACTION_MAKE_LINK`, `ACTION_RENAME_DISK`): nested directories, a move
  of a directory with its contents, refusals (existing name, non-empty
  directory, moving a directory into itself), a hard link to a file and to a
  directory that outlives its target, 150 entries in one directory with half of
  them deleted and 20 added, an emptied directory, and a relabel including
  invalid names. It then closes the volume, mounts it again and compares every path,
  every file's bytes and the volume name with its own model; `run.sh` also
  requires `verify` to count the same files, directories and bytes. Refusals
  must come with an error and leave the tree as it was. Unlike T1-T5 this is
  also a proof for one fix: a driver that accepts `a:b` as a volume label ends
  up with that name on disk after the remount.

`fi.py` is the fault-injection / crash harness the suite drives; see its docstring
for the `FI_*` knobs (`FI_SKIP` and `FI_PROGRESS` are what let faults and kills
land mid-operation). It hooks the classic, 64-bit TD and NSD (`NSCMD_TD_*64`)
transfer commands; supermode images use the NSD ones. `timer_device.py` gives the
vamos timer stub deferred completion using the host's monotonic clock, for
AmiFUSE releases that do not ship their own `amifuse.timer_device`.

## timer writeback regression

```sh
uv run tests/writeback.py --image /path/to/pfs.hdf --driver pfs3aio
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

## large file packets

```sh
make largefile
uv run tests/largefile64.py [--driver pfs3aio-largefile] [--blocksize 4096]
```

Formats a sparse 6 GiB image with 4096 byte blocks, switches the large file flag on with `ACTION_SET_LARGEFILE` and drives the 64-bit packets of AmigaOS 4 (`ACTION_CHANGE_FILE_SIZE64`, position, size and the three examine packets) with a file that is grown past 4 GiB: write, seek, read, rename, hard link, truncate and delete. After each step it checks the neighbouring directory entries and the free space. `--blocksize 512` formats 512 byte blocks with a `LARGE_FILE_SIZE` 2 build. Needs a large file build (`LARGE_FILE_SIZE` 1 or 2), `amifuse` and `rdbtool` (`uv venv && uv pip install --no-deps -r tests/requirements.txt`). It takes a few minutes with 4096 byte blocks. `.github/workflows/blackbox-test.yml` runs it as a job of its own, by hand and for changes to the handler sources, the makefile and the test.

## hard links

```sh
make
uv run tests/hardlink.py [--driver pfs3aio]
```

Makes two hard links to a file and deletes the file. The first link has to become the file, with its size and content, and the second link has to follow it. A compiler that lets `CreateLink` skip the link field of the file fails this by leaving the link dangling. Needs `amifuse` and `rdbtool` like the test above and takes seconds. `.github/workflows/blackbox-test.yml` runs it as a job of its own.

## many hard links

```sh
make
uv run tests/manylinks.py [--driver pfs3aio] [--links 4100] [--blocksize 512]
```

Links one file 4100 times. Every link has to be made, and the functions that follow the link list have to work on the result: rewriting the file with a new size (the new size has to reach the first and the last link), moving it into a directory, deleting the last link and deleting the original (the first link has to become the file). A file is written after the links are made and after each of these steps, and has to succeed. If a link or a step fails, the later steps still run and the first step after which the volume refuses writes (213 Disk not validated) is named. The image is then mounted again in a new process, the directory and a link have to read and a file has to be writable. `.github/workflows/blackbox-test.yml` runs it as a job of its own.

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
