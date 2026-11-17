#!/usr/bin/env bash
set -euo pipefail

ROOT="/home/pbjk/Documents/Lab/pp"
SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FAIL_DIR="/tmp/valgrind_fail_$$"

echo "=== PP Memory Leak Validation ==="
echo "Root: $ROOT"
echo "Failures will be saved to: $FAIL_DIR"
mkdir -p "$FAIL_DIR"

cd "$ROOT"

# 1. Fresh build
echo ""
echo "=== [1/4] Fresh build ==="
make clean > /dev/null 2>&1
make -j4 > /dev/null 2>&1
make bin/unittest > /dev/null 2>&1
echo "Build OK"

# 2. Unit tests under Valgrind
echo ""
echo "=== [2/4] Unit tests under Valgrind ==="
if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/unittest_valgrind.log" bin/unittest 2>&1 | tee "$FAIL_DIR/unittest_out.log"; then
    echo "FAIL: Unit tests under Valgrind"
    echo "See: $FAIL_DIR/unittest_valgrind.log"
    cat "$FAIL_DIR/unittest_valgrind.log"
    exit 1
fi
echo "Unit tests: PASS"

# 3. Discover demos
echo ""
echo "=== [3/4] Discover demos ==="
DEMOS=()
while IFS= read -r -d '' d; do
    DEMOS+=("$d")
done < <(find demo -maxdepth 1 -mindepth 1 -type d -print0 | sort -z)
echo "Found ${#DEMOS[@]} demos: ${DEMOS[*]}"

# 4. Test each demo using its Makefile
RESULTS=()
for d in "${DEMOS[@]}"; do
    name=$(basename "$d")
    echo ""
    echo "--- Demo: $name ---"
    
    cd "$ROOT/$d"
    
    # Read TARGET from Makefile
    TARGET=$(grep '^TARGET :=' Makefile | head -1 | sed 's/^TARGET := *//')
    if [[ -z "$TARGET" ]]; then
        TARGET="${name}_demo"
    fi
    
    # Compile without -g
    echo "  Compiling (no debug)..."
    make clean > /dev/null 2>&1
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_compile_nog.log" \
        make 2>&1 | tee "$FAIL_DIR/${name}_compile_nog_out.log"; then
        echo "  FAIL: $name compile (no -g)"
        RESULTS+=("$name:compile_nog:FAIL")
        continue
    fi
    echo "  Compile OK"
    
    # Run
    echo "  Running..."
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_run_nog.log" \
        ./"${TARGET}" 2>&1 | tee "$FAIL_DIR/${name}_run_nog_out.log"; then
        echo "  FAIL: $name run (no -g)"
        RESULTS+=("$name:run_nog:FAIL")
        continue
    fi
    echo "  Run OK"
    
    # Compile with -g
    echo "  Compiling (with -g)..."
    make clean > /dev/null 2>&1
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_compile_g.log" \
        make PPCFLAGSEXTRA="-g" 2>&1 | tee "$FAIL_DIR/${name}_compile_g_out.log"; then
        echo "  FAIL: $name compile (-g)"
        RESULTS+=("$name:compile_g:FAIL")
        continue
    fi
    echo "  Compile OK"
    
    # Run with -g binary
    echo "  Running (-g)..."
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_run_g.log" \
        ./"${TARGET}" 2>&1 | tee "$FAIL_DIR/${name}_run_g_out.log"; then
        echo "  FAIL: $name run (-g)"
        RESULTS+=("$name:run_g:FAIL")
        continue
    fi
    echo "  Run OK"
    
    RESULTS+=("$name:PASS")
    cd "$ROOT"
done

# Summary
echo ""
echo "=== [4/4] Summary ==="
all_pass=true
for r in "${RESULTS[@]}"; do
    if [[ "$r" == *":FAIL" ]]; then
        echo "  $r"
        all_pass=false
    else
        echo "  $r"
    fi
done

if $all_pass; then
    echo ""
    echo "✅ ALL CHECKS PASSED"
    rm -rf "$FAIL_DIR"
    exit 0
else
    echo ""
    echo "❌ SOME CHECKS FAILED"
    echo "Failure artifacts in: $FAIL_DIR"
    exit 1
fi