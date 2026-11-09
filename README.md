# pfs3aio

Fork of [tonioni/pfs3aio](https://github.com/tonioni/pfs3aio), the PFS3 filesystem handler for AmigaOS.

It exists to build the handler from source per target CPU, from several toolchains. Along the way it shows what the compiler and the target CPU are actually worth.

- [TOOLCHAINS.md](TOOLCHAINS.md): how the builds are produced, what each toolchain does to the code, and what the 68060 constrains.
- [BENCHMARK.md](BENCHMARK.md): what filesystem operations cost in CPU cycles per toolchain, build and emulated CPU.

## Binaries

Every binary names its own build in the `$VER` string, readable with `Version <file> FULL`. [BENCHMARK.md](BENCHMARK.md) compares them.

**These are not official builds and they are untested. Use them at your own risk.** They exist so the differences between the compilers and the target CPUs can be tried on real hardware. How a build behaves on your machine, and anything you notice about the builds themselves, is welcome in [issues here](https://github.com/pulchart/pfs3aio/issues), and a fix is best sent as a pull request. A bug in PFS3 itself belongs [upstream](https://github.com/tonioni/pfs3aio/issues), once it reproduces with an official build and is not down to the compiler.

Builds are attached to the [releases](https://github.com/pulchart/pfs3aio/releases), each with a table of every build. Files are named `pfs3aio-<tag>-<tier>-<toolchain>`.

## Large files

A build with an `l` after the tier (`020l`) handles files above 4 GiB. The others keep the 32 bit limit.

- A volume formatted by an `l` build is PFS\2 with the large file flag. Builds without `l` refuse to mount it. Volumes of the others stay readable and writable by both.
- An existing PFS\2 volume turns on with `tools/setlargefile`, a PFS\1 volume has to be reformatted. There is no way back.
- Programs reach such a file with the 64 bit packets of AmigaOS 4 (`ChangeFilePosition64`, `GetFilePosition64`, `ChangeFileSize64`, `GetFileSize64`). AmigaOS 3.2 has no 64 bit dos.library calls: sequential `Read` and `Write` pass 4 GiB, `Seek`, `SetFileSize` and `Examine` stay 32 bit. `Examine` shows 4 GiB - 1 for a larger file.
- `tests/m68k/largefile.sh` checks both formats against both builds, `tests/m68k/largefile64.py` takes a file past 4 GiB under emulation (about 15 minutes).

## Building

```sh
make              # every toolchain, one binary per target CPU, into compare/
make install      # side by side as pfs3aio.<toolchain> under /opt/AmigaOS/pfs/v20.0
make dist         # the release set into dist/, which git ignores
make EXTRA=1      # also the 030, 040, 060 and 080a builds, never part of a release
make verify       # format, write, read back, plus the instruction audits
make -f makefile  # upstream's own build and LHA package, see below
```

**Branches:** `master` mirrors upstream, development happens on `jpu`.

## Upstream build and package

Use the Amiga GCC cross-toolchain and a Unix LHA archiver that supports
creating archives (not the extraction-only `lhasa` utility):

```
make -f makefile
make -f makefile largefile
make -f makefile check
make -f makefile lha
```

Override `CC` and `LHA` if they are not installed at their default locations.
`make -f makefile check` uses the host C compiler and requires zsh; it does not execute
the compiled Amiga handler. See [tests/README.md](tests/README.md) for the
emulated-handler tests and their additional dependencies.

`make -f makefile lha` builds the handler and produces a matching pair in `dist/`:

- `pfs3aio_<version>.lha`
- `pfs3aio_<version>.readme`

The archive contains these four handlers, the Aminet readme, license,
revision history and `Build.txt` with the Git revision and compiler version:

- `pfs3aio`: normal build (`LARGE_FILE_SIZE=0`).
- `pfs3aio-custom`: normal build wrapped for broken RDB filesystem loaders.
- `pfs3aio-largefile`: large-file build (`LARGE_FILE_SIZE=1`).
- `pfs3aio-largefile-custom`: large-file build with the same custom wrapper.

The archive does not contain utilities or pfsdoctor. Large-file support is
opt-in: choose the large-file handler explicitly; the normal handler does
not gain support for large-file volumes.

`make -f makefile` builds the normal pair. `make -f makefile largefile` builds the large-file pair;
`make -f makefile all-variants` builds all four. Objects and generated dependency files
are isolated in `build/normal` and `build/largefile`, so switching variants
cannot reuse objects compiled with the other setting. `make -f makefile lha` builds
all four, while `make -f makefile archive` packages four already-built handlers (used
by CI after downloading the matrix artifacts).

Both unwrapped handlers link to one CODE hunk using `pfs3aio.ld`, including
libnix's ctype-table pointer. The freestanding link maps `_SysBase` to the
ExecBase pointer at address 4 rather than requiring C runtime startup.

The custom wrapper embeds its normal executable verbatim and applies that
executable's relocations in-place on first entry, then flushes the CPU
cache on Exec v36 or later before entering the handler. It is not
ROMmable. It is only needed for controller loaders which cannot handle
relocations. `tools/hunk.py` rejects unsupported hunk layouts and checks
that each custom executable has one CODE hunk, no outer relocations and
an unchanged embedded handler.

The distribution version comes from `git describe --tags --dirty --always`,
with a leading `v` or `release_` removed. It can be overridden with
`make -f makefile lha FULL_VERSION=3.2`. This does not change the handler's embedded
filesystem version in `versionhistory.doc`.

`dist.readme.in` is based on Toni Wilen's published 3.1 Aminet readme:
https://aminet.net/disk/misc/pfs3aio.readme
Its version and archive contents have been adapted for these builds. Review
the uploader details and add release-specific changes before a release.

## GitHub Actions

CI builds the normal and large-file pairs in separate matrix jobs on branch
pushes and pull requests, runs the native regression tests, then packages
the downloaded handlers into an LHA without rebuilding them. Both handler
pairs and the distribution pair are uploaded as workflow artifacts. The
build uses Stefan Reinauer's m68k-amigaos-gcc
13.4 container (`stefanreinauer/amiga-gcc:gcc-v13.4`).

Pushing a `v*` or `release_*` tag runs the same build and tests, then creates
a **draft** GitHub release with the LHA and matching Aminet readme. Review
the assets and generated release notes before publishing it.

Publishing a non-prerelease GitHub release triggers the Aminet workflow.
It downloads the reviewed LHA and readme from that release (no rebuild),
renames the pair to the established `pfs3aio.lha` / `pfs3aio.readme` names,
and uploads them to `disk/misc` using the same Aminet action as ODFileSystem.
The uploader email comes from the readme; no FTP credentials are required.
Drafts and prereleases are not uploaded. A manual workflow dispatch can
retry an existing published release by tag; it refuses drafts/prereleases.
No workflow uploads anything merely because CI succeeds.
