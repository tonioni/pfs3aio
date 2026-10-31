# The tier table: everything that differs per build, and nowhere else.
#
# A build is named after the machine you install it on, which is what the
# directory means in dist/ and in INSTALL_DIR. Where else it runs is the
# MINCPU/MAXCPU pair below, not something the name carries.
#
# Read by makefile.gcc and GNUmakefile, and from /bin/sh by
# tests/m68k/tiers.sh. Keep the "TIER_<FIELD>_<tier> = value" shape, one line
# per value, and no quotes: tests/m68k/tiers.sh parses this file with sed.
#
# Why each flag and each gap: TOOLCHAINS.md, "Target CPU: 68060".

# What a bare make builds and a release holds, then what is built only when
# asked for: make EXTRA=1, or TIERS="030 040". The extras are not published.
RELEASE_TIERS = 000 020 080 000l 020l 080l
EXTRA_TIERS = 030 040 060 080a 030l 040l 060l
ALL_TIERS = $(RELEASE_TIERS) $(EXTRA_TIERS)
TIERS = $(RELEASE_TIERS) $(if $(EXTRA),$(EXTRA_TIERS))

# gcc -m flag. Only 080a takes the line-A group, which most cores alert
# 8000000A on; -mtune=68080 gives the rest of what the core adds.
TIER_CFLAGS_000 = -m68000
TIER_CFLAGS_020 = -m68020-60
TIER_CFLAGS_030 = -m68030
TIER_CFLAGS_040 = -m68020-40
TIER_CFLAGS_060 = -m68060
TIER_CFLAGS_080 = -m68060 -mtune=68080 -Wa,-m68080
TIER_CFLAGS_080a = -m68080
TIER_CFLAGS_000l = -m68000
TIER_CFLAGS_020l = -m68020-60
TIER_CFLAGS_030l = -m68030
TIER_CFLAGS_040l = -m68020-40
TIER_CFLAGS_060l = -m68060
TIER_CFLAGS_080l = -m68060 -mtune=68080 -Wa,-m68080

# Toolchains that cannot build a given build, space separated.
#   gcc6: its -m68020-60 is not 68060 safe, unlike 13.4 and later
#   080:  only gcc 6.5 has -m68080 and -mtune=68080
TIER_SKIP_020 = gcc6
TIER_SKIP_080 = gcc13 gcc16
TIER_SKIP_080a = gcc13 gcc16
TIER_SKIP_020l = gcc6
TIER_SKIP_080l = gcc13 gcc16

# Lowest CPU the build runs on, so a harness knows which emulated CPU can run
# it.
TIER_MINCPU_000 = 68000
TIER_MINCPU_020 = 68020
TIER_MINCPU_030 = 68020
TIER_MINCPU_040 = 68020
TIER_MINCPU_060 = 68020
TIER_MINCPU_080 = 68080
TIER_MINCPU_080a = 68080
TIER_MINCPU_000l = 68000
TIER_MINCPU_020l = 68020
TIER_MINCPU_030l = 68020
TIER_MINCPU_040l = 68020
TIER_MINCPU_060l = 68020
TIER_MINCPU_080l = 68080

# Highest CPU the build runs on. Only those reaching 68060 are scanned by
# tests/check68060.sh.
TIER_MAXCPU_000 = 68060
TIER_MAXCPU_020 = 68060
TIER_MAXCPU_030 = 68040
TIER_MAXCPU_040 = 68040
TIER_MAXCPU_060 = 68060
TIER_MAXCPU_080 = 68080
TIER_MAXCPU_080a = 68080
TIER_MAXCPU_000l = 68060
TIER_MAXCPU_020l = 68060
TIER_MAXCPU_030l = 68040
TIER_MAXCPU_040l = 68040
TIER_MAXCPU_060l = 68060
TIER_MAXCPU_080l = 68080

# Bracketed after the "runs on" column of the release table, where MINCPU and
# MAXCPU alone cannot tell two builds for the same CPU apart. A note holds for
# binaries stamped with TIER_NOTE_SINCE_<tier> or later; the 080 build before
# that tag was built the way 080a is.
TIER_NOTE_080 = no Line-A
TIER_NOTE_SINCE_080 = 20260913-1
TIER_NOTE_080a = with Line-A
TIER_NOTE_080l = no Line-A

# Builds the author runs, <tier>:<toolchain>, shown green in the release table.
# Every other badge is grey: those are there to compare compilers and CPUs.
RELEASE_PICKS = 000:gcc16 020:gcc16

# LARGE_FILE_SIZE of the build: 0 is 32 bit file sizes, 2 supports file sizes
# above 4G and formats every new volume as one. A volume formatted that way
# is refused by the builds with 0. The tier name carries an l for 2.
TIER_LARGE_000 = 0
TIER_LARGE_020 = 0
TIER_LARGE_030 = 0
TIER_LARGE_040 = 0
TIER_LARGE_060 = 0
TIER_LARGE_080 = 0
TIER_LARGE_080a = 0
TIER_LARGE_000l = 2
TIER_LARGE_020l = 2
TIER_LARGE_030l = 2
TIER_LARGE_040l = 2
TIER_LARGE_060l = 2
TIER_LARGE_080l = 2

# Highest CPU Musashi implements, so the harnesses know which tiers they
# cannot run at all.
EMU_MAXCPU = 68040
