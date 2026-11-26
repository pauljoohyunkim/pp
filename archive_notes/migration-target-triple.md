# Migration: `--target <triple>` (thin slice)

Status: **done — thin slice (compile/emit only)**

## Goal

Let `pplang` cross-*compile* to a non-native target for the self-contained modes:

- `--target <triple> -c file.pp`  → object file for that target
- `--target <triple> -S file.pp`  → assembly for that target
- `--target <triple> --emit-llvm file.pp` → LLVM IR for that target

Code generation is already fully target-driven after the usize/isize migration and `c48fd01`:
the module triple and data layout come from the `LLVMTargetMachineRef`, and every pointer/size/DI
width is derived from that layout. So this slice only needs driver + target-machine plumbing.

## Non-goals (this slice)

- **Cross-linking / running.** `pp_linker_offload` shells out to host `cc` with no target flags and
  links a host-built `pp_std.a`. Cross-linking needs `cc -m32`/a cross toolchain, a 32-bit `pp_std.a`,
  and a 32-bit runtime. Out of scope here; `--target` with a link step is rejected with a clear message
  unless the triple equals the host default.
- Per-target `pp_std` builds, sysroot discovery.

## Current vs desired

| Aspect | Current | After |
|---|---|---|
| Target triple | always host default (`LLVMGetDefaultTargetTriple`) | `--target <triple>` optional; default = host |
| LLVM target init | `LLVMInitializeNativeTarget` only | `LLVMInitializeAll*` (portable cross backends) |
| Target machine API | `..._create_default(module, out_tm)` | add `..._create(module, triple, out_tm)`; default delegates |
| Emit `-c/-S/--emit-llvm` | host only | any registered target |
| Link | host `cc` | unchanged; refused for non-native `--target` |

## Step-by-step plan

### Step 1 — Target-machine API  `(done)`
- [x] `include/pp_code_generator.h`: declare
  `int pp_code_generator_target_machine_create(LLVMModuleRef module, const char* triple, LLVMTargetMachineRef* out_tm);`
- [x] `src/pp_code_generator.c`: implement it:
  - `triple ? triple : LLVMGetDefaultTargetTriple()`
  - `LLVMInitializeAllTargetInfos/Targets/TargetMCs/AsmPrinters/AsmParsers` (idempotent)
  - `LLVMGetTargetFromTriple`, `LLVMCreateTargetMachine`, `LLVMSetTarget(module, triple)` when module != NULL
  - dispose the triple string only if we created it
- [x] Make `pp_code_generator_target_machine_create_default(module, out_tm)` delegate to it with `triple = NULL`.

### Step 2 — Driver `--target`  `(done)`
- [x] `src/pplang.c`: add `--target` long option (`required_argument`), store `const char* target_triple`.
- [x] Use `pp_code_generator_target_machine_create(tmp_mod, target_triple, &tm)`.
- [x] If a link step is requested (`!(to_object || to_assembly || emit_ir)`) and `--target` was given,
  error out unconditionally: automatic linking is disabled when `--target` is specified; use
  `-c`/`-S`/`--emit-llvm`, or drop `--target` to link natively. (Simpler than a triple-equivalence
  check and honest about the host-only link path.)
- [x] Update `print_help` with `--target triple`: triple form, example triples, the arch values
  verified to emit in this build, and a note that other LLVM backends can fail in codegen.
- [x] `pp_code_generator_target_is_supported(triple, &backend)`: static allow-list of LLVM **backend
  names** (LLVMGetTargetName), so `-c`/`-S` refuse unlowerable backends (sparcv9, nvptx*, amdgpu,
  xcore, ...) up front. `--emit-llvm` never runs instruction selection, so it only requires a
  resolvable triple. `pplang` installs a minimal non-returning LLVM fatal-error handler (`_Exit(1)`)
  as a backstop; no setjmp/longjmp.

### Step 3 — Test  `(done)`
- [x] `tests/codegentest.cpp`: create an `i386-unknown-linux-gnu` target machine, run codegen on a
  list/string + `usize` program, assert `%pp.list = type { ptr, i32, i32, i32 }`, `%pp.string = type { ptr, i32 }`,
  `usize` stored as `i32`, and `LLVMVerifyModule` passes. `GTEST_SKIP` if the X86 target is unavailable.
- [x] Optionally verify `nasm`-free: object bytes start with ELF `e_machine = EM_386` (0x03). (Optional.)

## Risks

- `LLVMInitializeAllTargets` adds a little startup cost; harmless.
- Passing an unknown triple must produce a clear error (LLVM `err` message surfaced).
- Forgetting to set the module triple would produce host-target objects → the test asserts the triple
  and layout, catching this.
- Cross-linking is genuinely environment-dependent; disabling automatic linking for any `--target` avoids
  silently linking a foreign object against a host `cc`/`pp_std.a`, and avoids brittle triple string
  comparison. Diverges from gcc/clang `--target=<host>` linking on purpose: this project is a compiler,
  not a universal linker.

## Acceptance criteria

- [x] `pplang --emit-llvm --target i386-unknown-linux-gnu x.pp` yields i386 IR: `target triple` set,
  `p:32:32` data layout, `usize` → `i32`, `%pp.list = { ptr, i32, i32, i32 }`.
- [x] `pplang -c --target i386-unknown-linux-gnu x.pp -o x.o` produces an ELF object with `e_machine = 3`.
- [x] No `--target` (or `--target` equal to host) keeps current behavior (all 660 tests pass, demos build).
- [x] Any `--target` without `-c/-S/--emit-llvm` (even the host triple) errors out clearly.
- [x] New unit test passes; existing suite stays green.

## Next steps (not this slice)

- Opt-in cross-link: thread a target hint into `pp_linker_offload_multi` (`cc -m32` for i386, or a
  configured cross `cc`), build a 32-bit `pp_std.a`, document prerequisites.
- Multi-source `-c` (currently one source per `-c`/`-S`/`--emit-llvm` invocation).
