# Building pfs3aio with several m68k-amigaos toolchains

Reference for building this tree with gcc 6.5, 13.4 and 16.2, one build per target CPU from 68000 to 68060, and for the compiler issues found while doing it. `tiers.mk` is the list; this file follows it.

Everything is driven by `GNUmakefile`, which only selects a toolchain and an output directory; the flags live in `makefile.gcc`, so the builds differ only by compiler. `makefile` is upstream's and is left alone, so a plain `make` runs ours and `make -f makefile` runs theirs.

```sh
make help
make all          # every toolchain, every release build, then sizes
make all EXTRA=1  # plus the extra builds of `tiers.mk`
make gcc13        # just one
make sibcall-proof
```

Output lands in `compare/<toolchain>/<cpu>/pfs3aio`. `compare/` is gitignored. The normal `make` still builds with the host toolchain and is untouched by any of this.

`install` builds every toolchain and puts them side by side as `INSTALL_DIR/<cpu>/pfs3aio.<toolchain>`, every build, so variants can be compared on hardware without rebuilding between flashes. Defaults are every toolchain and `INSTALL_DIR=/opt/AmigaOS/pfs/v20.0`.

```sh
make install                          # every toolchain, every release build
make install INSTALL_TOOLCHAINS=gcc16 # just one
make install INSTALL_DIR=/tmp/x       # dry run somewhere harmless
```

Resulting layout, one directory per target CPU:

```
/opt/AmigaOS/pfs/v20.0/000/pfs3aio.gcc6     66408
/opt/AmigaOS/pfs/v20.0/000/pfs3aio.gcc13    66104
/opt/AmigaOS/pfs/v20.0/000/pfs3aio.gcc16    66544
```

## The toolchains

Several gcc versions from one image family:

| | version | tag on `docker.io/stefanreinauer/amiga-gcc` | needs |
|---|---|---|---|
| gcc6 | 6.5.0b | `gcc-v6.5.0b-20251218` | pinned, see below |
| gcc13 | 13.4.0b | `gcc-v13.4` | nothing |
| gcc16 | 16.2.0b | `gcc-v16.2` | `-std=gnu17` |

Nothing has to be bootstrapped. `make all` pulls what it needs; each image is 5 to 10 GB unpacked.

They all install into `/opt/amiga`, which the auto-detection in `makefile` finds, so none of them passes `AMIGA_PREFIX`. That is also why a plain `make` still works against a host install: `makefile` prefers `~/opt/m68k-amigaos` and falls back to `/opt/amiga`, and `AMIGA_PREFIX=` or `CC=` override it.

**gcc 16 needs `-std=gnu17`.** It defaults to C23, where `struct.h:306`'s `typedef enum {false, true} bool;` is a syntax error. gcc 6.5 does not know the option at all. gcc 13.4 accepts it but does not need it, and leaving it off keeps its output byte-identical to what the previously used locally built image produced.

**gcc 6.5 is pinned to a dated tag**, not the floating `gcc-v6.5.0b`, because that tag now carries a build that miscompiles `intlcmp`. See "Issue 3" below. `gcc-v6.5.0b-20251218` carries bebbo build `251015095727` and produces binaries byte-identical to a host `~/opt/m68k-amigaos` install of the same build, so the pin changed nothing that ships. `gcc-v6.5.0b-20260629`, build `260602123145`, is also clean but 88 bytes smaller on the 000 tier and 84 on the 020, so it is not a drop-in replacement for the pinned one.

## Results

One build per target CPU, each named after the machine you would install it on; `tiers.mk` holds the table and `BENCHMARK.md` compares them. Sizes in bytes, stock flags, no `COMMON_EXTRA`:

| toolchain | 000 | 020 | 030 | 040 | 060 |
|---|---|---|---|---|---|
| gcc 6.5.0b | 66408 | - | 65036 | 65444 | 65060 |
| gcc 13.4.0b | 66104 | 64152 | 64096 | 64300 | 64152 |
| gcc 16.2.0b | 66544 | 64592 | 64772 | 64944 | 64592 |

gcc 6.5 has no `020` build, recorded as `TIER_SKIP` in `tiers.mk`: its `-m68020-60` is not 68060-safe, see "Target CPU: 68060".

All of them build the tree clean. Under `-Wall`, gcc 6.5 reports 292 warnings.

## Code quality on 68000

Upstream's position in PR #10 was that 6.5.0b stays the compiler for pfs3aio because later gcc versions produce bigger and slower 68000 code, with more absolute addressing and sequences like `cmp.w #0,an` instead of `move.l an,dn`. Counted from `gcc -S` over the whole tree. An earlier version of this table counted absolute data references from `objdump` output; that column was dropped, see "Counting instructions" below.

| | .text | insns | `cmpa #0,aN` | libgcc calls |
|---|---|---|---|---|
| gcc 6.5 / 000 | 60804 | 19275 | 4 | 74 |
| gcc 13.4 / 000 | 60500 | 18723 | 180 | 74 |
| gcc 6.5 / 060 | 60032 | 18319 | 0 | 0 |
| gcc 13.4 / 060 | 59032 | 18142 | 0 | 0 |

**The `cmp #0,an` complaint is real and large.** `tst.l aN` is 68020+, so a pointer NULL test has to be synthesized on 68000. gcc 6.5 uses `move.l aN,dN` (`2008`, one word, register to register). gcc 13.4 uses `cmpa.w #0,aN` (`b0fc 0000`, two words, with an immediate operand fetch). That is 4 occurrences against 180, roughly 360 extra bytes and an extra memory fetch on every pointer check in a driver that checks pointers constantly. On 68020 the difference vanishes: all three emit `tst.l aN`, which is why the 68020 column shows zero.

**The size claim does not hold for 13.4.** Despite that regression, gcc 13.4 produces less text than 6.5 in both builds, so it is winning elsewhere by more than the NULL tests cost. The absolute-addressing half of the claim was not re-verified after the objdump problem was found.

**No algorithmic difference in arithmetic.** They emit the same 74 libgcc helper calls on the `000` build, where 32-bit multiply and divide have no hardware instruction, and none where `muls.l`/`divs.l` exist in hardware. No compiler is picking a better or worse algorithm here.

This is static instruction-mix analysis. For measured cost see "Measuring cost under emulation" below; nothing was timed on hardware.

### Does the regression reach the hot paths?

Largely no. Comparing gcc 6.5 against 13.4 per function, over the routines that run once per block or per directory entry:

| function | g6 | g13 | delta | g13 `cmpa #0` |
|---|---|---|---|---|
| `RawRead`, `RawWrite` | 30, 30 | 34, 34 | +4, +4 | 0 |
| `RawReadWrite_DS`, `_TD` | 342, 392 | 342, 382 | 0, -10 | 0 |
| `DiskRead`, `DiskWrite` | 286, 180 | 278, 174 | -8, -6 | 0 |
| `CachedRead`, `CachedWrite` | 200, 134 | 160, 124 | -40, -10 | 0 |
| `MakeBlockDirty` | 630 | 604 | -26 | 0 |
| `UpdateSlot` | 150 | 126 | -24 | 0 |
| `FlushBlock`, `GetAnode` | 154, 116 | 150, 122 | -4, +6 | 0 |
| `AllocLRU`, `SearchInDir` | 498, 188 | 500, 212 | +2, +24 | 2, 1 |

Across all 15 of those routines gcc 13.4 emits 3 `cmpa #0`, and the core block path emits none. The 180 sit mostly in packet dispatch and setup code: `EntryPoint` 10, `ChangeDirEntry` 5, `dd_SeekRead`, `dd_Open` and `dd_ReadLink` 3 each. `EntryPoint` runs once per DOS packet, where 10 instructions are lost next to a single block read.

Function size is a poor proxy for hot-path cost. `BoundsCheck` is the largest regression anywhere, +238 bytes (106 to 344), and runs on every raw access, but its fast path is two `InPartition` comparisons and all the growth is in the cold out-of-bounds error branch. To the success branch it is 14 instructions and 40 bytes on 6.5 against 15 instructions and 40 bytes on 13.4. The only real hot-path difference is that 13.4 saves five registers on entry (`d2-d3/a2-a4`) where 6.5 saves four (`d2-d3/a2-a3`), one extra push and pop per call.

Excluding `BoundsCheck` and its cold branch, gcc 13.4 is 88 bytes smaller than 6.5 across the hot group, mostly from `CachedRead`, `MakeBlockDirty` and `UpdateSlot`. In the paths that run per block, 13.4 is level or slightly ahead.

Two limits: this counts static instructions, so it says nothing about cache behaviour, chip RAM wait states, or how often each branch is taken; and "hot" is a structural judgement from the call graph, not a profile. The section below replaces the second limit with a measurement.

## Measuring cost under emulation

Two harnesses, both counting the cycles Musashi charges for the instructions it interprets:

- `make driverbench` (`tests/m68k/driverbench.sh`) runs the **real driver** through format, write, read, list and create at both PFS3 block sizes.
- `make bench` (`tests/m68k/bench.sh`) compiles a **synthetic workload** (`tests/m68k/bench_m68k.c`) split into separate loops, so a difference can be attributed to a kind of work. `BREAKDOWN=1` prints the per-loop split.

**The results live in `BENCHMARK.md`**, which carries the driver tables per toolchain and build, the builds compared against each other, and the block-size comparison. What follows here is what the measurements say about the compilers rather than about the filesystem.

### What the 060 build is worth

On a 68020, against the same source built `000`. Positive means the `060` build is cheaper.

| | fmt | wr | rd | ls | create |
|---|---|---|---|---|---|
| gcc 6.5, 512B | +0% | +22% | +2% | -1% | +3% |
| gcc 13.4, 512B | +3% | +17% | -13% | +1% | +2% |
| gcc 16.2, 512B | +4% | +19% | -1% | -1% | +3% |
| gcc 6.5, 4K | +2% | +6% | -5% | -4% | -1% |
| gcc 13.4, 4K | +3% | +5% | -5% | +4% | +5% |
| gcc 16.2, 4K | +4% | +6% | +0% | -1% | +2% |

The payoff sits in the write, where hardware `muls.l`/`divs.l` replace libgcc helpers; the other operations gain little, and on reads the `000` build is sometimes faster. The CPU is worth more than the flag: the same `000` binary moved from a 68000 to a 68020 roughly halves the write, which choosing the `060` build does not approach.

The negative read column is most likely an artifact. The `060` build is compiled `-m68060` while Musashi charges every build at 68020 prices, so its `divu.l` is billed 84 cycles against a magic-multiply sequence on the `000` side. On real 68060 hardware that instruction is the fast one, so these figures are a lower bound.

### What the synthetic workload shows about codegen

One loop in `bench.sh` shows a mechanism. For `if (w & (1UL << b)) sum++` gcc 6.5 emits `btst d5,d6` and a branch, 6 cycles; gcc 16.2 goes branchless with `lsr.l d0,d3` / `and.l #1,d3`. A register-count `lsr.l` costs 8+2n cycles on a 68000, around 39 at n averaged over 0-31, and a flat 6 on a 68020.

It does not reach the driver. Register-count shifts over the whole tree, `000` build:

| build | register-count shifts | all shifts |
|---|---|---|
| gcc 6.5 | 84 | 330 |
| gcc 13.4 | 83 | 206 |
| gcc 16.2 | 84 | 316 |

The gcc versions are level.

So a synthetic loop shows a mechanism and cannot rank toolchains: whether a transformation matters depends on how often the real code hits it.

### Method

vamos runs on machine68k, a Python binding around Musashi. Musashi has no timing model: every instruction it executes is charged a constant from a table selected by CPU type at reset, plus a constant for the addressing mode, plus a per-bit surcharge for shifts on 68010 and below. `btst Dn,Dm` is 6 cycles on a 68000 and 4 on a 68020, `divu.w` 140 and 44, `divu.l` exists only from the 68020 and costs 84 there. A total is what the Motorola timings say that instruction stream costs.

- **Only instructions the emulated CPU interprets are counted.** `exec.library`, `dos.library` and `scsi.device` are Python traps in vamos and amifuse, so a library call costs nothing. A figure is the driver's own CPU work, not the time an operation takes.
- **Nothing outside the CPU core is counted:** no memory wait states, no caches, no chip-RAM contention.
- **Deterministic.** Three consecutive `driverbench` runs, each with a freshly generated random payload, produced identical output, so one run per cell is enough. Host wall clock tracks interpreter throughput and carries no signal.
- **Each run verifies itself.** The synthetic benchmark returns a checksum of its work as its exit code, so a build computing something else is a failure and not a number; the driver benchmark reads the file back and compares it byte for byte.

`Machine.execute(max_cycles)` returns a result object with a `cycles` field; `count_cycles()` in `tests/m68k/vamos_cpu.py` replaces that method with one running the same request in 1000-cycle chunks and summing what each chunk reports.

`sh tests/m68k/sensitivity.sh` varies one parameter at a time, twice per configuration. Every configuration reproduced exactly, and the slopes are linear:

| knob | operation | marginal cost, first interval | second interval |
|---|---|---|---|
| directory entries | `ls` | 2708 cycles per entry | 2766 |
| directory entries | `rd` | 208 cycles per entry | 391 |
| directory entries | `create` | 333 cycles per entry | 609 |
| payload bytes | `wr` | 1.422 cycles per byte | 1.422 |
| payload bytes | `rd` | 0.0966 cycles per byte | 0.0973 |
| partition MiB | `fmt` | 3125 cycles per MiB | 2927 |

Everything else stays flat: entry count does not move `fmt` or `wr`, payload size does not move `fmt`, `ls` or `create`, partition size moves nothing but `fmt`.

`rd` grows with the entry count because a read has to find the file first, over the same directory scan that shows up in `create`. At the `driverbench` defaults a read is 51 kilocycles of transfer and 20 of scan against about 110 of fixed packet handling, so the `rd` column compares packet paths more than copy loops.

Emulator behaviour worked around in `tests/m68k/vamos_cpu.py` and `tests/m68k/driverbench.py`:

- timer requests are answered at once, which would expire the handler's five second writeback deadline between two packets. `driverbench.py` holds the replies with `tests/timer_device.py` and never advances the clock.
- vamos derives `exec.library`'s `AttnFlags` from the CPU name but compares against `"68030(fake)"`, which machine68k no longer reports, so a `-C 68030` run leaves the field at 0 and any 68020+ binary aborts with alert `00068020` before reaching `main`.
- Cycles have to be accumulated in small chunks. amifuse asks for the whole handler in one call, and that call unwinds with `UnsupportedFeatureError` at `WaitPkt`: the result object is never produced and everything up to that point is lost. At a 100000-cycle chunk the same PFS3 write measured 72681 cycles for one build and 886009 for another while both issued exactly 87 block writes. At 1000, vamos's own figure, the two come out 929k and 1001k, and dropping to 200 moves them less than 1%.
- amifuse hardwires the emulated CPU to 68020 with no option for it. `vamos` takes `-C`, but not `68010`, so there is no 68010 column.

## Telling the variants apart

Every build reports the same `Professional-File-System-III 20.0`, so neither `C:Version` nor the ROM module listing could say which one was installed. `boot.c` puts the compiler, the build and the range it runs on into the version string, immediately after the revision:

```
$VER: Professional-File-System-III 20.0 [gcc16.1/030][020-040][jpu/e4bb995] PFS3AIO-VERSION (1.9.2026) written by ...
```

| build | flag | runs on | tag |
|---|---|---|---|
| `000` | `-m68000` | 68000+ | `[<cc>/000][000+]` |
| `020` | `-m68020-60` | 68020+ | `[<cc>/020][020+]` |
| `030` | `-m68030` | 68020 to 68040 | `[<cc>/030][020-040]` |
| `040` | `-m68020-40` | 68020 to 68040 | `[<cc>/040][020-040]` |
| `060` | `-m68060` | 68020+ | `[<cc>/060][020+]` |
| `080` | `-m68060 -mtune=68080 -Wa,-m68080` | 68080 only | `[<cc>/080][080]` |
| `080a` | `-m68080` | 68080 only, line-A | `[<cc>/080a][080]` |

`<cc>` is `gcc6.5`, `gcc13.4` or `gcc16.2`. The first field is what the build is tuned for, the second where it runs. The range alone would not identify a binary: `030` and `040` share `020-040`, `020` and `060` share `020+`, `080` and `080a` share `080`.

The last bracket is the origin, `[<fork>/<tag or commit>]`, the tag when the build came from a tagged commit. A build made with upstream's `makefile` carries no origin bracket, since it passes none of these defines.

**Ask the file, not the running system.** Checked on the 68060 target:

| | shows the tag |
|---|---|
| `Version L:pfs3aio FULL`, the file on disk | yes |
| `Version pfs3aio FULL`, the resident module | no |

`Version` scans a file for `$VER:`, so the tag shows there. The resident module reports from `rt_Name` and `rt_Version` and never reads `rt_IdString`, which is where the tag sits. Moving it into `rt_Name` means changing `shortname`, which `fsresource.c` also uses for `fsr_Creator` and for the FileSystem.resource entry name. A ROM build is identified from the binary before flashing.

## Target CPU: 68060

**A `-m68020` build does not run on a 68060 because it uses 64-bit multiplication.** The 68060 implements part of the 68020 integer instruction set in software, not hardware: those instructions trap and are emulated by the 68060 software support package, loaded from disk at boot. A filesystem linked into Kickstart ROM runs before that, so for ROM use they must not be generated at all. `-m68060` does not generate them, which is what the `68060` build is built with.

The complete list, from the Motorola porting note: 64-bit `DIVU.L`/`DIVS.L`, 64-bit `MULU.L`/`MULS.L`, `MOVEP`, `CHK2`, `CMP2`, `CAS2`, and `CAS` with a misaligned effective address. **Bitfield instructions are not on it** and are implemented in hardware, so they need no avoiding.

Counted from compiler assembly output over the whole tree, at upstream `211f7f0`:

| flags | bitfield | 64-bit mul | 64-bit div |
|---|---|---|---|
| gcc 6.5 `-m68000` | 0 | 0 | 0 |
| gcc 6.5 `-m68020` | 37 | 10 | 0 |
| gcc 6.5 `-m68060` | 36 | 0 | 0 |
| gcc 6.5 `-m68020-60` | 37 | 10 | 0 |
| gcc 13.4 `-m68000` | 0 | 0 | 0 |
| gcc 13.4 `-m68020` | 30 | 11 | 0 |
| gcc 13.4 `-m68060` | 28 | 0 | 0 |
| gcc 13.4 `-m68020-60` | 28 | 0 | 0 |

The 64-bit multiplies come from GCC's divide-by-constant optimization, a multiply-high against a magic constant such as `mulu.l #3435973837,d2:d1`. `-m68020` emits them, `-m68060` does not. `-mnobitfield` removes the bitfield instructions in both compilers.

Hardware results, `bare` ROM profile on 68060. The `020`, `030` and `040` builds are newer than this table; `030` and `040` cannot run here by construction:

| build | result |
|---|---|
| gcc 6.5 / 000 (`-m68000`) | **works** |
| gcc 6.5 / 060 (`-m68060`) | **works** |
| gcc 13.4 / 000 (`-m68000`) | **works** |
| gcc 13.4 / 060 (`-m68060`) | **works** |
| gcc 16.1 / 000 (`-m68000`) | **works** |
| gcc 16.1 / 060 (`-m68060`) | **works** |
| any / `-m68020` | fails |

**The `060` build is compiled `-m68060`.** That drops the 64-bit multiplies and keeps the rest 68020 compatible, so the binary still runs on 68020 and 68030. Both builds verify clean:

| build | flags | 64-bit mul/div | `MOVEP` | `CHK2`/`CMP2` | `CAS2` | FPU |
|---|---|---|---|---|---|---|
| `000` | `-m68000` | 0 | 0 | 0 | 0 | 0 |
| `060` | `-m68060` | 0 | 0 | 0 | 0 | 0 |

`-m68020-60` is 68060-safe from gcc 13.4 on; gcc 6.5 still emits the 64-bit multiplies under it. `-m68060` selects the FPU libgcc, `libm020/libm881`, from which the driver pulls nothing, having no floating point.

Every toolchain boots here once the build avoids `-m68020`, so the choice of compiler is about code quality, not whether it runs.

## Apollo 68080

The AC68080 runs everything from the 68000 to the 68060 except the MMU instructions, so the `060` build works on it. Two builds use what the core adds, `080` and `080a`, and only gcc 6.5 can produce either: 13.4 and 16.1 reject `-m68080`, `-mcpu=68080` and `-march=68080` alike.

What `-m68080` puts in the binary, counted over `directory.c`, `disk.c`, `anodes.c`, `allocation.c` and `lru.c`, 10230 instructions of which 408 are not 68060 instructions:

| instruction | count | what it does |
|---|---|---|
| `mov3q.l` | 132 | ColdFire ISA_B quick immediate, -1 and 1 to 7, to any destination |
| `moviw.l` | 117 | 16-bit sign-extended immediate stored as a longword |
| `cmpiw.l` | 82 | compare a longword against a 16-bit sign-extended immediate |
| `clr.q` | 22 | 64-bit clear, through `(an)+` it zeroes two longwords at a time |
| `dbral` | 22 | `DBRA` with a 32-bit counter |
| `mvs.b`, `mvs.w` | 20 | ColdFire ISA_B move with sign extension |
| `mvz.w` | 3 | move with zero extension |
| `addiw.l` | 2 | add a 16-bit sign-extended immediate to a longword |
| `mulu.l`/`muls.l` `dh:dl` | 8 | the 68020 64-bit multiply, absent on the 68060, back on the 68080 |

`m68k-amigaos-as` rejects every one of them under `-m68020`, `-m68040` and `-m68060` with "needs 68080". Most encode in the A-line space, `clr.q` as `ae18`, `mov3q.l` as `a240`, `mvs.b` as `a301`, `mvz.w` as `abc5`, `moviw.l` as `a214`, where a classic core takes the line-A exception. `addiw.l` is `06c5`, the `ADDI` opcode with the size field a classic core rejects, and `cmpiw.l` is `4e00`, unassigned in the `4e` line. Either way a 68060 faults rather than computing something wrong.

`dbral` is the exception: it assembles to `51cf`, the ordinary `DBF` encoding, and only the counter width differs. That one would not trap on a 68060, it would wrap at 16 bits.

### The line-A group is not usable

`AC68080PRM.pdf` p.10 marks the whole group "do not use, only available on scores 10280 to 10904", so a core outside that range faults on it: on an IceDrake V4 a fat95 build using `clr.q` and `movs.b` fails with `8000000A`. The `20260902-1` binary built with `-m68080` holds 565 line-A opcodes against 0 in its `68060` build, from offset `$1e4` on, so it faults at startup.

gcc 6.5 has no switch that drops the group, but `-mtune=68080` emits what the core adds without it:

| build | flags | carries |
|---|---|---|
| `080` | `-m68060 -mtune=68080 -Wa,-m68080` | `cmpiw.l`, `addiw.l`, `dbral`, 64-bit `mulu.l` |
| `080a` | `-m68080` | those plus the line-A group |

`-Wa,-m68080` is needed because the assembler refuses `cmpiw.l` under `-m68060`. Both builds are 68080 only: `cmpiw.l` and the 64-bit multiply fault on a 68060.

`make check-linea` scans the generated assembly of every build and expects a count only from `080a`.

No AMMX: not one E-register or SIMD instruction. `-m68080` uses the integer extensions only, so AMMX needs intrinsics or assembly.

`-m68080` selects the base multilib, so libgcc and libnix come from the plain 68000 build. The driver calls almost nothing from them, since the 68080 has the division and multiplication in hardware.

`080a` is the smallest gcc 6.5 produces, 63988 bytes, from the shorter immediate forms; `080` is 64760 and the `060` build 65060.

### Counting instructions: use the compiler, not objdump

Scan the `.s` from `gcc -S`, not `objdump -d` output. Disassembling `.text` decodes jump tables as instructions and invents them. In this tree `objdump` reported two `movep` in `dostohandlerinterface.c`, which do not exist: both were switch jump-table data, one of them sitting directly under its own `jmp (table,pc,d0.w)`. No compiler here emits `movep` at any CPU setting. The `cmpa #0,aN` counts above were re-verified from `.s` and were correct.

## Testing

Layers of test, in increasing cost and coverage, then the benchmarks:

```sh
bash tests/live/run.sh                  # host, lifted functions (upstream's is zsh)
make check-68060    # static, instructions the 68060 lacks
make verify         # the audit plus format/write/read per build
zsh tests/run.sh --driver <binary>      # emulated 68k, upstream's full black-box suite

make driverbench    # cycles: the real driver
make bench          # cycles: synthetic workload, all toolchains
sh tests/m68k/sensitivity.sh            # does driverbench react to its knobs?
```

`tests/live` compiles driver functions with the host compiler: cheap, blind to target type widths, unable to run assembly.

`make verify` runs each built binary against a fresh `rdbtool` image: format, write 32 KB, read back, compare, each on the lowest CPU its build runs on. All of them pass. The benchmarks are documented in "Measuring cost under emulation" and in `BENCHMARK.md`; they check their own results, so a cell carries either a cycle count or a failure.

**Emulation cannot clear a build for the 68060.** Musashi, the CPU behind vamos, emulates up to 68040, which has the 64-bit `MULU.L`/`MULS.L` a 68060 lacks: a `-m68020` build formats, writes and reads correctly under emulation and is the same build that does not boot on the hardware.

`make check-68060` closes that gap statically. It emits assembly for every toolchain and build with that toolchain's own flags, scans the ones that claim to reach a 68060 for 64-bit `MULU.L`/`MULS.L` and `DIVU.L`/`DIVS.L`, `MOVEP`, `CHK2`, `CMP2` and `CAS2`, and fails naming the offending lines. Every scanned build is clean; the same audit on a `-m68020` build reports ten. `verify` depends on it, so a green `verify` is both functional and 68060-safe.

Scan assembly, never `objdump`: disassembling `.text` decodes switch jump tables as instructions, and reported two `MOVEP` in `dostohandlerinterface.c` that do not exist.

### Getting the emulator working

`amitools` declares `machine68k` without a version bound, so a fresh install pairs amitools 0.8.0 with machine68k 0.4.1, where `Traps.set_exc_func` no longer exists and vamos dies on startup. Installing `amifuse` pulls a working combination (amitools 0.8.1 alongside its own pinned machine68k). Building `machine68k` from source needs the Python headers: on Fedora, `python3-devel`.

Fixtures are not in the repo. Create one with `rdbtool` and format it with the driver itself:

```sh
rdbtool -f tests/fixtures/small.hdf create size=64Mi + init + add name=DH0 \
        size=95% dostype=0x50465303
FI_CMD=none python3 tests/fi.py format tests/fixtures/small.hdf DH0 TESTVOL \
        --driver compare/gcc6/000/pfs3aio
```

## Issue 1: sibling-call miscompilation (handled)

Affects gcc 13.4, not 6.5. Already fixed by `-fno-optimize-sibling-calls` in `makefile`, which came from upstream commit `82afc5f`. **Do not remove that flag.** Nothing else is required.

`RawRead`/`RawWrite` in `disk.c` end in a tail call to `RawReadWrite_DS`/`_TD`, so gcc turns the call into a jump. With `-mregparm=3` the arguments live in registers and have to be shuffled first. gcc 13.4 gets the shuffle wrong: `blocknr` is never moved into d2 where the callee reads it, and d2 is restored to the caller's value before the jump. Every raw sector access then uses a wrong block number.

Reproduce with `make sibcall-proof`, which builds `disk.o` both ways and prints `RawRead`. With sibling calls enabled the `move.l d1,d2` is missing:

```
sibling calls on                  sibling calls off
  move.l  d2,-(sp)                  move.l  d2,-(sp)
  move.l  d0,d1                     move.l  d1,d2    <-- blocknr, absent above
  moveq   #0,d0                     move.l  d0,d1
  ...                               moveq   #0,d0
  move.l  (sp)+,d2                  ...
  bra.w   _RawReadWrite_DS          jsr     _RawReadWrite_DS
```

That d2 is really `blocknr` is visible in the callee, which compiles `if (blocknr == (ULONG)-1)` and `blocknr += g->firstblock` to `cmp.l d2,d1` and `add.l 88(a1),d2`.

Cost of the flag: 0 bytes on gcc 6.5 (binaries are byte-identical, it never emits the sibling call here), 60/72 bytes on gcc 13.4.

## Issue 2: strict aliasing (fixed)

`GetExtraFields` filled a `struct extrafields` through a `UWORD *` and `AddExtraFields` read it that way. gcc 6.5 reports 34 `-Wstrict-aliasing` warnings.

gcc 13.4 and 16.2 miscompile it: `CreateLink` reuses the `link` value it stored itself before `GetExtraFields`, so the linked file never gets its link field. Deleting it then frees the file and leaves the link dangling. gcc 6.5 is not affected. The test is deleting the original of a hard link and reading the link (`tests/m68k/hardlink.py`, a column of `variants.sh`).

Both functions go through a union of the struct and a `UWORD` array now. Every build also passes `-fno-strict-aliasing`, so other accesses of that kind cannot go wrong.

## Issue 3: intlcmp post-increment miscompilation (avoided by pinning)

Affects bebbo build `20260731` of gcc 6.5.0b, which is what the floating `docker.io/stefanreinauer/amiga-gcc:gcc-v6.5.0b` tag carries. Avoided by pinning `GCC6_TAG` to `gcc-v6.5.0b-20251218`. Not present in build `251015095727` (that tag, and a host install of the same build) or in `260602123145` (`gcc-v6.5.0b-20260629`), so it landed between June and July 2026.

`intlcmp` in `assroutines.c` starts by consuming one length byte from each side:

```c
unsigned char len = *a0++;
if (len != *a1++)
	return 0;
```

At `-m68020` or `-m68060` with `-fbbb=+`, the bad build emits:

```
_intlcmp:
	movem.l d5/d4/d3/d2,-(sp)
	move.b (a0)+,d1
	cmp.b (a1)+,d1
	jne .L25
	addq.l #1,a1        <- a1 advanced a second time
	jra .L27
```

The `i` pass, "use post increment on addresses", rewrote `cmp.b (a1),d1` into `cmp.b (a1)+,d1` and did not delete the `addq.l #1,a1` that had followed it. It got `a0` right. `a1` therefore runs one byte ahead for the whole comparison loop. A clean build of the same source emits no `addq.l #1,a1` at all.

`intlcmp` is the case-insensitive name comparison behind directory lookup, so the symptom is narrow and misleading: the 060 build formats and writes correctly, and `amifuse ls` lists the directory, but no file can be opened by name. The data on disk is fine, which is provable by reading it back with a good build.

Localisation, in order:

- `-fbbb=-` fixes it, `-O1` and `-O0` fix it, `-m68020` does not: it is not 68060 scheduling.
- Leave-one-out over the fifteen passes in `-fbbb=+` (`abcefhilmnprsz0`): only removing `i` fixes it.
- Per-object bisect over the C files, all built with `-fbbb=+` except one: only `assroutines.c` fixes it.
- The `000` build is untouched. With `-m68000` the `i` pass produces byte-identical assembly for `assroutines.c` either way.

If a floating tag has to be used, `-fbbb=abcefhlmnprsz0` is the whole-tree workaround. It passes every build and costs 112 bytes on the `000` one and 96 on the others.

Worth reporting to AmigaPorts/m68k-amigaos-gcc.
