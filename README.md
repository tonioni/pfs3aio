# pfs3aio

PFS3 All-In-One by Toni Wilen, based on Professional File System III.
This product includes software developed by Michiel Pelt.

## Build and package

Use the Amiga GCC cross-toolchain and a Unix LHA archiver that supports
creating archives (not the extraction-only `lhasa` utility):

```
make
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

The archive contains the standard `pfs3aio` handler, Aminet readme, license,
revision history and `Build.txt` with the Git revision and compiler version.
It does not contain `pfs3aio-custom`, utilities, or pfsdoctor.

The distribution version comes from `git describe --tags --dirty --always`,
with a leading `v` or `release_` removed. It can be overridden with
`make lha FULL_VERSION=3.2`. This does not change the handler's embedded
filesystem version in `versionhistory.doc`.

`dist.readme.in` is based on Toni Wilen's published 3.1 Aminet readme:
https://aminet.net/disk/misc/pfs3aio.readme
Its version and archive contents have been adapted for these builds. Review
the uploader details and add release-specific changes before a release.

## GitHub Actions

CI builds the handler and LHA on branch pushes and pull requests, runs the
native regression tests, and uploads both the handler and distribution pair
as workflow artifacts. The build uses Stefan Reinauer's m68k-amigaos-gcc
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
