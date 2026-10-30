#!/bin/sh
# Functional check of built driver binaries under emulation: format a fresh
# PFS3 image, write a file, read it back, compare byte for byte, then delete
# the original of a hard link (hardlink.py).
#
#   sh tests/m68k/variants.sh [compare-dir]
#
# Needs amifuse and rdbtool on PATH. amitools 0.8.0 with machine68k 0.4.1 does
# not work together; installing amifuse pulls a combination that does.
#
# WHAT THIS DOES NOT COVER: Musashi, the CPU behind vamos, emulates up to
# 68040. It has 64-bit MULU.L/MULS.L, which a 68060 does not. A -m68020 build
# therefore passes everything here and still fails to boot on a 68060. That
# check is static and lives in TOOLCHAINS.md, "Target CPU: 68060". Do not read
# a pass here as 68060 clearance.
#
# Each build runs on the lowest CPU its tier declares in tiers.mk, so the cpu
# column says which core the row was tested on. The 040 tier is deliberately
# not a 68060 target and check-68060 skips it, which makes this its only
# automated evidence.

set -u
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
OUT=${1:-$ROOT/compare}
TMP=$ROOT/tests/.tmp
FI=$ROOT/tests/fi.py
SIZE=${SIZE:-32Mi}

. "$ROOT/tests/m68k/tiers.sh"
EMUCPU=${EMUCPU:-68020}		# what amifuse would have used on its own
MAXEMU=$(emu_maxcpu)		# Musashi stops here, see EMU_MAXCPU in tiers.mk

command -v amifuse >/dev/null 2>&1 || { echo "ERROR: amifuse not on PATH"; exit 2; }
command -v rdbtool >/dev/null 2>&1 || { echo "ERROR: rdbtool not on PATH"; exit 2; }

mkdir -p "$TMP"
head -c 32768 /dev/urandom > "$TMP/payload.bin"

printf "%-14s %-7s %-7s %-7s %-7s %-7s %-7s\n" build cpu format write read compare link
fail=0

for drv in "$OUT"/*/*/pfs3aio; do
	[ -f "$drv" ] || continue
	name=$(echo "$drv" | sed "s|^$OUT/||; s|/pfs3aio$||")
	tier=${name#*/}
	cpu=$(tier_emu_cpu "$tier" "$EMUCPU")
	if [ "$cpu" -gt "$MAXEMU" ]; then
		printf "%-14s %-7s %-7s %-7s %-7s %-7s %-7s\n" \
			"$name" "$cpu" skip skip skip skip "no core"
		continue
	fi
	img=$TMP/variant.hdf
	rm -f "$img"
	rdbtool -f "$img" create size=$SIZE + init + add name=DH0 size=95% \
		dostype=0x50465303 >/dev/null 2>&1

	f=no; w=no; r=no; c=no; l=no
	FI_CPU=$cpu FI_CMD=none python3 "$FI" format "$img" DH0 V --driver "$drv" 2>&1 \
		| grep -qi "format complete" && f=yes
	if [ $f = yes ]; then
		FI_CPU=$cpu FI_CMD=none python3 "$FI" write "$img" --file F --in "$TMP/payload.bin" \
			--driver "$drv" >/dev/null 2>&1 && w=yes
		rm -f "$TMP/back.bin"
		FI_CPU=$cpu FI_CMD=none python3 "$FI" read "$img" --file F --out "$TMP/back.bin" \
			--driver "$drv" >/dev/null 2>&1 && r=yes
		[ -f "$TMP/back.bin" ] && cmp -s "$TMP/payload.bin" "$TMP/back.bin" && c=yes
		python3 "$DIR/hardlink.py" --driver "$drv" --cpu "$cpu" >/dev/null 2>&1 && l=yes
	fi

	printf "%-14s %-7s %-7s %-7s %-7s %-7s %-7s\n" "$name" "$cpu" "$f" "$w" "$r" "$c" "$l"
	[ "$c" = yes ] && [ "$l" = yes ] || fail=$((fail + 1))
done

rm -f "$TMP/variant.hdf" "$TMP/back.bin" "$TMP/payload.bin"
echo "== variants: $fail failed =="
[ "$fail" -eq 0 ]
