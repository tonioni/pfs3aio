#!/bin/sh
# A volume formatted by a large file build and by a build without it, each
# opened by the other: what is refused and what has to keep working.
#
#   sh tests/m68k/largefile.sh [compare-dir]
#   TC=gcc16 TIER=060 sh tests/m68k/largefile.sh
#
# Needs amifuse and rdbtool on PATH, like variants.sh. The large tier is the
# plain one with an l, see LARGE_FILE_SIZE in tiers.mk.
#
# It does not write a file above 4G: that needs the 64 bit packets, which this
# handler only has for AmigaOS 4, and a disk image of that size.

set -u
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
OUT=${1:-$ROOT/compare}
TMP=$ROOT/tests/.tmp
FI=$ROOT/tests/fi.py
SIZE=${SIZE:-32Mi}
TC=${TC:-gcc13}
TIER=${TIER:-020}

. "$ROOT/tests/m68k/tiers.sh"
CPU=$(tier_mincpu "$TIER")
PLAIN=$OUT/$TC/$TIER/pfs3aio
LARGE=$OUT/$TC/${TIER}l/pfs3aio

command -v amifuse >/dev/null 2>&1 || { echo "ERROR: amifuse not on PATH"; exit 2; }
command -v rdbtool >/dev/null 2>&1 || { echo "ERROR: rdbtool not on PATH"; exit 2; }
[ -f "$PLAIN" ] || { echo "ERROR: no $PLAIN"; exit 2; }
[ -f "$LARGE" ] || { echo "ERROR: no $LARGE"; exit 2; }

mkdir -p "$TMP"
head -c 32768 /dev/urandom > "$TMP/a.bin"
head -c 20000 /dev/urandom > "$TMP/b.bin"

runfi() { FI_CPU=$CPU FI_CMD=none python3 "$FI" "$@"; }

format() {	# $1=driver $2=image
	rm -f "$2"
	rdbtool -f "$2" create size=$SIZE + init + add name=DH0 size=95% \
		dostype=0x50465303 >/dev/null 2>&1
	runfi format "$2" DH0 V --driver "$1" 2>&1 | grep -qi "format complete"
}

put() {	# $1=driver $2=image $3=name $4=source
	runfi write "$2" --file "$3" --in "$4" --driver "$1" >/dev/null 2>&1
}

same() {	# $1=driver $2=image $3=name $4=source: does the file read back equal?
	rm -f "$TMP/back.bin"
	runfi read "$2" --file "$3" --out "$TMP/back.bin" --driver "$1" >/dev/null 2>&1
	[ -f "$TMP/back.bin" ] && cmp -s "$4" "$TMP/back.bin"
}

# Disk type and large file flag of the root block: the first PFS\1 or PFS\2
# whose options are not zero, since the boot block carries PFS\1 and none.
rootinfo() {
	python3 - "$1" <<'PY'
import struct, sys
b = open(sys.argv[1], "rb").read()
for off in range(0, len(b) - 8, 4):
    t = b[off:off + 4]
    if t in (b"PFS\x01", b"PFS\x02"):
        opts = struct.unpack(">I", b[off + 4:off + 8])[0]
        if opts:
            print("PFS%d %s" % (t[3], "large" if opts & 2048 else "plain"))
            break
else:
    print("none")
PY
}

fail=0
check() {	# $1=expected $2=got $3=what
	if [ "$1" = "$2" ]; then r=ok; else r=FAIL; fail=$((fail + 1)); fi
	printf "%-62s %-9s %s\n" "$3" "$2" "$r"
}
yes_no() { if "$@"; then echo yes; else echo no; fi; }

printf "%-62s %-9s\n" "$TC $TIER and ${TIER}l on $CPU" result

LIMG=$TMP/large.hdf
PIMG=$TMP/plain.hdf

# a volume the large build formats
check yes "$(yes_no format "$LARGE" "$LIMG")" "large build formats"
check "PFS2 large" "$(rootinfo "$LIMG")" "  root block disk type and large file flag"
check yes "$(yes_no put "$LARGE" "$LIMG" F "$TMP/a.bin")" "large build writes a file"
check yes "$(yes_no same "$LARGE" "$LIMG" F "$TMP/a.bin")" "large build reads it back"
check no "$(yes_no same "$PLAIN" "$LIMG" F "$TMP/a.bin")" "build without large file support reads that volume"

# a volume the build without large file support formats
check yes "$(yes_no format "$PLAIN" "$PIMG")" "build without large file support formats"
check "PFS1 plain" "$(rootinfo "$PIMG")" "  root block disk type and large file flag"
check yes "$(yes_no put "$PLAIN" "$PIMG" F "$TMP/a.bin")" "it writes a file"
check yes "$(yes_no same "$LARGE" "$PIMG" F "$TMP/a.bin")" "large build reads that file"
check yes "$(yes_no put "$LARGE" "$PIMG" G "$TMP/b.bin")" "large build writes another file to it"
check yes "$(yes_no same "$PLAIN" "$PIMG" G "$TMP/b.bin")" "build without large file support reads that file"
check yes "$(yes_no same "$PLAIN" "$PIMG" F "$TMP/a.bin")" "and the first one"

rm -f "$LIMG" "$PIMG" "$TMP/back.bin" "$TMP/a.bin" "$TMP/b.bin"
echo "== largefile: $fail failed =="
[ "$fail" -eq 0 ]
