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

## Workflow

### 1. Fresh build
```bash
cd /home/pbjk/Documents/Lab/pp
make -j
make bin/unittest
```

### 2. Unit tests under Valgrind
```bash
valgrind --leak-check=full --error-exitcode=1 bin/unittest
```
Expected:
```
definitely lost: 0 bytes in 0 blocks
indirectly lost: 0 bytes in 0 blocks
```
All 359 tests must pass.

### 3. Discover demos
```bash
find demo -maxdepth 1 -mindepth 1 -type d
```
For each demo directory `d`:
* Build file is `Makefile` in that directory
* Compilation command is `make` inside the directory

### 4. Valgrind on compilation
For each demo `d`:
```bash
cd "$d"
valgrind --leak-check=full ../../bin/pplang --ppi ../../lib/pp_std/extern/pp_io.ppi main.pp [demo sources] ../../lib/pp_std/bin/pp_std.a -o <binary>
```
Check:
```
in use at exit: 0 bytes in 0 blocks
All heap blocks were freed -- no leaks are possible
```

### 5. Valgrind on execution
For each demo binary:
```bash
valgrind --leak-check=full ./<binary>
```
Check same heap summary – zero leaks.

## Automation notes

* Demos may grow. Do not hardcode names. Iterate over `demo/*` directories.
* PPI path is always `../../lib/pp_std/extern/pp_io.ppi`.
* Use `--error-exitcode=1` so CI fails on leaks.
* Record summary to notebook page `valgrind-validation`.

## Success criteria

* `bin/unittest` passes under Valgrind with 0 definitely lost.
* Every demo compiles under Valgrind with 0 leaks.
* Every demo binary runs under Valgrind with 0 leaks.
