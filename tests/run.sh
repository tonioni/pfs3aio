#!/bin/zsh
# pfs3aio behaviour + fix-validation tests under AmiFUSE.
#
#   zsh run.sh [--driver PATH] [--stock PATH] [--image PATH]... [--quick]
#
# Defaults: driver = <repo>/pfs3aio (the output of "make"), images = every *.hdf
# in tests/fixtures/. Run tests/make-supermode.sh first to create the
# supermode fixture. Each test runs on a throwaway (sparse-preserving) copy.
# Needs zsh, python3 with amifuse importable (activate its venv), cmp and awk.
#
# Every verdict is based on what the volume really contains, not on it mounting:
# after a fault or crash it must remount with the same directories and EITHER
# the original files and byte total (rolled back) OR one more file whose bytes
# read back identical to what was written (committed) - never anything else.
# Fault/crash tests first run the operation once without faults on a fresh copy
# to learn how many reads/writes it takes, place the faults relative to that, and
# FAIL if the fault/crash never fired (fi.py FI-SUMMARY faults=, FI-CRASH): a spec
# that never triggers proves nothing.
set -u
SCRIPT_DIR=${0:A:h}
REPO=${SCRIPT_DIR:h}
. $SCRIPT_DIR/builddir.sh
FI=$SCRIPT_DIR/fi.py
TMP=$TEST_BUILD_DIR/blackbox-tmp
FIXDIR=$SCRIPT_DIR/fixtures
DRV=$DEFAULT_DRIVER
STOCK=""
QUICK=0
typeset -a IMAGES
IMAGES=()

while [ $# -gt 0 ]; do
  case $1 in
    --driver) DRV=$2; shift 2;;
    --stock)  STOCK=$2; shift 2;;
    --image)  IMAGES+=$2; shift 2;;
    --quick)  QUICK=1; shift;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done
[ ${#IMAGES} -eq 0 ] && IMAGES=($FIXDIR/*.hdf(N))
[ ${#IMAGES} -eq 0 ] && { echo "no images (pass --image, or run make-supermode.sh)"; exit 2; }
command -v amifuse >/dev/null || { echo "amifuse not on PATH"; exit 2; }
python3 -c 'import amifuse' 2>/dev/null || { echo "python3 cannot import amifuse (activate its venv)"; exit 2; }
[ -x $DRV ] || { echo "driver not found/executable: $DRV"; exit 2; }
[ -z "$STOCK" ] || [ -x $STOCK ] || { echo "stock driver not found/executable: $STOCK"; exit 2; }

ensure_builddir
rm -rf $TMP; mkdir -p $TMP
trap 'rm -rf $TMP' EXIT
pass=0; fail=0; idx=0
STATE=""; WHY=""; PUT_RC=0; CW=0; CR=0
ok(){ print -r -- "    PASS  $1"; pass=$((pass+1)); return 0; }
no(){ print -r -- "    FAIL  $1"; fail=$((fail+1)); return 0; }
# sparse-preserving copy: clonefile on macOS (`cp -c`; on GNU cp -c means something
# else entirely), reflink/sparse on GNU cp, plain cp where neither is available
case $OSTYPE in
  darwin*) cpy(){ /bin/cp -c "$1" "$2" 2>/dev/null || cp "$1" "$2" || { print -u2 "copy failed: $1"; exit 2 } };;
  *)       cpy(){ cp --reflink=auto --sparse=always "$1" "$2" 2>/dev/null || cp "$1" "$2" || { print -u2 "copy failed: $1"; exit 2 } };;
esac

# "files dirs bytes" of the volume in image $1 (from `verify`), empty if it won't mount
vol(){ FI_CMD=none python3 $FI verify "$1" --driver $DRV 2>/dev/null | awk '
         /Directories:/ { d=$2 }  /Files:/ { f=$2 }
         /Total size:/ { gsub(",", "", $3); b=$3 }
         /Filesystem responsive: yes/ { r=1 }
         END { if (r && f != "") print f, d+0, b+0 }'; }
# put IMG NAME SRC [VAR=val ...]: write SRC into IMG as NAME, with the fi.py settings
# in the extra args. Output goes to $WD/put.out and $WD/put.err, the exit code to PUT_RC.
put(){ local img=$1 name=$2 src=$3; shift 3
  env FI_CMD=none "$@" python3 $FI write $img --file $name --in $src --driver $DRV >$WD/put.out 2>$WD/put.err
  PUT_RC=$?; }
# field $1 of the last FI-SUMMARY line of the last put (empty if the harness died before printing it)
sumf(){ sed -n "s/.*FI-SUMMARY.* $1=\([0-9][0-9]*\).*/\1/p" $WD/put.err | tail -1; }
# classify IMG NAME SRC: remount IMG and decide what an interrupted `put NAME SRC` left behind.
# STATE=rolledback|committed (return 0) or bad (return 1, WHY says what was found).
classify(){ local img=$1 name=$2 src=$3 v size
  STATE=bad; WHY=""
  v=($(vol $img))
  [ ${#v} -eq 3 ] || { WHY="volume does not remount"; return 1; }
  [ ${v[2]} = $BASE_D ] || { WHY="directory count changed (${v[2]}, was $BASE_D)"; return 1; }
  if [ ${v[1]} = $BASE_F ] && [ ${v[3]} = $BASE_B ]; then STATE=rolledback; return 0; fi
  size=$(wc -c < $src); size=$((size))
  if [ ${v[1]} = $((BASE_F+1)) ] && [ ${v[3]} = $((BASE_B+size)) ]; then
    rm -f $WD/rb
    FI_CMD=none python3 $FI read $img --file $name --out $WD/rb --driver $DRV >/dev/null 2>&1
    cmp -s $WD/rb $src && { STATE=committed; return 0; }
    WHY="$name was committed but does not read back identical"; return 1
  fi
  WHY="files=${v[1]} dirs=${v[2]} bytes=${v[3]}; expected files=$BASE_F bytes=$BASE_B (rolled back) or files=$((BASE_F+1)) bytes=$((BASE_B+size)) (committed)"
  return 1
}
# control NAME SRC: fault-free put on a fresh copy of $IMG. Sets CW/CR to the writes/reads the
# operation issues; fails (saying why) unless it committed in full, as the faulty runs are
# placed relative to these counts.
control(){ cpy $IMG $WD/ctl.hdf; put $WD/ctl.hdf $1 $2
  CW=$(sumf writes); CR=$(sumf reads)
  [ -n "$CW" ] && [ -n "$CR" ] && [ $CW -gt 0 ] \
    || { print "      control write of $1 gave no usable FI-SUMMARY: $(head -c 300 $WD/put.err | tr '\n' ' ')"; return 1; }
  classify $WD/ctl.hdf $1 $2 && [ $STATE = committed ] \
    || { print "      control write of $1 did not commit: ${WHY:-$STATE}"; return 1; }
}

mk(){ dd if=/dev/urandom of=$1 bs=$2 count=$3 2>/dev/null; }
ODDSZ=777; MULTISZ=$((1024*600))
mk $TMP/odd.bin   1    $ODDSZ   # sub-block -> read-modify-write tail
mk $TMP/multi.bin 1024 600      # 600 KB multi-block

for IMG in $IMAGES; do
  idx=$((idx+1)); WD=$TMP/img$idx          # per-image scratch dir: nothing is shared between images
  rm -rf $WD; mkdir -p $WD
  cpy $IMG $WD/base.hdf                     # verify writes at mount: never touch the fixture itself
  bv=($(vol $WD/base.hdf)); rm -f $WD/base.hdf
  print -r -- "== image: ${IMG:t}  (driver ${DRV:t}, baseline ${bv[1]:-UNMOUNTABLE} files)"
  [ ${#bv} -eq 3 ] || { no "image does not mount"; continue; }
  BASE_F=${bv[1]}; BASE_D=${bv[2]}; BASE_B=${bv[3]}

  # T1  functional round-trip; odd-size + multi-block, byte-exact, and the
  #     volume totals account for exactly the two new files
  cpy $IMG $WD/t1.hdf
  put $WD/t1.hdf ODD   $TMP/odd.bin;   rc1=$PUT_RC
  put $WD/t1.hdf MULTI $TMP/multi.bin; rc2=$PUT_RC
  rm -f $WD/o1 $WD/o2
  FI_CMD=none python3 $FI read  $WD/t1.hdf --file ODD   --out $WD/o1 --driver $DRV >/dev/null 2>&1
  FI_CMD=none python3 $FI read  $WD/t1.hdf --file MULTI --out $WD/o2 --driver $DRV >/dev/null 2>&1
  v=($(vol $WD/t1.hdf))
  if [ $rc1 = 0 ] && [ $rc2 = 0 ] && cmp -s $WD/o1 $TMP/odd.bin && cmp -s $WD/o2 $TMP/multi.bin \
     && [ "$v" = "$((BASE_F+2)) $BASE_D $((BASE_B+ODDSZ+MULTISZ))" ]; then
    ok "T1 write/read round-trip (odd + multiblock) byte-exact"
  else
    no "T1 round-trip (write rc=$rc1/$rc2, volume now '$v', expected '$((BASE_F+2)) $BASE_D $((BASE_B+ODDSZ+MULTISZ))')"
  fi
  if [ -n "$STOCK" ]; then
    rm -f $WD/o2s
    FI_CMD=none python3 $FI read $WD/t1.hdf --file MULTI --out $WD/o2s --driver $STOCK >/dev/null 2>&1
    cmp -s $WD/o2s $TMP/multi.bin && ok "T1 data reads back on STOCK driver (on-disk format compatible)" \
                                  || no "T1 stock cross-read"
  fi

  # T2  format then round-trip (on a copy; reformats the partition)
  cpy $IMG $WD/t2.hdf
  if FI_CMD=none python3 $FI format $WD/t2.hdf 0 TV --driver $DRV 2>&1 | grep -qi "Format complete"; then
    empty=$(vol $WD/t2.hdf)
    put $WD/t2.hdf X $TMP/multi.bin
    rm -f $WD/o3
    FI_CMD=none python3 $FI read $WD/t2.hdf --file X --out $WD/o3 --driver $DRV >/dev/null 2>&1
    v=$(vol $WD/t2.hdf)
    if [ "$empty" = "0 0 0" ] && [ $PUT_RC = 0 ] && cmp -s $WD/o3 $TMP/multi.bin && [ "$v" = "1 0 $MULTISZ" ]; then
      ok "T2 format + write + read round-trip"
    else
      no "T2 format round-trip (fresh volume '$empty', expected '0 0 0'; after write '$v', expected '1 0 $MULTISZ')"
    fi
  else
    no "T2 format did not complete"
  fi

  # T3  I/O-error resilience: inject read & write faults at many points of the
  #     odd-size write (cancelling and retrying, single and persistent). The faults are
  #     placed with FI_SKIP as a percentage of the reads/writes the fault-free control
  #     run took, and every spec must really fire. Never crash the emulator, always
  #     leave the volume remountable and either rolled back or fully committed.
  #     spec = "cmd count req lo hi skip%"  (req 1=retry, 0=cancel; count 0 = every transfer)
  t3=1
  if control T3FILE $TMP/odd.bin; then
    W3=$CW; R3=$CR
    typeset -a specs
    specs=("r 0 0 2 999999999 0"  "r 0 0 0 999999999 50" "r 1 0 0 999999999 100" "r 1 1 0 999999999 50"
           "w 0 0 2 800 0"        "w 0 0 0 999999999 50" "w 1 0 0 999999999 100"
           "w 1 1 0 999999999 0"  "w 3 1 0 999999999 0"  "w 1 1 0 999999999 50")
    [ $QUICK = 1 ] && specs=($specs[1] $specs[3] $specs[6] $specs[7])
    for spec in $specs; do
      set -- ${=spec}
      tot=$W3; [ $1 = r ] && tot=$R3
      skip=$((tot*$6/100)); [ $skip -ge $tot ] && skip=$((tot-1)); [ $skip -lt 0 ] && skip=0
      cpy $IMG $WD/t3.hdf
      put $WD/t3.hdf T3FILE $TMP/odd.bin FI_CMD=$1 FI_COUNT=$2 FI_REQ=$3 FI_LO=$4 FI_HI=$5 FI_SKIP=$skip
      f=$(sumf faults)
      if grep -q FI-EXCEPTION $WD/put.err; then t3=0; print "      emulator crash on fault [$spec]"
      elif [ -z "$f" ]; then t3=0; print "      no FI-SUMMARY after [$spec]: the harness died"
      elif [ $f -lt 1 ]; then
        t3=0; print "      fault never fired for [$spec] (skip=$skip of $tot, matched=$(sumf matched)): nothing was tested"
      fi
      classify $WD/t3.hdf T3FILE $TMP/odd.bin || { t3=0; print "      volume bad after [$spec] (faults=${f:-?}): $WHY"; }
    done
  else t3=0; fi
  [ $t3 = 1 ] && ok "T3 faults fired, no crash, volume rolled back or committed intact across read/write fault injection" \
              || no "T3 I/O-error resilience"

  # T4  crash-consistency: deterministic power cut just before the Nth write,
  #     swept across the whole write sequence of the fault-free control run
  #     (N=1..W, plus W+1 which is never reached and must commit). Every cut must
  #     really happen, and every remount must be clean: rolled back or fully
  #     committed, never anything in between. PFS3 commits a write atomically with
  #     one of its last writes (a root block write; a cleanup write may follow), so
  #     the cuts before that write are expected to roll back.
  t4=1
  if control T4FILE $TMP/multi.bin; then
    W4=$CW; rb=0; cm=""
    typeset -a pts; pts=()
    addpt(){ [ $1 -ge 1 ] && pts+=$1 }
    if [ $QUICK = 1 ]; then
      addpt 1; addpt $((W4/2)); addpt $((W4-1)); addpt $W4
    else
      st=$(( (W4+39)/40 ))      # every write for the usual ~20, at most ~40 points on big sequences
      for ((n=1; n<=W4; n+=st)); do addpt $n; done
      addpt $((W4-2)); addpt $((W4-1)); addpt $W4
    fi
    addpt $((W4+1))
    pts=(${(nu)pts})
    for n in $pts; do
      cpy $IMG $WD/t4.hdf
      put $WD/t4.hdf T4FILE $TMP/multi.bin FI_CRASHN=$n
      fired=0; grep -q "FI-CRASH after write #$n\$" $WD/put.err && fired=1
      if [ $n -le $W4 ] && [ $fired = 0 ]; then
        t4=0; print "      power cut before write #$n never happened (only $(sumf writes) writes issued): nothing was tested"; continue
      fi
      if classify $WD/t4.hdf T4FILE $TMP/multi.bin; then
        if [ $n -gt $W4 ] && [ $STATE != committed ]; then
          t4=0; print "      uninterrupted write (cut@$n > $W4 writes) did not commit: $STATE"
        fi
        [ $STATE = rolledback ] && rb=$((rb+1)) || cm="$cm $n"
      else
        t4=0; print "      power cut before write #$n: $WHY"
      fi
    done
    print "      ${#pts} runs, power cut before writes 1..$W4 plus one uninterrupted: $rb rolled back, committed at:${cm:- none}"
  else t4=0; fi
  [ $t4 = 1 ] && ok "T4 atomic commit holds at every deterministic power-cut point" \
              || no "T4 crash-consistency (deterministic)"

  # T5  external power loss: start a large write, kill -9 the amifuse process once
  #     the driver has issued a given share of the control run's writes (fi.py's
  #     FI_PROGRESS file counts them, so there is no sleep/timing guess), remount
  #     and require rolled back or fully committed. FI_FLUSH makes every write
  #     reach the image file, so the killed image is a faithful power cut. The
  #     kill must really land (SIGKILL exit status, no FI-SUMMARY printed), except
  #     at 100% where the writer may legitimately have finished first.
  #     Kills at 25-75% roll back, and so does anything short of the commit: the
  #     final burst of ~10 writes (commit + cleanup) takes well under a millisecond,
  #     so an external kill can only land before it (rolled back) or after it
  #     (committed, the 100% point). Cuts inside the burst are T4's job: it cuts
  #     before each individual write.
  #     The file is a quarter of the image (max 120 MB) so it fits and keeps writing
  #     long enough for the kills to land mid-write.
  if [ $QUICK = 0 ]; then
    t5=1
    bigmb=$(( $(wc -c < $IMG) / 1048576 / 4 )); [ $bigmb -gt 120 ] && bigmb=120; [ $bigmb -lt 1 ] && bigmb=1
    BIG=$TMP/big$bigmb.bin
    [ -f $BIG ] || mk $BIG 1048576 $bigmb
    if control T5FILE $BIG; then
      W5=$CW
      for q in 25 50 75 100; do
        thr=$((W5*q/100)); [ $thr -lt 1 ] && thr=1
        cpy $IMG $WD/t5.hdf
        rm -f $WD/prog
        FI_FLUSH=1 FI_PROGRESS=$WD/prog python3 $FI write $WD/t5.hdf --file T5FILE --in $BIG --driver $DRV \
          >$WD/k.out 2>$WD/k.err &
        pid=$!
        i=0
        while kill -0 $pid 2>/dev/null; do
          [ -f $WD/prog ] && [ $(( $(wc -c < $WD/prog) )) -ge $thr ] && break
          sleep 0.002; i=$((i+1)); [ $i -ge 10000 ] && break       # ~20+ s guard against a hung writer
        done
        kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null; wrc=$?
        nw=$([ -f $WD/prog ] && wc -c < $WD/prog || echo 0); nw=$((nw))
        if [ $wrc != 137 ] || grep -q FI-SUMMARY $WD/k.err; then
          if [ $q != 100 ]; then
            t5=0; print "      kill after $q% of $W5 writes missed (writer exited rc=$wrc after $nw writes): nothing was tested"; continue
          fi
          print "      (writer finished before the kill)"
        fi
        if classify $WD/t5.hdf T5FILE $BIG; then
          print "      kill -9 after write $nw/$W5: volume OK, $STATE"
        else
          t5=0; print "      kill -9 after write $nw/$W5: $WHY"
        fi
      done
    else t5=0; fi
    [ $t5 = 1 ] && ok "T5 volume consistent after external kill -9 during a large write" \
                || no "T5 external-kill power loss"
  fi

  # T6  directories, hard links, rename/move and relabel: dirops.py sends the
  #     AmigaDOS packets itself (the amifuse command line cannot), then closes
  #     the volume, mounts it again and compares the whole tree, every file's
  #     bytes and the volume name with its model. A second, independent count
  #     comes from `verify`: files/dirs/bytes must match the model's.
  cpy $IMG $WD/t6.hdf
  FI_CMD=none python3 $SCRIPT_DIR/dirops.py $WD/t6.hdf --driver $DRV >$WD/t6.out 2>$WD/t6.err
  t6rc=$?
  t6=($(sed -n 's/^DIROPS-TREE files=\([0-9]*\) dirs=\([0-9]*\) bytes=\([0-9]*\)$/\1 \2 \3/p' $WD/t6.out))
  t6v=($(vol $WD/t6.hdf))
  if [ $t6rc = 0 ] && [ ${#t6} -eq 3 ] && [ "${t6v[*]}" = "${t6[*]}" ]; then
    ok "T6 directory, link and label operations survive a remount (${t6[1]} files, ${t6[2]} dirs, ${t6[3]} bytes, as verify counts them)"
  else
    grep -E "FAIL|DIROPS-" $WD/t6.out | sed 's/^/      /'
    [ $t6rc = 0 ] || grep -E "DIROPS-EXCEPTION|Error" $WD/t6.err | head -5 | sed 's/^/      /'
    [ $t6rc != 0 ] || print "      verify counts '${t6v[*]}' but the model expects '${t6[*]}'"
    no "T6 directory, link and label operations (dirops.py rc=$t6rc)"
  fi
  rm -rf $WD
done

print -r -- "== TOTAL: $pass passed, $fail failed =="
exit $([ $fail -eq 0 ] && echo 0 || echo 1)
