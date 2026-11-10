# Sourced by the test scripts, after they set REPO (the repository root).
#
# Scratch space of the test scripts (live-test binaries, black-box temp files,
# python bytecode): <repo>/build/tests, unless TEST_BUILD_DIR is set. "make" keeps
# its own objects in build/normal and build/largefile and puts the driver in the
# repository root (<repo>/pfs3aio), which is what the scripts test by default.
# The path is made absolute here, so the scripts may cd freely.
TEST_BUILD_DIR=${${TEST_BUILD_DIR:-$REPO/build/tests}:A}
DEFAULT_DRIVER=$REPO/pfs3aio

# Create it with a .build-dir marker. An existing non-empty directory without the
# marker is not ours: refuse it rather than adopt it.
ensure_builddir() {
  [ -f $TEST_BUILD_DIR/.build-dir ] || {
    if [ -d $TEST_BUILD_DIR ] && [ -n "$(ls -A $TEST_BUILD_DIR)" ]; then
      print -u2 "$TEST_BUILD_DIR exists but is not a pfs3aio test directory (no .build-dir marker)"
      print -u2 "use another TEST_BUILD_DIR, or adopt it explicitly: touch $TEST_BUILD_DIR/.build-dir"
      exit 2
    fi
    mkdir -p $TEST_BUILD_DIR && touch $TEST_BUILD_DIR/.build-dir
  }
  # keep tests/ free of __pycache__ (only after the marker exists, so python
  # can never create the directory without it)
  export PYTHONPYCACHEPREFIX=$TEST_BUILD_DIR/pycache
}
