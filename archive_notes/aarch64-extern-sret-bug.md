# AArch64 segfaults in `pp_get_argv` demos — missing `sret` at the C ABI boundary

Status: **diagnosed + independently verified**, fix not yet implemented.
Date: 2026-11-24. Base: `a87c2d4` (clean tree). Host used for verification: x86-64, LLVM/Clang 23.1.5.

## TL;DR

PP codegen declares C externs that return aggregates as `%type @fn(...)` (returned by
value in registers) and never emits the `sret` parameter attribute. Clang-compiled C
callees *always* use the indirect-return ABI (`sret`). This mismatches the caller and
callee:

- **x86-64 (SysV)**: hidden return pointer goes in `RDI` either way, so caller and
  callee accidentally agree. Bug is invisible.
- **AArch64 (AAPCS64)**: hidden return pointer goes in the dedicated `x8` register, and
  LLVM only uses `x8` when `sret` is present. Without it LLVM returns the aggregate in
  `x0..x7`. Callee writes via `x8`, caller reads `x0..x3` → garbage `data`/`len`/`cap`
  → segfault / `Aborted`.

Only one extern in `lib/pp_std` returns a >16-byte aggregate today:
`pp_get_argv -> [string]`. That is exactly the demos that crash.

## Symptoms (Termux / Android aarch64)

| Demo | Result on aarch64 |
| --- | --- |
| `demo/08_argv/argv_demo` | `Segmentation fault` (139) |
| `demo/09_aes/aes_demo` | `Segmentation fault` (139) |
| `demo/10_chip8/chip8_demo` | garbage usage line, then tagged-pointer `Aborted` |
| all non-argv demos | exit 0 |

`lib/pp_std/tests/pp/runtime_argv.pp` uses the same call and would fail the same way.

On x86-64 the same demos work (verified: `./argv_demo a b c` → `argc = 4`, correct argv).

## Facts established in the tree

- `pp.list` = `{ ptr, i64, i64, i64 }` = **32 bytes**
  (`src/pp_code_generator.c:934-940`) — matches `pp_list_t` in
  `lib/pp_std/include/pp_std_types.h`.
- `pp.string` = `{ ptr, i64 }` = **16 bytes** → returned in registers on both ABIs, so
  `pp_print_*`, `pp_fd_*`, `pp_file_*` are safe.
- **No `sret` anywhere**: `grep -rn sret src include lib` → nothing.
- `pp_code_generator_llvm_subroutine_type()` (`src/pp_code_generator.c:962`, built at
  `:1008`) constructs `LLVMFunctionType(return, params...)` with no ABI special-casing.
  Used by pre-declare `:1773`, definition `:2772`, normal call `:3180/:3280`, binary-op
  call `:3461/:3497`, unary-op call `:3833/:3868`.

### Emitted IR for `demo/08_argv` (`--emit-llvm`)

```llvm
%pp.list = type { ptr, i64, i64, i64 }
declare %pp.list @pp_get_argv(i32, ptr)          ; no sret
...
%func_call = call %pp.list @pp_get_argv(i32 %lval_to_rval, ptr %lval_to_rval1)
store %pp.list %func_call, ptr %args, align 8
```

C side (`lib/pp_std/src/pp_runtime.c:8`):

```c
pp_list_t pp_get_argv(int32_t argc, uint8_t** argv) { ... return arg_list; }
```

Clang's IR for that same C function (aarch64 target):

```llvm
define dso_local void @pp_get_argv(
    ptr dead_on_unwind noalias writable sret(%struct.pp_list_t) align 8 %0,
    i32 noundef %1, ptr noundef %2)
```

## Reproduction of the ABI mismatch (x86-64 host, LLVM 23.1.5)

Comparing clang's `sret` callee vs. PP-style unattributed IR lowered with `llc`:

| | Clang callee (`sret`) | PP caller (no `sret`) |
| --- | --- | --- |
| aarch64 | `mov x9, x8; str q0, [x9]` (writes via **x8**) | `bl fn; add x0, x1, x2` (reads **x0..x3**) ❌ |
| x86-64  | hidden ptr in **RDI** | `leaq 8(%rsp), %rdi` (hidden ptr in **RDI**) ✅ |

Minimal repro IR used:

```llvm
%S32 = type { i64, i64, i64, i64 }
declare %S32 @ret32(i32)
declare void @ret32_sret(ptr sret(%S32), i32)
```

Key finding: LLVM's AArch64 backend keys the indirect-return register solely off the
`sret` attribute. Clang always inserts it; hand-written/PP-emitted IR that omits it gets
register return. That is the whole bug.

## Blast radius

- Every extern C function returning a struct/list by value with ABI size **> 16 bytes**.
  In `lib/pp_std` today: only `pp_get_argv -> [string]`
  (`lib/pp_std/extern/pp_runtime.ppi:2`).
- `PP_AllocBytes` (`ptr[uint8]`), `PP_FD` (`int32`), `PP_File` / `PP_Term` (`ptr[void]`)
  are handles — unaffected.
- **User `extern func` declarations returning large structs are also affected.**
- PP-internal functions returning large structs are fine as long as declaration and all
  call sites use the same (currently register-based) `LLVMFunctionType` — they are
  self-consistent. Any global fix must update definitions and all call sites together.

## Fix options

### A. Proper `sret` support in codegen (recommended; correct everywhere)

1. Add a predicate, e.g.
   `static bool pp_code_generator_return_uses_sret(PP_CodeGenerator* cg, const PPType* type)`
   - true iff the lowered type is `LLVMStructTypeKind` **or** `LLVMArrayTypeKind`
     **and** `LLVMABISizeOfType(LLVMGetTargetMachineData(tm), ty) > 16`.
   - The magic `16` is the register-return limit for both SysV x86-64 and AAPCS64.
   - Do **not** trip for `void`, scalars, pointers, or the exactly-16-byte `pp.string`.
2. In `pp_code_generator_llvm_subroutine_type()` (`:962`/`:1008`): when true, build
   `void (ptr, params...)`. After `LLVMAddFunction`, attach
   `sret` via `LLVMCreateTypeAttribute(ctx, LLVMAttributeIndex, sret_ty)` +
   `LLVMAddAttributeAtIndex(fn, 1, attr)` (attribute indices are 1-based per param;
   index 1 == first parameter). Add the same attribute at each call site with
   `LLVMAddCallSiteAttribute(call, 1, attr)`.
3. Definition body (`pp_code_generator_generate_function_body`, param loop at `:1976`
   uses `LLVMGetParam(llvm_func, i)`): the sret pointer is param 0, so user params read
   `LLVMGetParam(llvm_func, i + 1)`. Every `return` path
   (`:2403` void check, `:2414` and `:2473` `LLVMBuildRet`) must store the value through
   the sret pointer and emit `ret void`, including the non-void fallthrough.
4. Call sites — each needs an entry alloca of the real return type passed as `args[0]`,
   then `LLVMBuildLoad2` from that alloca as the result:
   - normal call `:3280`
   - binary operator `:3497`
   - unary operator `:3868`

**Gotcha not in the original report:** once the LLVM return type is `void`, the two
sites that branch on `LLVMGetReturnType(...) == LLVMVoidTypeInContext` to choose the SSA
name (`:3281`, `:3498`) now see `void` and must not be treated as "no value" — update
them alongside the return-type change.

### B. Extern-only `sret` (blast-radius-limited)

Same change but applied only when the callee is an extern C decl
(`sym->is_callable_decl && !sym->is_definition`) in pre-declare `:1762`/`:1773` and in
`pp_code_generator_generate_function_call` `:3180`/`:3280`. PP-internal convention stays
register-based. Less risk, but user externs still hit the bug — not a permanent fix.

### C. Short-term workaround, no compiler change (fastest unblock for Termux)

Never return the 32-byte struct by value:

```pp
// lib/pp_std/extern/pp_runtime.ppi
extern func pp_get_argv :: (argc :: int32, argv :: ptr[ptr[uint8]], out_args :: [string]&) -> void;
```

```c
// lib/pp_std/include/pp_runtime.h + lib/pp_std/src/pp_runtime.c
void pp_get_argv(int32_t argc, uint8_t** argv, pp_list_t* out);
```

Update `demo/08_argv`, `demo/09_aes`, `demo/10_chip8`, `lib/pp_std/tests/pp/runtime_argv.pp`.

Recommendation: do **A** as the real fix (C-ABI interop is wrong everywhere the
attribute is missing); optionally ship **C** first to unblock. Avoid leaving B in place
permanently.

## Verification plan

1. (x86-64 first) rebuild; `demo/08_argv` + `runtime_argv.pp` still correct; run all
   demos + `make unittest`.
2. aarch64 (Termux): `make`, then
   `demo/08_argv/argv_demo a b c` → `argc = 4` + all args; `demo/09_aes` (`./aes_demo`
   and `selftest.sh`); `demo/10_chip8` (`./chip8_demo turnover77.ch8`).
3. Re-run non-argv demos to confirm no regression in PP-internal aggregate returns.
4. Cheap regression gate that needs no ARM host: add a `--emit-llvm` assertion that the
   declaration of an extern returning `[string]` / a >16-byte struct contains `sret`.

## Minor notes while in this area (not the crash)

- `lib/pp_std/include/pp_std_types.h`: stale comment `pp.list = { data_ptr, len, cap }`;
  the struct has a 4th field `elem_size` (offset 24). Fix comment.
- List drop already calls each element's drop (`src/pp_code_generator.c` drop path), so
  `pp_get_argv`'s per-element buffers are not leaked; the C comment about `free()` is
  just explaining the extra NUL byte.
- `--emit-llvm` writes `<basename>.ll` (e.g. `a.ll` for `main.pp`) rather than the `-o`
  path — confusing when reproducing; unrelated.
