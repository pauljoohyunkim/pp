#!/bin/bash
# Automated valgrind validation for the PP compiler.
#
# Usage:
#   .pi/skills/memory-leak-test/run_valgrind_validation.sh [ROOT]
#
# ROOT defaults to the repository root inferred from this script's location.
# Logs go to ${LOGDIR:-/tmp}/pp_valgrind_*.log.
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=${1:-$(cd "$SCRIPT_DIR/../../.." && pwd)}
LOGDIR=${LOGDIR:-/tmp}
LOG="$LOGDIR/pp_valgrind_summary.log"
VG="valgrind --leak-check=full --error-exitcode=1"
# Selftest runs use a distinct error exit code so a valgrind-detected error cannot be
# mistaken for the non-zero exit code a negative test expects from the demo itself.
VG_SELFTEST="valgrind --leak-check=full --error-exitcode=99"
FAIL=0

: >"$LOG"
say() { echo "$@" | tee -a "$LOG"; }

# Check for valgrind
if ! command -v valgrind >/dev/null 2>&1; then
    say "ERROR: valgrind not found in PATH. Install it first (e.g., 'sudo pacman -S valgrind' on Arch)."
    exit 1
fi

say "root: $ROOT (valgrind: $(valgrind --version | head -1))"

say "== Step 1: fresh build =="
cd "$ROOT" || exit 1
make clean >>"$LOG" 2>&1
if ! make -j"$(nproc)" pplang >>"$LOG" 2>&1; then say "BUILD FAILED (pplang)"; exit 1; fi
if ! make -j"$(nproc)" unittest >>"$LOG" 2>&1; then say "BUILD FAILED (unittest)"; exit 1; fi
# Rebuild the standard library: root "make clean" does not touch it, and a stale
# archive makes demo linking fail with undefined pp_std_* symbols.
if ! make -C lib/pp_std >>"$LOG" 2>&1; then say "BUILD FAILED (pp_std)"; exit 1; fi
say "build ok"

say "== Step 2: unit tests under valgrind =="
UNIT_LOG="$LOGDIR/pp_valgrind_unittest.log"
if timeout 1800 $VG ./bin/unittest >"$UNIT_LOG" 2>&1; then
    say "unit tests: PASS ($(grep -c '^\[       OK \]' "$UNIT_LOG") ok)"
else
    say "unit tests: FAIL (rc=$?)"
    FAIL=1
fi
grep -E "definitely lost|indirectly lost|possibly lost|in use at exit|ERROR SUMMARY|PASSED|FAILED" "$UNIT_LOG" |
    tail -8 | tee -a "$LOG"

say "== Steps 3-6: demos (compile + run, plain and -g) =="
for d in "$ROOT"/demo/*/; do
    d="${d%/}"
    name=$(basename "$d")
    cd "$d" || { say "$name: cd failed"; FAIL=1; continue; }

    for mode in plain debug; do
        make clean >/dev/null 2>&1
        if [ "$mode" = debug ]; then WRAP="$VG $ROOT/bin/pplang -g"; else WRAP="$VG $ROOT/bin/pplang"; fi

        CLOG="$LOGDIR/pp_valgrind_${name}_${mode}_compile.log"
        if timeout 600 make PPLANG="$WRAP" >"$CLOG" 2>&1; then
            say "$name[$mode]: compile PASS"
        else
            say "$name[$mode]: compile FAIL (rc=$?) - see $CLOG"
            FAIL=1
            continue
        fi

        # Locate the produced demo binary. Checking for the artifact (instead of
        # trusting make's exit status) catches driver exit-code bugs. selftest.sh is
        # excluded: it may be executable and would otherwise win the search.
        bin=$(find "$d" -maxdepth 1 -type f -perm -u+x ! -name '*.pp' ! -name '*.ppi' ! -name '*.sh' | head -1)
        if [ -z "$bin" ]; then say "$name[$mode]: no binary found"; FAIL=1; continue; fi

        RLOG="$LOGDIR/pp_valgrind_${name}_${mode}_run.log"
        LOGSTEM="$LOGDIR/pp_valgrind_${name}_${mode}_run"
        SELFTEST="$d/selftest.sh"
        rm -f "$LOGSTEM".*.log
        
        # chip8_demo requires a ROM file argument; running without args exits 1 (expected).
        # Skip direct run for this demo since no ROM is provided in CI.
        if [ "$name" = "10_chip8" ] && [ ! -f "$SELFTEST" ]; then
            say "$name[$mode]: run SKIP (requires ROM argument)"
            continue
        fi

        if [ -f "$SELFTEST" ]; then
            # A demo that needs input files or arguments ships a selftest.sh. Do not run
            # the script under valgrind: that traces only the shell. Instead hand the
            # script a wrapper that puts valgrind around each demo binary invocation,
            # logging one file per invocation so every call can be checked.
            VGW="$LOGDIR/pp_valgrind_${name}_${mode}_vgwrap.sh"
            TEMPDIR=$(mktemp -d)
            cat >"$VGW" <<WRAP
#!/bin/sh
exec $VG_SELFTEST --log-file="$LOGSTEM.%p.log" "$bin" "\$@"
WRAP
            chmod +x "$VGW"
            if PP_DEMO_BIN="$VGW" PP_SELFTEST_TMPDIR="$TEMPDIR" timeout 300 sh "$SELFTEST" </dev/null >"$RLOG" 2>&1; then
                say "$name[$mode]: selftest PASS"
            else
                say "$name[$mode]: selftest FAIL (rc=$?) - see $RLOG"
                FAIL=1
            fi
            rm -rf "$TEMPDIR" "$VGW"
            if ! ls "$LOGSTEM".*.log >/dev/null 2>&1; then
                say "$name[$mode]: selftest never ran the demo binary"
                FAIL=1
            fi
            RUNLOGS=$(ls "$LOGSTEM".*.log 2>/dev/null)
        else
            if timeout 300 $VG "$bin" </dev/null >"$RLOG" 2>&1; then
                say "$name[$mode]: run PASS"
            else
                say "$name[$mode]: run FAIL (rc=$?) - see $RLOG"
                FAIL=1
            fi
            RUNLOGS="$RLOG"
        fi

        for log in "$CLOG" $RUNLOGS; do
            # Check for actual leaks: definitely lost, indirectly lost, possibly lost
            # "still reachable" is NOT a leak - it's memory still allocated but reachable at exit
            if grep -qE "definitely lost: [1-9]|indirectly lost: [1-9]|possibly lost: [1-9]" "$log"; then
                say "$name[$mode]: LEAK in $(basename "$log")"
                grep -E "definitely lost|indirectly lost|possibly lost" "$log" | tee -a "$LOG"
                FAIL=1
            fi
        done
    done

    make clean >/dev/null 2>&1
done

say "== Summary =="
if [ "$FAIL" -eq 0 ]; then say "VALGRIND VALIDATION: ALL CLEAN"; else say "VALGRIND VALIDATION: FAILURES PRESENT"; fi
exit "$FAIL"
