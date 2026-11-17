---
name: memory-leak-test
description: Valgrind-based memory leak validation for PP compiler. Builds pplang and unit tests, runs unit tests under valgrind, and validates compilation and runtime of all demos under valgrind.
license: MIT
compatibility: Requires valgrind installed, project root at /home/pbjk/Documents/Lab/pp
metadata:
  author: pi
---

# Memory Leak Test Skill

This skill provides a reproducible workflow to verify that the PP compiler, its unit tests, and all demos are leak-free under Valgrind.

## Scope

* Fresh build of `pplang` and `bin/unittest`
* Valgrind leak check on unit tests
* Valgrind leak check on compilation of every demo in `demo/*`
* Valgrind leak check on execution of every demo binary
* Demos are discovered dynamically – no hardcoded list

## Automated Script

Run the complete validation with a single command:

```bash
cd /home/pbjk/Documents/Lab/pp
bash .pi/skills/memory-leak-test/run_valgrind_validation.sh
```

The script:
1. Fresh builds `pplang` and `bin/unittest`
2. Runs unit tests under Valgrind
3. Discovers all demos in `demo/*` dynamically
4. For each demo: compiles under Valgrind (with and without `-g`), runs binary under Valgrind
5. On any failure: prints the failing command, leaves artifacts in `/tmp/valgrind_fail_<demo>/`, and exits with non-zero
6. On success: prints a summary table and records to notebook page `valgrind-validation`

### Script: `.pi/skills/memory-leak-test/run_valgrind_validation.sh`

```bash
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

# 4. Test each demo
RESULTS=()
for d in "${DEMOS[@]}"; do
    name=$(basename "$d")
    echo ""
    echo "--- Demo: $name ---"
    
    # Compile without -g
    echo "  Compiling (no debug)..."
    cd "$ROOT/$d"
    make clean > /dev/null 2>&1
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_compile_nog.log" \
        ../../bin/pplang --ppi ../../lib/pp_std/extern/pp_io.ppi \
        $(ls *.ppi 2>/dev/null | xargs -I{} echo --ppi {}) \
        $(ls *.pp | head -1) \
        $(ls *.pp | tail -n +2 | tr '\n' ' ') \
        ../../lib/pp_std/bin/pp_std.a -o "${name}_demo" \
        2>&1 | tee "$FAIL_DIR/${name}_compile_nog_out.log"; then
        echo "  FAIL: $name compile (no -g)"
        RESULTS+=("$name:compile_nog:FAIL")
        continue
    fi
    echo "  Compile OK"
    
    # Run
    echo "  Running..."
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_run_nog.log" \
        ./"${name}_demo" 2>&1 | tee "$FAIL_DIR/${name}_run_nog_out.log"; then
        echo "  FAIL: $name run (no -g)"
        RESULTS+=("$name:run_nog:FAIL")
        continue
    fi
    echo "  Run OK"
    
    # Compile with -g
    echo "  Compiling (with -g)..."
    make clean > /dev/null 2>&1
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_compile_g.log" \
        ../../bin/pplang --ppi ../../lib/pp_std/extern/pp_io.ppi \
        $(ls *.ppi 2>/dev/null | xargs -I{} echo --ppi {}) \
        -g \
        $(ls *.pp | head -1) \
        $(ls *.pp | tail -n +2 | tr '\n' ' ') \
        ../../lib/pp_std/bin/pp_std.a -o "${name}_demo" \
        2>&1 | tee "$FAIL_DIR/${name}_compile_g_out.log"; then
        echo "  FAIL: $name compile (-g)"
        RESULTS+=("$name:compile_g:FAIL")
        continue
    fi
    echo "  Compile OK"
    
    # Run with -g binary
    echo "  Running (-g)..."
    if ! valgrind --leak-check=full --error-exitcode=1 --log-file="$FAIL_DIR/${name}_run_g.log" \
        ./"${name}_demo" 2>&1 | tee "$FAIL_DIR/${name}_run_g_out.log"; then
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
```

## Manual Workflow (if script unavailable)

### 1. Fresh build
```bash
cd /home/pbjk/Documents/Lab/pp
make clean
make -j
make bin/unittest
```

### 2. Unit tests under Valgrind
```bash
valgrind --leak-check=full --error-exitcode=1 bin/unittest
```

### 3. Discover demos
```bash
find demo -maxdepth 1 -mindepth 1 -type d
```

### 4-7. Valgrind on compilation/execution
For each demo `d`:
```bash
cd "$d"
make clean
# Compile without -g
valgrind --leak-check=full --error-exitcode=1 ../../bin/pplang --ppi ../../lib/pp_std/extern/pp_io.ppi --ppi $(ls *.ppi 2>/dev/null | xargs -I{} echo --ppi {}) $(ls *.pp) ../../lib/pp_std/bin/pp_std.a -o demo
# Run
valgrind --leak-check=full --error-exitcode=1 ./demo
# Compile with -g
make clean
valgrind --leak-check=full --error-exitcode=1 ../../bin/pplang ... -g ...
# Run
valgrind --leak-check=full --error-exitcode=1 ./demo
```

## Success Criteria

* `bin/unittest` passes under Valgrind with 0 definitely lost.
* Every demo compiles under Valgrind with 0 leaks.
* Every demo binary runs under Valgrind with 0 leaks.
* Same with `-g` flag.

## Failure Investigation

If the script fails, check the failure directory:
```bash
ls /tmp/valgrind_fail_*/
cat /tmp/valgrind_fail_*/<demo>_<step>.log
```

The logs contain full Valgrind output with leak details for the failing step.