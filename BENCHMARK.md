# PFS3 handler: emulated-cycle comparison of m68k toolchains

What decides the cost of a filesystem operation: the CPU model, the compiler, or the CPU the build is aimed at? I ran driver operations under vamos and counted the cycles the emulator charges for the instructions it executes. An estimate only: waiting on the system and anything outside the CPU core is not modelled.

The operations, on a 32 MiB image:

- `fmt` format it fresh
- `wr` write a 512 KiB file
- `rd` read that file back
- `ls` list a directory holding 64 entries
- `create` write one more file into that directory

`rd`, `ls` and `create` run against a copy of one template image per block size, so every build starts from the same on-disk state.

The handler arms a five second writeback deadline on `timer.device`. The emulator answers timer requests at once, which would expire it between two packets, so the harness holds the replies and never advances the clock. Figures taken with immediate replies are not comparable.

Toolchains: gcc 6.5.0b, 13.4.0b and 16.2.0b, all from `docker.io/stefanreinauer/amiga-gcc`.

The tables use the `000` build and the `060` build of each toolchain. The rest are under "Which build for which machine".

## Format and write

Kilocycles. The two right-aligned columns are speed relative to row 3, the baseline for all three tables; 0.50x means twice the cycles.

| # | compiler | CPU | build | 512 fmt | 512 wr | 4K fmt | 4K wr | wr speed 512 | wr speed 4K |
|---|---|---|---|---|---|---|---|---:|---:|
| 1 | gcc 6.5 | 68000 | 000 | 585 | 1819 | 639 | 851 | _0.37x_ | _0.45x_ |
| 2 | gcc 6.5 | 68020 | 000 | 273 | 859 | 308 | 406 | _0.78x_ | _0.94x_ |
| 3 | gcc 6.5 (baseline) | 68020 | 060 | 272 | 672 | 303 | 383 | _1.00x_ | _1.00x_ |
| 4 | gcc 13.4 | 68000 | 000 | 615 | 1888 | 672 | 946 | _0.36x_ | _0.40x_ |
| 5 | gcc 13.4 | 68020 | 000 | 295 | 894 | 323 | 441 | _0.75x_ | _0.87x_ |
| 6 | gcc 13.4 | 68020 | 060 | 286 | 746 | 314 | 417 | _0.90x_ | _0.92x_ |
| 7 | gcc 16.2 | 68000 | 000 | 608 | 1877 | 652 | 885 | _0.36x_ | _0.43x_ |
| 8 | gcc 16.2 | 68020 | 000 | 295 | 892 | 321 | 417 | _0.75x_ | _0.92x_ |
| 9 | gcc 16.2 | 68020 | 060 | 282 | 726 | 308 | 392 | _0.93x_ | _0.98x_ |

## Read and list

Kilocycles, same rows and baseline. Row 3 is the cheapest write and format; a few other rows list or read for less.

| # | compiler | CPU | build | 512 rd | 512 ls | 4K rd | 4K ls | ls speed 512 | ls speed 4K |
|---|---|---|---|---|---|---|---|---:|---:|
| 1 | gcc 6.5 | 68000 | 000 | 352 | 655 | 417 | 732 | _0.44x_ | _0.46x_ |
| 2 | gcc 6.5 | 68020 | 000 | 162 | 284 | 194 | 323 | _1.01x_ | _1.04x_ |
| 3 | gcc 6.5 (baseline) | 68020 | 060 | 159 | 287 | 203 | 337 | _1.00x_ | _1.00x_ |
| 4 | gcc 13.4 | 68000 | 000 | 379 | 732 | 500 | 865 | _0.39x_ | _0.39x_ |
| 5 | gcc 13.4 | 68020 | 000 | 170 | 289 | 223 | 348 | _0.99x_ | _0.97x_ |
| 6 | gcc 13.4 | 68020 | 060 | 192 | 286 | 234 | 335 | _1.00x_ | _1.01x_ |
| 7 | gcc 16.2 | 68000 | 000 | 372 | 675 | 450 | 763 | _0.43x_ | _0.44x_ |
| 8 | gcc 16.2 | 68020 | 000 | 174 | 285 | 208 | 325 | _1.01x_ | _1.04x_ |
| 9 | gcc 16.2 | 68020 | 060 | 175 | 288 | 209 | 327 | _1.00x_ | _1.03x_ |

## Create a file

Kilocycles, one more file written into the directory the previous table lists. Same rows and baseline.

| # | compiler | CPU | build | 512 create | 4K create | create speed 512 | create speed 4K |
|---|---|---|---|---|---|---:|---:|
| 1 | gcc 6.5 | 68000 | 000 | 388 | 579 | _0.47x_ | _0.50x_ |
| 2 | gcc 6.5 | 68020 | 000 | 187 | 284 | _0.97x_ | _1.01x_ |
| 3 | gcc 6.5 (baseline) | 68020 | 060 | 181 | 288 | _1.00x_ | _1.00x_ |
| 4 | gcc 13.4 | 68000 | 000 | 406 | 662 | _0.45x_ | _0.44x_ |
| 5 | gcc 13.4 | 68020 | 000 | 192 | 312 | _0.94x_ | _0.92x_ |
| 6 | gcc 13.4 | 68020 | 060 | 189 | 295 | _0.96x_ | _0.98x_ |
| 7 | gcc 16.2 | 68000 | 000 | 391 | 592 | _0.46x_ | _0.49x_ |
| 8 | gcc 16.2 | 68020 | 000 | 186 | 283 | _0.97x_ | _1.02x_ |
| 9 | gcc 16.2 | 68020 | 060 | 181 | 277 | _1.00x_ | _1.04x_ |

## Which build for which machine

One build per target CPU, named after the machine it is installed on. The `$VER` tag says which build a binary is and where it runs (`Version <file> FULL`):

| build | flag | runs on | tag |
|---|---|---|---|
| `000` | `-m68000` | 68000+ | `[<cc>/000][000+]` |
| `020` | `-m68020-60` | 68020+ | `[<cc>/020][020+]` |
| `030` | `-m68030` | 68020 to 68040 | `[<cc>/030][020-040]` |
| `040` | `-m68020-40` | 68020 to 68040 | `[<cc>/040][020-040]` |
| `060` | `-m68060` | 68020+ | `[<cc>/060][020+]` |
| `080` | `-m68060 -mtune=68080 -Wa,-m68080` | 68080 only | `[<cc>/080][080]` |
| `080a` | `-m68080` | 68080 only, line-A | `[<cc>/080a][080]` |

`<cc>` is `gcc6.5`, `gcc13.4` or `gcc16.2`. The first field is what the build is tuned for, the second where it runs. `030` and `040` stop at the 68040 because `-m68030` and `-m68020-40` emit 64-bit `MULU.L` for divide-by-constant, which the 68060 does not implement.

gcc 6.5 has no `020` build: its `-m68020-60` emits those multiplies too, so the build would not run on a 68060.

The `080` and `080a` builds are gcc 6.5 only and have no figures: they use Apollo instructions no emulator implements. See TOOLCHAINS.md, "Apollo 68080".

gcc 16.2 on an emulated 68020, kilocycles. 512 byte PFS3 blocks:

| build | flag | fmt | wr | rd | ls | create |
|---|---|---|---|---|---|---|
| `000` | `-m68000` | 295 | 892 | 174 | 285 | 186 |
| `020` | `-m68020-60` | 281 | 718 | 174 | 285 | 180 |
| `030` | `-m68030` | 279 | 716 | 171 | 287 | 176 |
| `040` | `-m68020-40` | 278 | 709 | 170 | 284 | 176 |
| `060` | `-m68060` | 282 | 726 | 175 | 288 | 181 |

4096 byte PFS3 blocks:

| build | flag | fmt | wr | rd | ls | create |
|---|---|---|---|---|---|---|
| `000` | `-m68000` | 321 | 417 | 208 | 325 | 283 |
| `020` | `-m68020-60` | 307 | 390 | 209 | 325 | 276 |
| `030` | `-m68030` | 305 | 383 | 205 | 327 | 272 |
| `040` | `-m68020-40` | 304 | 381 | 204 | 324 | 271 |
| `060` | `-m68060` | 308 | 392 | 209 | 327 | 277 |

The emulator charges a 68020, 68030 and 68040 alike for every instruction these builds contain, so the table compares instruction selection, not pipelines; an emulated 68040 gives the same numbers, checked. There is no 68060 core.

## 4096 against 512 byte PFS3 blocks

Set on the RDB partition, `rdbtool ... add name=DH0 bs=4096`, not amifuse's `--block-size`, which is the device sector size. Larger blocks help the write only. Same build, same payload:

| operation | change |
|---|---|
| write | cheaper |
| read | more expensive |
| list a directory | more expensive |
| create a file | more expensive, the most |
| format | more expensive |

The 512 KiB payload occupies 128 blocks instead of 1024, so anode lookups, bitmap updates and cache slot churn happen an eighth as often. Everything else is metadata work whose unit is a block, and the block got eight times bigger. Creating a file is worst: it reads, modifies and writes back a full directory block.

## Binary sizes

Bytes.

| toolchain | 000 | 020 | 030 | 040 | 060 |
|---|---|---|---|---|---|
| gcc 6.5 | 66408 | - | 65036 | 65444 | 65060 |
| gcc 13.4 | 66104 | 64152 | 64096 | 64300 | 64152 |
| gcc 16.2 | 66544 | 64592 | 64772 | 64944 | 64592 |

## Observation

- **The 060 build against the 000 build, both on a 68020.** Helps the write. Format and file creation gain little, and for the gcc builds so does the directory listing; their reads are often slower.
- **The CPU matters more than the build.** Moving the same 000 build from a 68000 to a 68020 roughly halves the write. Switching that 68020 to the 060 build gains far less.
