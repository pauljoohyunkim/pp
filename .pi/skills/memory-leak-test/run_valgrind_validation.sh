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
FAIL=0

: >"$LOG"
say() { echo "$@" | tee -a "$LOG"; }

say "root: $ROOT"

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
        # trusting make's exit status) catches driver exit-code bugs.
        bin=$(find "$d" -maxdepth 1 -type f -perm -u+x ! -name '*.pp' ! -name '*.ppi' | head -1)
        if [ -z "$bin" ]; then say "$name[$mode]: no binary found"; FAIL=1; continue; fi

        RLOG="$LOGDIR/pp_valgrind_${name}_${mode}_run.log"
        if timeout 300 $VG "$bin" </dev/null >"$RLOG" 2>&1; then
            say "$name[$mode]: run PASS"
        else
            say "$name[$mode]: run FAIL (rc=$?) - see $RLOG"
            FAIL=1
        fi

        for log in "$CLOG" "$RLOG"; do
            if ! grep -q "no leaks are possible" "$log"; then
                say "$name[$mode]: LEAK in $(basename "$log")"
                grep -E "definitely lost|indirectly lost" "$log" | tee -a "$LOG"
                FAIL=1
            fi
        done
    done

    make clean >/dev/null 2>&1
done

say "== Summary =="
if [ "$FAIL" -eq 0 ]; then say "VALGRIND VALIDATION: ALL CLEAN"; else say "VALGRIND VALIDATION: FAILURES PRESENT"; fi
exit "$FAIL"
