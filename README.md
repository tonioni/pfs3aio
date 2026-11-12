# pfs3aio

PFS3 All-In-One by Toni Wilen, based on Professional File System III.
This product includes software developed by Michiel Pelt.

## Build and package

Use the Amiga GCC cross-toolchain and a Unix LHA archiver that supports
creating archives (not the extraction-only `lhasa` utility):

```
make
make largefile
make check
make lha
```

Override `CC` and `LHA` if they are not installed at their default locations.
`make check` uses the host C compiler and requires zsh; it does not execute
the compiled Amiga handler. See [tests/README.md](tests/README.md) for the
emulated-handler tests and their additional dependencies.

`make lha` builds the handler and produces a matching pair in `dist/`:

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

`make` builds the normal pair. `make largefile` builds the large-file pair;
`make all-variants` builds all four. Objects and generated dependency files
are isolated in `build/normal` and `build/largefile`, so switching variants
cannot reuse objects compiled with the other setting. `make lha` builds
all four, while `make archive` packages four already-built handlers (used
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
`make lha FULL_VERSION=3.2`. This does not change the handler's embedded
filesystem version in `versionhistory.doc`.

`dist.readme.in` is based on Toni Wilen's published 3.1 Aminet readme:
https://aminet.net/disk/misc/pfs3aio.readme
Its version and archive contents have been adapted for these builds. Review
the uploader details and add release-specific changes before a release.

## Floppy image

`make floppy` builds all four handlers and writes `pfs3aio.adf`, an 880K
AmigaDOS floppy image named `PFS3AIO` containing the handlers, `LICENSE` and
`versionhistory.doc`. `tools/mkadf.py` needs just Python. `make clean`
removes the image.

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
