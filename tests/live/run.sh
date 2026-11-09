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
# Each *_live.c declares which function(s) to lift, e.g.:
#   /* LIVE @src volume.c @func CalculateBlockSize */
# and #includes the generated gen/<func>.c.
set -u
DIR=${0:A:h}
SRCROOT=${SRCROOT:-${DIR:h:h}}     # default: repo root (two levels up from tests/live)
CC=${CC:-cc}
cd $DIR
mkdir -p gen
pass=0; fail=0

# extract a C function definition by name from a file (signature .. matching })
extract() {  # $1=file $2=func; definitions start at column zero in this tree
  awk -v fn="$2" '
    !ins && $0 ~ /^[A-Za-z_]/ && $0 ~ (fn "[[:space:]]*[(]") && $0 !~ /;/ { ins=1 }
    ins { print; o=gsub(/{/,"{"); c=gsub(/}/,"}"); n+=o-c; if(n>0) seen=1; if(seen && n==0) exit }
  ' "$1"
}

for t in *_live.c; do
  [ -e $t ] || continue
  # gather @src/@func directives (a test may lift several funcs)
  src=$(/usr/bin/grep -oE '@src [^ ]+' $t | awk '{print $2}' | head -1)
  ok=1
  for fn in $(/usr/bin/grep -oE '@func [A-Za-z_][A-Za-z0-9_]*' $t | awk '{print $2}'); do
    if [ ! -f $SRCROOT/$src ]; then echo "  MISS  $t: no $SRCROOT/$src"; ok=0; break; fi
    extract $SRCROOT/$src $fn > gen/$fn.c
    [ -s gen/$fn.c ] || { echo "  MISS  $t: could not extract $fn from $src"; ok=0; break; }
  done
  [ $ok = 1 ] || { fail=$((fail+1)); continue; }
  if ! $CC -w -I. -o ${t%.c} $t 2>gen/${t%.c}.build.log; then
    echo "  BUILD $t (see gen/${t%.c}.build.log)"; fail=$((fail+1)); continue
  fi
  if ./${t%.c}; then pass=$((pass+1)); else fail=$((fail+1)); fi
done
echo "== live: $pass passed, $fail failed  (source: $SRCROOT) =="
exit $([ $fail -eq 0 ] && echo 0 || echo 1)
