#!/bin/sh
# Builds tests/m68k/bench_m68k.c with every toolchain and both tiers, runs each
# on every emulated CPU and prints the time.
#
#   sh tests/m68k/bench.sh
#   CPUS="68000 68020" TOOLCHAINS="gcc6 gcc16" sh tests/m68k/bench.sh
#   BREAKDOWN=1 sh tests/m68k/bench.sh          # per workload, one table per CPU
#
# Reported per cell: the cycles Musashi charges for the instructions it
# interpreted, and the binary size. Same work in every cell, and the figure is
# deterministic, so a single run per cell is enough.
#
# Not measured: the driver itself, and anything outside the CPU core. No memory
# wait states, no caches, no chip-RAM contention, which is exactly what makes
# stack traffic expensive on a real 68000. Read a column as a ranking.
#
# 68010 is missing because vamos does not accept it. 68030 needs the AttnFlags
# fix in tests/m68k/vamos_cpu.py; see that file. Musashi charges 68030 and
# 68040 from the same cycle table as the 68020, so those three columns come out
# identical by construction, not by measurement.

set -u
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
VAMOS=${VAMOS:-vamos}
# Same image family and pinned tags as GNUmakefile; see the note there on
# why gcc 6.5 is pinned to a dated tag.
GCC_IMAGE=${GCC_IMAGE:-docker.io/stefanreinauer/amiga-gcc}
GCC6_TAG=${GCC6_TAG:-gcc-v6.5.0b-20251218}
GCC13_TAG=${GCC13_TAG:-gcc-v13.4}
GCC16_TAG=${GCC16_TAG:-gcc-v16.2}
CPUS=${CPUS:-"68000 68020 68030 68040"}
TOOLCHAINS=${TOOLCHAINS:-"gcc6 gcc13 gcc16"}
. "$ROOT/tests/m68k/tiers.sh"
# the synthetic workload does not use LARGE_FILE_SIZE, so the large tiers repeat
# the others
TIER_LIST=${TIER_LIST:-$(for t in $(tiers); do [ "$(tier_large "$t")" = 0 ] && echo "$t"; done)}

command -v "$VAMOS" >/dev/null 2>&1 || { echo "ERROR: no vamos on PATH"; exit 2; }
GEN=$DIR/gen
mkdir -p "$GEN"
SRC=$DIR/bench_m68k.c

# Same flags the driver is built with, so the codegen being compared is the
# codegen that ships. See makefile.gcc.
GFLAGS="-Os -fbbb=+ -noixemul -fomit-frame-pointer -mregparm=3 \
	-fno-optimize-sibling-calls -msmall-code"

# The -m flag per tier comes from tiers.mk, so this cannot drift from what the
# driver is actually built with.
gflag_tier() { tier_gccflag "$1"; }

gccbuild() {	# $1=tag $2=tier $3=outfile-rel $4=extra flags
	podman run --rm --security-opt label=disable -v "$ROOT":/src -w /src \
		"$GCC_IMAGE:$1" /opt/amiga/bin/m68k-amigaos-gcc $GFLAGS \
		$4 $(gflag_tier "$2") -o "$3" tests/m68k/bench_m68k.c
}

build() {	# $1=toolchain $2=tier
	tc=$1; tier=$2
	out=$GEN/bench-$tc-$tier
	rel=tests/m68k/gen/bench-$tc-$tier
	rm -f "$out"
	case $tc in
	gcc6)  gccbuild "$GCC6_TAG" "$tier" "$rel" "" ;;
	gcc13) gccbuild "$GCC13_TAG" "$tier" "$rel" "" ;;
	gcc16) gccbuild "$GCC16_TAG" "$tier" "$rel" "-std=gnu17" ;;
	esac
	[ -f "$out" ]
}

cycles() {	# $1=binary $2=cpu [$3=workload] -> kilocycles, or "fail"
	out=$(CYCLES=1 python3 "$DIR"/vamos_cpu.py -q -C "$2" "$1" ${3:-} 2>&1 >/dev/null)
	rc=$(printf '%s' "$out" | sed -n 's/^RC \(.*\)$/\1/p')
	n=$(printf '%s' "$out" | sed -n 's/^CYCLES \(.*\)$/\1/p')
	# main returns sum % 100, so a build computing something else stands out
	if [ -z "${3:-}" ] && [ "$rc" != "$EXPECT" ]; then echo "fail(rc=$rc)"; return; fi
	[ -n "$n" ] || { echo fail; return; }
	echo $((n / 1000))
}

# names must match the order main() dispatches them in
WORKLOADS="1:blockmove 2:divide 3:namecmp 4:listwalk 5:bitmap"

# Reference result, from the toolchain the driver is released with.
build gcc6 000 || { echo "ERROR: reference build failed"; exit 2; }
EXPECT=$(CYCLES=1 python3 "$DIR"/vamos_cpu.py -q -C 68000 \
	"$GEN"/bench-gcc6-000 2>&1 >/dev/null | sed -n 's/^RC \(.*\)$/\1/p')
[ -n "$EXPECT" ] || { echo "ERROR: reference run produced no exit code"; exit 2; }

echo "kilocycles charged by Musashi; expected exit code $EXPECT everywhere"
echo
printf "%-14s %7s" build bytes
for c in $CPUS; do printf " %9s" "$c"; done
printf "\n"

for tc in $TOOLCHAINS; do
	for tier in $TIER_LIST; do
		tier_builds "$tier" "$tc" || continue
		if ! build "$tc" "$tier" >/dev/null 2>&1; then
			printf "%-14s %7s\n" "$tc/$tier" "build failed"
			continue
		fi
		bin=$GEN/bench-$tc-$tier
		printf "%-14s %7s" "$tc/$tier" "$(stat -c%s "$bin")"
		for cpu in $CPUS; do
			# a tier only runs on its minimum CPU and up, see tiers.mk
			if ! tier_runs_on "$tier" "$cpu"; then
				printf " %9s" -
				continue
			fi
			printf " %9s" "$(cycles "$bin" "$cpu")"
		done
		printf "\n"
	done
done

[ -n "${BREAKDOWN:-}" ] || exit 0

# 68030 and 68040 come out of the 68020 cycle table, so breaking those down
# reprints the same figures three times.
BCPUS=$(for c in $CPUS; do
		case $c in
		68030|68040) echo "$CPUS" | grep -q 68020 || echo "$c" ;;
		*) echo "$c" ;;
		esac
	done | head -2)

for cpu in $BCPUS; do
	echo
	echo "$cpu, kilocycles per workload"
	printf "%-14s" build
	for w in $WORKLOADS; do printf " %9s" "${w#*:}"; done
	printf "\n"
	for tc in $TOOLCHAINS; do
		for tier in $TIER_LIST; do
			tier_builds "$tier" "$tc" || continue
			bin=$GEN/bench-$tc-$tier
			[ -f "$bin" ] || continue
			tier_runs_on "$tier" "$cpu" || continue
			printf "%-14s" "$tc/$tier"
			for w in $WORKLOADS; do
				printf " %9s" "$(cycles "$bin" "$cpu" "${w%%:*}")"
			done
			printf "\n"
		done
	done
done
