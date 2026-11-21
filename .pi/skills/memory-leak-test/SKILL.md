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
* `$LOGDIR/pp_valgrind_<demo>_<plain|debug>_run.<pid>.log` — one per demo-binary invocation, for demos with a
  `selftest.sh`

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
     executables; `*.sh` is excluded so an executable `selftest.sh` cannot be mistaken for the binary) and run it
     under valgrind.
   * Require `All heap blocks were freed -- no leaks are possible` in **both** the compile and run logs.
   * Treat a missing binary as a failure. Do not trust `make`'s exit status alone: `bin/pplang` used to exit 0
     when the system linker failed, so artifact presence is the reliable signal (the driver now propagates link
     status, but keep artifact checks as a guard).

### Demos that need arguments: `selftest.sh`

A demo whose binary needs input files or arguments ships `selftest.sh` next to its `Makefile`. The script never
runs under valgrind itself (that would trace only the shell); the harness instead generates a wrapper that puts
valgrind around the demo binary and logs one file per invocation, then runs:

```bash
PP_DEMO_BIN=<wrapper> PP_SELFTEST_TMPDIR=<mktemp -d> sh <demo>/selftest.sh   # cwd = demo dir
```

Contract:

* `PP_DEMO_BIN` runs the demo binary under `valgrind --leak-check=full --error-exitcode=99`. The distinct exit
  code 99 keeps a valgrind-detected error from looking like the non-zero exit a negative test expects. The script
  itself asserts exact exit codes; the harness asserts cleanliness.
* `PP_SELFTEST_TMPDIR` is a scratch directory for keys, inputs and outputs, removed afterwards.
* Both variables are optional, so `selftest.sh` also runs standalone (falls back to the sibling binary and
  `mktemp -d`).
* The harness requires: the script exits 0, it invoked the binary at least once (a vacuous selftest fails), and
  every `pp_valgrind_<demo>_<mode>_run.<pid>.log` reports `no leaks are possible`.
* Without `selftest.sh`, the binary is run with no arguments and must exit 0.

`demo/09_aes/selftest.sh` is the reference: round trips with 16/24/32-byte keys (random keys, so key material is
never checked in), ciphertext size = plaintext + 12 (IV), a 17-byte key rejected with no output file, a
ciphertext shorter than the IV rejected, and a wrong-key decrypt producing garbage rather than the plaintext.

## Success criteria

* `bin/unittest` passes under Valgrind with 0 definitely/indirectly/possibly lost and 0 errors.
* Every demo compiles under Valgrind with no leaks.
* Every demo binary runs under Valgrind with no leaks.
* The same holds for the `-g` build of every demo.

## Failure investigation

* Read `$LOGDIR/pp_valgrind_summary.log` to find the failing demo/mode/step.
* Then open the matching `..._compile.log` or `..._run.log`; it has the full Valgrind output with the leak stacks.
  For a selftest demo, `..._run.log` holds the script's own output and each `..._run.<pid>.log` holds one binary
  invocation's Valgrind report (one per encrypt/decrypt/negative case).
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
* `pp_input_char` / `pp_abort` undefined at link time almost always means a stale
  `lib/pp_std/bin/pp_std.a`; rebuild it with `make -C lib/pp_std`.
* Record the outcome in the notebook page `valgrind-validation`.
