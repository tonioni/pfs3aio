#!/bin/zsh
# Top-level entry point: the live-source proofs (no driver/emulator needed) and,
# if a built driver + amifuse are available, the black-box AmiFUSE suite.
#
#   zsh tests/run-all.sh [--driver PATH] [--stock PATH] [--quick] [--strict]
#
# --strict makes a skipped black-box layer (no amifuse / no driver) a failure,
# so "GREEN" cannot silently mean "only the live proofs ran".
set -u
DIR=${0:A:h}
REPO=${DIR:h}
. $DIR/builddir.zsh
DRV=""
STRICT=0
typeset -a STOCK QUICK
STOCK=(); QUICK=()
while [ $# -gt 0 ]; do
  case $1 in
    --driver) DRV=$2; shift 2;;
    --stock)  STOCK=(--stock $2); shift 2;;
    --quick)  QUICK=(--quick); shift;;
    --strict) STRICT=1; shift;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done
rc=0
blackbox="ran"

echo "===== live-source proofs (fail on broken tree, pass on fixed) ====="
zsh $DIR/live/run.sh || rc=1

# black-box layer is optional: needs amifuse + a built driver
[ -z "$DRV" ] && [ -x $DEFAULT_DRIVER ] && DRV=$DEFAULT_DRIVER
if command -v amifuse >/dev/null && [ -n "$DRV" ] && [ -x $DRV ]; then
  echo "\n===== black-box AmiFUSE suite ====="
  if [ ! -f $DIR/fixtures/super.hdf ]; then
    zsh $DIR/make-supermode.sh --driver $DRV || { echo "ERROR: could not build the supermode fixture"; rc=1; }
  fi
  zsh $DIR/run.sh --driver $DRV $STOCK $QUICK || rc=1
else
  echo "\n(skipping black-box suite: needs amifuse on PATH and --driver PATH to a built driver)"
  blackbox="SKIPPED"
  [ $STRICT = 1 ] && rc=1
fi

echo "\n===== overall: $([ $rc = 0 ] && echo GREEN || echo FAILURES)  (black-box suite: $blackbox) ====="
exit $rc
