---
name: memory-leak-test
description: Valgrind-based memory leak validation for PP compiler. One script builds pplang/unittest/pp_std, runs the unit tests under valgrind, and validates compilation and execution of every demo (plain and -g) under valgrind.
license: MIT
compatibility: Requires valgrind, make and a native toolchain; run inside the pp repository.
metadata:
  author: pi
---

# Memory Leak Test Skill

Reproducible check that the PP compiler, its unit tests and all demos are leak-free under Valgrind.

## Quick start

```bash
.pi/skills/memory-leak-test/run_valgrind_validation.sh          # root inferred from the script location
.pi/skills/memory-leak-test/run_valgrind_validation.sh /path/pp # or pass the root explicitly
```

Exit code is `0` only when everything is clean. Logs:

* `$LOGDIR/pp_valgrind_summary.log` (default `LOGDIR=/tmp`) — per-step PASS/FAIL summary
* `$LOGDIR/pp_valgrind_unittest.log`
* `$LOGDIR/pp_valgrind_<demo>_<plain|debug>_compile.log` and `..._run.log`

On failure, the summary names the failing step and the log file that holds the full Valgrind output.

## What the script does

1. **Fresh build**: `make clean`, `make pplang`, `make unittest`, and `make -C lib/pp_std`.
   The stdlib rebuild matters: root `make clean` leaves `lib/pp_std/bin/pp_std.a` alone, and a stale archive
   fails demo linking with undefined `pp_std_*` symbols.
2. **Unit tests under valgrind**: `valgrind --leak-check=full --error-exitcode=1 ./bin/unittest`.
   Requires 0 definitely/indirectly lost and 0 valgrind errors, and all tests passing.
3. **Demos, discovered dynamically** (`find demo -maxdepth 1 -mindepth 1 -type d`) — no hardcoded list. For each
   demo, twice (plain and `-g`):
   * `make clean`, then `make PPLANG="valgrind --leak-check=full --error-exitcode=1 <root>/bin/pplang [-g]"`.
     Wrapping `PPLANG` reuses each demo Makefile's own sources, `.ppi` flags and stdlib archive, so nothing is
     duplicated here. Demo Makefiles also accept `PPCFLAGSEXTRA` for extra pplang flags (e.g. `-O2`), which the
     script does not need because `-g` is passed directly to `pplang`.
   * Locate the produced binary with `find <demo> -maxdepth 1 -type f -perm -u+x` (demos contain no other
     executables) and run it under valgrind.
   * Require `All heap blocks were freed -- no leaks are possible` in **both** the compile and run logs.
   * Treat a missing binary as a failure. Do not trust `make`'s exit status alone: `bin/pplang` used to exit 0
     when the system linker failed, so artifact presence is the reliable signal (the driver now propagates link
     status, but keep artifact checks as a guard).

## Success criteria

* `bin/unittest` passes under Valgrind with 0 definitely/indirectly/possibly lost and 0 errors.
* Every demo compiles under Valgrind with no leaks.
* Every demo binary runs under Valgrind with no leaks.
* The same holds for the `-g` build of every demo.

## Failure investigation

* Read `$LOGDIR/pp_valgrind_summary.log` to find the failing demo/mode/step.
* Then open the matching `..._compile.log` or `..._run.log`; it has the full Valgrind output with the leak stacks.
* Reproduce by hand (below), keeping the artifact instead of deleting it.

## Manual fallback

If a step fails, reproduce it by hand:

```bash
cd demo/<name>
make clean
make PPLANG="valgrind --leak-check=full --error-exitcode=1 ../../bin/pplang -g"
valgrind --leak-check=full ./<demo_binary>
```

Check the tail of the valgrind output for:

```
in use at exit: 0 bytes in 0 blocks
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

`1.5 KB` still reachable from gtest/LLVM statics is expected for `bin/unittest`; only
`definitely/indirectly/possibly lost` must be zero.

## Notes

* Demos may grow or be renamed; the script never hardcodes names, binaries or source lists.
* Per-step timeouts (600 s compile, 300 s run, 1800 s unit tests) keep CI from hanging.
* `pp_std_input_char` / `pp_std_abort` undefined at link time almost always means a stale
  `lib/pp_std/bin/pp_std.a`; rebuild it with `make -C lib/pp_std`.
* Record the outcome in the notebook page `valgrind-validation`.
