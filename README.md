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
- vbcc does not build the `l` tiers.
- `tests/m68k/largefile.sh` checks both formats against both builds, `tests/m68k/largefile64.py` takes a file past 4 GiB under emulation (about 15 minutes).

## Building

```sh
make              # every toolchain, one binary per target CPU, into compare/
make install      # side by side as pfs3aio.<toolchain> under /opt/AmigaOS/pfs/v20.0
make dist         # the release set into dist/, which git ignores
make verify       # format, write, read back, plus the instruction audits
make -f makefile  # upstream's own single-binary build, untouched
```

**Branches:** `master` mirrors upstream, development happens on `jpu`.
