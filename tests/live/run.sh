#!/bin/zsh
# Live-source tests: each *_live.c pulls the REAL driver function(s) out of the
# checkout's source, compiles them with a small mock, and asserts CORRECT
# behaviour. So a test FAILS on a tree that still has the bug and PASSES once
# the fix is applied -- that is what makes it a proof the fix changed something,
# not just a demonstration.
#
#   zsh run.sh                       # test the source in this checkout (../..)
#   SRCROOT=/path/to/tree zsh run.sh # test some other checkout's source
#
# The test binaries and the extracted functions go to $TEST_BUILD_DIR/live (default
# <repo>/build/tests/live), never into tests/.
#
# Each *_live.c declares which function(s) to lift, e.g.:
#   /* LIVE @src volume.c @func CalculateBlockSize */
# and #includes the generated gen/<func>.c.
set -u
DIR=${0:A:h}
REPO=${DIR:h:h}
. $DIR/../builddir.zsh
SRCROOT=${SRCROOT:-$REPO}          # default: repo root (two levels up from tests/live)
CC=${CC:-cc}
ensure_builddir
OUT=$TEST_BUILD_DIR/live
cd $DIR
mkdir -p $OUT/gen
pass=0; fail=0

# extract a C function definition by name from a file (signature .. matching })
# definitions start at column zero in this tree. If a name is defined more than
# once (init.c has a DEBUGMODE stub of Initialize), the last definition wins.
extract() {  # $1=file $2=func
  awk -v fn="$2" '
    !ins && $0 ~ /^[A-Za-z_]/ && $0 ~ ("(^|[^A-Za-z0-9_])" fn "[[:space:]]*[(]") && $0 !~ /;/ { ins=1; buf=""; n=0; seen=0 }
    ins { buf = buf $0 "\n"; o=gsub(/{/,"{"); c=gsub(/}/,"}"); n+=o-c; if(n>0) seen=1; if(seen && n==0) { last=buf; ins=0 } }
    END { printf "%s", last }
  ' "$1"
}

for t in *_live.c; do
  [ -e $t ] || continue
  # gather @src/@func directives (a test may lift several funcs)
  # (a later @src switches the file the following @funcs are taken from)
  src=
  ok=1
  for tok in $(grep -oE '@(src|func) [^ ]+' $t | tr ' ' ':'); do
    case $tok in
      @src:*) src=${tok#@src:}; continue ;;
    esac
    fn=${tok#@func:}
    if [ ! -f $SRCROOT/$src ]; then echo "  MISS  $t: no $SRCROOT/$src"; ok=0; break; fi
    extract $SRCROOT/$src $fn > $OUT/gen/$fn.c
    [ -s $OUT/gen/$fn.c ] || { echo "  MISS  $t: could not extract $fn from $src"; ok=0; break; }
  done
  [ $ok = 1 ] || { fail=$((fail+1)); continue; }
  # -I$OUT: the tests #include "gen/<func>.c" from the build directory
  if ! $CC -w -I. -I$OUT -o $OUT/${t%.c} $t 2>$OUT/gen/${t%.c}.build.log; then
    echo "  BUILD $t (see $OUT/gen/${t%.c}.build.log)"; fail=$((fail+1)); continue
  fi
  if $OUT/${t%.c}; then pass=$((pass+1)); else fail=$((fail+1)); fi
done
echo "== live: $pass passed, $fail failed  (source: $SRCROOT) =="
exit $([ $fail -eq 0 ] && echo 0 || echo 1)
