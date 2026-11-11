#!/bin/zsh
# Build a supermode PFS3 fixture: a sparse, large-enough partition that the
# driver formats with MODE_SUPERINDEX (supermode) enabled, which also forces
# the >4GB TD64 access path. Costs ~a few MB of real disk despite the size.
#
#   zsh make-supermode.sh [--driver PATH] [--size 8Gi] [--out fixtures/super.hdf]
#
# Requires: rdbtool (amitools) and amifuse on PATH. Supermode needs the
# partition to exceed MAXSMALLDISK (~5.24 GB with 512-byte blocks), so the
# default 8Gi image with a 95% partition (~7.6 GiB) clears it comfortably.
set -u
SCRIPT_DIR=${0:A:h}
REPO=${SCRIPT_DIR:h}
. $SCRIPT_DIR/builddir.sh
DRV=$DEFAULT_DRIVER
SIZE=8Gi
OUT=$SCRIPT_DIR/fixtures/super.hdf
while [ $# -gt 0 ]; do
  case $1 in
    --driver) DRV=$2; shift 2;;
    --size)   SIZE=$2; shift 2;;
    --out)    OUT=$2; shift 2;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done
command -v rdbtool >/dev/null || { echo "rdbtool (amitools) not on PATH"; exit 2; }
command -v amifuse >/dev/null || { echo "amifuse not on PATH"; exit 2; }
[ -x $DRV ] || { echo "driver not found: $DRV"; exit 2; }
ensure_builddir
mkdir -p ${OUT:h}

echo "creating sparse image $OUT ($SIZE) ..."
rm -f $OUT
# never leave a half-built image behind: run-all.sh reuses an existing fixture as-is
built=0
trap '[ $built = 1 ] || rm -f $OUT' EXIT
# sparse allocation via truncate-to-size; same size syntax as rdbtool (8Gi, 512Mi, ...)
python3 - "$SIZE" "$OUT" <<'PY' || { echo "cannot allocate an image of size '$SIZE'"; exit 2; }
import sys
from amitools.util.ByteSize import parse_byte_size_str
size = parse_byte_size_str(sys.argv[1])
if not size:
    sys.exit(1)
with open(sys.argv[2], "wb") as f:
    f.truncate(size)
PY
rdbtool -f $OUT create size=$SIZE + init + add name=DH0 size=95% dostype=0x50465303 >/dev/null || {
  echo "rdbtool failed"; exit 1; }

echo "formatting DH0 with $DRV ..."
FI=$SCRIPT_DIR/fi.py
FI_CMD=none python3 $FI format $OUT DH0 SUPER --driver $DRV 2>&1 | grep -qi "Format complete" || {
  echo "format failed"; exit 1; }

# verify supermode bit (MODE_SUPERINDEX=128) in the rootblock
python3 - "$OUT" <<'PY'
import sys, struct
f = open(sys.argv[1], "rb")
# DH0 first sector = LowCyl(1) * cyl_blks(512) = 512; rootblock at partition sector 2
f.seek((512 + 2) * 512)
disktype, options = struct.unpack(">II", f.read(8))
sup = bool(options & 128)
print("rootblock disktype=0x%08x options=0x%x  MODE_SUPERINDEX=%s" % (disktype, options, sup))
sys.exit(0 if sup else 1)
PY
[ $? -eq 0 ] && echo "OK: $OUT is a supermode fixture" || { echo "ERROR: not supermode"; exit 1; }
built=1
du -k $OUT | awk '{print "  actual disk usage:", $1, "KB"}'
