# Migration: Freestanding (heap-free) compilation mode

Status: **plan** (not started)
Date: 2026-11-25

## Goal

Add a runtime mode to `pplang` — spell it `--ffreestanding` — that compiles PP programs without
depending on the C heap:

* list type `[T]` and its builtins are rejected,
* the `string` type and heap string literals are rejected,
* the generated module no longer pre-declares `malloc`, `realloc`, `free` (and `memcmp`, which only
  backs string equality),
* the generated object must not reference `malloc/calloc/realloc/free/memcpy/memset/memmove`
  (not even as an `llvm.memcpy` intrinsic lowered to a libcall),
* everything heap-free keeps working: primitives, structs, fixed arrays, pointers, references,
  `newtype`, `enum`/`flag`, callables, `extern`, **static** string literals (`@ "..."` → `[uint8:N]`),
  and **`handle`** with its user-bound drop function.

## Non-goals

* Building the compiler binary itself without libc. The compiler links LLVM and uses libc/LLVM heap
  internally; only the *emitted program* is constrained.
* A custom allocator, `alloc`/`free` intrinsics, or a bare-metal linker script. Programs produced in
  this mode still link through `cc` unless the user supplies their own runtime/flags.

## Current behavior

Everything is available unconditionally:

| Concern | Location |
|---|---|
| `string` keyword | `src/pp_lexer.c:474` → `PP_TT_STRING` |
| `string` type resolution | `src/pp_semantic_analyzer.c:1076` |
| `string` alias registration | `src/pp_semantic_analyzer.c:404` |
| list type `[T]` | `src/pp_semantic_analyzer.c:947-972` |
| list literal `[T; e...]` | `src/pp_semantic_analyzer.c:6799` |
| heap string literal `"..."` | `src/pp_semantic_analyzer.c:5980` |
| `list_push/pop/clear` | `src/pp_semantic_analyzer.c:4978-4986`, bodies at 6946/7005/7051 |
| runtime declarations | `src/pp_code_generator.c:1146-1167` (`malloc`, `realloc`, `free`, `memcmp`) |
| `llvm.memcpy` emission | `src/pp_code_generator.c:215-221` helper; calls at 501, 2518/2519, 3872/3873, 4516, 5160, 5214, 5300, 5333, 5811 |
| `abort` declaration (on demand) | `src/pp_code_generator.c:5841` (list_pop only) |
| CLI | `src/pplang.c:71-160`; options struct `include/pp_compiler_pipeline.h` |

## Desired behavior

| Feature | freestanding |
|---|---|
| primitives, bool, float, ints | yes |
| structs, fixed arrays `[T:N]`, pointers, references | yes |
| `newtype`, `enum`, `flag`, `typedef`, callables, `extern` | yes |
| `@ "..."` static string literal (no NUL-free heap copy) | yes |
| `handle` + bound `drop` function | yes (no allocator involved) |
| `copy_T` / `drop_T` per-type helpers | yes (trivial types → byte copy; handles → user drop) |
| `string` type, `"..."` heap literal, string `+`/`==`, string indexing | rejected |
| list type, list literal, `list_push/pop/clear`, list indexing/length | rejected |
| module declarations of `malloc/realloc/free/memcmp` | not emitted |
| `llvm.memcpy` / `memcpy`/`memset` libcalls | not emitted (inline byte copy instead) |

## Design decision: runtime flag, no scatter of `#ifdef`

`-DFREESTANDING` is a build-time macro and would require either a second binary or many conditional
compilations. A runtime boolean is easier to test and allows one `pplang` to serve both modes.

* CLI: `--ffreestanding` (long-only getopt entry, alongside `--ppi`/`--emit-llvm`).
* Thread a `bool freestanding` through:
  * `CompilerPipelineOptions` (`include/pp_compiler_pipeline.h`)
  * `PP_SemanticAnalyzer` (`include/pp_semantic_analyzer.h`) — set from options in
    `pp_compiler_pipeline_compile_source_core_opts` before `pp_semantic_analyzer_analyze`
  * `PP_CodeGenerator` (`include/pp_code_generator.h`) — set from `sa`/options in
    `pp_code_generator_create` or right after
* Optional build-time default later: a single `#ifdef FREESTANDING` in `pplang.c` that seeds
  `freestanding = true`. Zero ifdefs in feature logic.

Semantic analysis is the single source of truth for rejection. The pipeline already refuses to run
codegen when semantic errors exist (`src/pp_compiler_pipeline.c:203-217`), so codegen can stay mostly
unchanged plus defensive asserts.

---

## Step-by-step plan

Each step should compile and pass `make unittest` on its own. Add the in-code checklist comment block
to every function touched (see "In-code checklists" at the end).

### Step 1 — CLI and plumbing (ready for review: [x])

* [x] Add `#define PPLANG_OPT_FREESTANDING 1003` and `{"ffreestanding", no_argument, 0, PPLANG_OPT_FREESTANDING}`
      to `static struct option long_opts[]` in `src/pplang.c:74-77`.
* [x] Add `bool freestanding = false;` and a `case PPLANG_OPT_FREESTANDING` in `src/pplang.c`.
* [x] Add `bool freestanding;` to `CompilerPipelineOptions` (`include/pp_compiler_pipeline.h:29-40`).
* [x] Populate `.freestanding = freestanding` in the options initializer at `src/pplang.c:217`.
* [x] Set `sa->freestanding` in `pp_compiler_pipeline_compile_source_core_opts`
      (`src/pp_compiler_pipeline.c:200`) — either before analyze or via create param.
* [x] Add `bool freestanding;` to `PP_SemanticAnalyzer` and `PP_CodeGenerator`; initialize in their
      `*_create` functions.
* [x] Update `print_help()` (`src/pplang.c:479-490`) with `--ffreestanding`.
* [x] Manual check: `bin/pplang --ffreestanding /tmp/hello.pp -c -o /tmp/hello.o` still builds and
      behaves identically (no gates yet).

### Step 2 — Semantic gates (ready for review: [x])

Add the gates in `src/pp_semantic_analyzer.c`; use one shared diagnostic wording, e.g.
`"%s is not available in freestanding mode"`.

* [x] `pp_type_from_type_node`: reject `PP_AST_TYPE_KIND_STRING` (~1076).
* [x] `pp_type_from_type_node`: reject the `PP_AST_TypeListNode` branch (~947-972) before creating
      `PP_TK_List`. This covers `[T]`, `ptr[[T]]`, struct fields, params, returns, aliases.
* [x] `pp_semantic_analyzer_check_literal_expression`: reject `PP_TT_STRING_LITERAL` (~5980).
      **Do not** reject `PP_TT_STATIC_STRING_LITERAL` (`@ "..."`) — it resolves to `[uint8:N]`.
* [x] `pp_semantic_analyzer_check_collection_initializer_expression`: reject the `is_array == false`
      branch (~6799) before interning `PP_TK_List`. Array initializers stay.
* [x] `pp_semantic_analyzer_check_list_push/pop/clear` (or the dispatch at ~4978-4986): reject.
      Gating at dispatch is one place; gating inside each leaves room for future reuse — pick dispatch.
* [x] Keep the `string` alias registration at ~404 (the literal path and assertion depend on it).
      The type remains interned but is unreachable from user syntax.
* [x] Manual checks (expect clean semantic errors):
  * `let s :: string = "";`
  * `let s :: string = "hello";`
  * `let v :: [int32] = [int32;];`
  * `let v :: [int32 : 2u] = [int32 : 2u; 1, 2];` (must still pass)
  * `let s :: [uint8 : 6u] = @"hello";` (must still pass)
  * `list_push(v, 1);`

Verification run (2026-11-25):

* `make pplang` and `make unittest` pass; `bin/unittest` reports 628/628 passing.
* `--ffreestanding` rejects string type, heap string literal, list type, list literal and list builtins
  with dedicated diagnostics.
* `--ffreestanding` accepts a static string literal (`@"hello"` → `[uint8 : 6u]`), a fixed array, and a
  `handle`; the object has no heap undefined symbols (`nm -u` shows only the user's `extern` symbols).
* End-to-end handle smoke test: the C drop function runs exactly once (exit code 112, `dropped 7`).

### Step 3 — Codegen: stop declaring heap symbols (ready for review: [x])

* [x] Wrap the block at `src/pp_code_generator.c:1146-1167` in `if (!cg->freestanding) { ... }`.
      `abort` is added lazily in list_pop only, so nothing else is needed.
* [x] Defensive asserts (cheap, catches gate regressions): at the top of the `PP_TK_List` and
      `PP_TK_String` branches of `pp_code_generator_gen_copy_body` and `pp_code_generator_gen_drop_body`,
      and in `pp_code_generator_generate_rvalue_list_push/pop/clear`, add
      `assert(!cg->freestanding && "list/string reached codegen in freestanding mode");`.
* [x] Verify via IR: `bin/pplang --ffreestanding --emit-llvm /tmp/free.pp` must not contain
      `declare ... @malloc/@realloc/@free/@memcmp`. (Verified: zero matches.)

### Step 4 — `memcpy` elimination (ready for review: [x])

`LLVMBuildMemCpy` emits `llvm.memcpy`, whose backend lowering may call libc `memcpy` for large or
non-inlined copies. The user requirement is that this fallback is impossible.

Reachable `memcpy` sites in freestanding mode (lists/strings are unreachable):

* `src/pp_code_generator.c:5160` — `copy_T` for a trivially copyable type (whole struct/array/scalar).
* `src/pp_code_generator.c:5333` — `copy_T` for a fixed array of trivially copyable elements.

All other `emit_memcpy` calls are inside list/string code and become unreachable.

* [x] Add `static void pp_code_generator_emit_inline_byte_copy(PP_CodeGenerator* cg, LLVMValueRef dst_i8,
      LLVMValueRef src_i8, LLVMValueRef size, const char* name)` next to
      `pp_code_generator_emit_memcpy` (`src/pp_code_generator.c:215`). Emit an explicit loop:
      `i = 0; while (i < size) { b = load i8, src+i; store b, dst+i; i++ }` using
      `LLVMBuildGEP2`/`LLVMBuildLoad2`/`LLVMBuildStore`. Sizes at the two reachable sites are constants,
      so the loop bound is constant; no libcall.
* [x] Route through a dispatcher: `pp_code_generator_emit_copy_bytes(cg, dst, src, size)` chooses the
      loop when `cg->freestanding`, otherwise the existing `llvm.memcpy`.
* [x] Replace the calls at 5160 and 5333 with the dispatcher.
* [-] Optional hardening: also route the list-push copy at 5811 and the list/string copies through the
      dispatcher -> not needed: those paths are unreachable in freestanding mode and now assert.
* [x] Optimization policy: in freestanding mode, force `optimization_mode = '\0'` (no `default<O*>` pass
      pipeline) and warn if the user passed `-O1..-O3`. Rationale: `LoopIdiom`/`LoopUnroll` can
      synthesize `llvm.memcpy`/`llvm.memset` from loops at `-O1+`, reintroducing the libcall. Apply in
      `src/pplang.c` after option validation (near line 133) and/or in `src/pp_compiler_pipeline.c:266`.
      Note this is a temporary restriction; a later improvement can emit `llvm.memcpy.inline` (available
      in the installed LLVM, `llvm/IR/Intrinsics.td:1159`) for constant-size copies, which is guaranteed
      never to lower to a libcall, and then allow `-O1+`.
* [x] Verify no libcall remains, at the object level:
      `bin/pplang --ffreestanding -c /tmp/free.pp -o /tmp/free.o && nm -u /tmp/free.o | grep -E 'mem(cpy|set|move)|malloc|calloc|realloc|free'`
      must print nothing. (Verified on small and 1 KiB-array programs.)
* [x] Inspect asm too: `bin/pplang --ffreestanding -S /tmp/free.pp -o /tmp/free.s` and
      `grep -E 'call.*(memcpy|memset|malloc|free)' /tmp/free.s` must be empty. (Verified.)
* [x] `-O2` with `--ffreestanding` warns and produces an object with no heap/mem symbols.

### Step 5 — handle / `copy_T` / `drop_T` verification (ready for review: [x])

No code change expected; this is a correctness gate. With lists/strings rejected, the only non-trivial
type is a handle (directly or inside a struct/array of structs):

* [x] Confirm `pp_type_is_copyable` returns false for handles (`src/pp_type.c:229-330`), so `copy_T`
      never clones a handle. Pinned by `SemanticTest.FreestandingHandleCopyRejected` (rejects in both
      modes).
* [x] Confirm `gen_drop_body` handle branch loads the underlying value and calls the bound drop symbol
      — no allocator. Pinned by `CodeGenTest.FreestandingHandleDropCallsUserFunction` (IR contains
      `@pp_close`, no `@malloc`/`@free`).
* [-] Confirm structs containing handles get a `pp.drop.*` helper -> pre-existing assertion in
      `pp_semantic_analyzer_check_struct_initializer_expression` fires for a struct-with-handle
      initializer in both normal and freestanding mode. Out of scope for this migration; recorded as a
      separate issue.
* [x] Smoke test: freestanding `handle` with an `extern` C drop compiles, runs, and calls the drop
      function exactly once on each path. Verified with a `/tmp` C shim.

### Step 6 — `lib/pp_std` and prelude (ready for review: [x])

* [x] No compiler change required: preludes are user-supplied via `--ppi`.
* [x] Document that these are not usable in freestanding mode, directly at their source:
  * `lib/pp_std/extern/pp_heap.ppi` — allocator handle,
  * `lib/pp_std/extern/pp_runtime.ppi` — `pp_get_argv -> [string]`, `pp_system :: (const string&)`,
  * `lib/pp_std/extern/pp_io.ppi` — uses `const string&`.
  Their C sources (`pp_heap.c`, `pp_io.c`, `pp_runtime.c`) call `malloc/free/calloc/realloc`.
* [-] Optional `-DFREESTANDING` handling in `lib/pp_std/Makefile` -> not applicable: the compiler uses a
      runtime flag, not a build macro. Exclusion is documented instead.
* [x] Document the pointer+length replacement idiom for freestanding I/O in `pp_io.ppi`:
      `extern func write :: (fd :: int32, buf :: ptr[const uint8], n :: uint64) -> int64;`.

### Step 7 — Tests (ready for review: [x])

* [x] `tests/semantictest.cpp`: added freestanding cases (10 new): `string` type, heap `"..."` literal,
      `[T]` type, `[T; ...]` literal, `list_push`, handle copy rejection; allowed cases for `@"..."` →
      `[uint8:N]`, fixed arrays, and a handle; plus a normal-mode regression that string/list still pass.
      Added `semantic_analyze_source_opts(..., bool freestanding)` helper.
* [x] `tests/codegentest.cpp`: added 3 cases via `compile_and_verify(source, debug, freestanding)`:
      normal mode declares `@malloc/@realloc/@free/@memcmp`; freestanding omits all of them and
      `llvm.memcpy`; freestanding handle drop calls `@pp_close` without heap symbols.
* [x] Existing suite green: `bin/unittest` reports **640/640 passing** (628 pre-existing + 12 new).
* [x] CLI smoke: a `/tmp` heap-free program (fixed arrays, static string literal, handle + C shim,
      drop observed via exit status) builds/runs with `--ffreestanding`. `nm -u` on the linked binary
      has no `mem(cpy|set|move)|malloc|calloc|realloc|free`.
* [-] Add a freestanding demo under `demo/` -> intentionally removed: `demo/` is user-facing examples,
      not a test fixture. Freestanding coverage lives in the unit tests and the `/tmp` smoke test.

### Step 8 — Docs (ready for review: [x])

* [x] New page `docs/language/11-freestanding.md`: what is available, what is rejected, why (no heap),
      `--ffreestanding` usage, pointer+length I/O idiom, handle behaviour.
* [x] Cross-reference from `docs/language/01-types.md` (list/string sections),
      `docs/language/02-variables-and-const.md` and `06-initialization.md` (string literal rules),
      `docs/reference/builtins.md` (list builtins), `docs/reference/known-limitations.md`, and the
      documentation index `docs/README.md`.
* [x] README build/usage snippet: `./bin/pplang --ffreestanding program.pp`.
* [x] Note the `-O` restriction (freestanding forces unoptimized IR until an `llvm.memcpy.inline`/
      `memset` policy is added).

### Step 9 — Final review (ready for review: [x])

* [x] Full clean build: `make clean && make pplang && make unittest && bin/unittest` → **640/640 pass**.
* [x] Valgrind check of the compiler in both modes: 0 errors, all heap blocks freed in both
      `--ffreestanding` and normal mode.
* [x] `clang-format -i src/*.c include/*.h` after edits.
* [x] `migration-freestanding_REVIEW_GUIDE.md` created with the evidence: command output, object
      `nm -u` results, IR excerpts.
* [x] All step statuses marked `(ready for review)`.

### Steps 8–9 verification run (2026-11-25)

* Clean build + full suite: 640/640 tests pass.
* `valgrind --leak-check=yes --error-exitcode=99 ./bin/pplang --ffreestanding -c ...`:
  0 errors, "All heap blocks were freed".
* Normal-mode valgrind: 0 errors, "All heap blocks were freed".
* `/tmp` heap-free program + C shim: builds with `--ffreestanding`, runs, drop observed via exit status.
* Formatting: `clang-format` applied to `src/` and `include/`; an unrelated pre-existing formatting
  diff in `src/pp_linker_offload.c` was reverted to keep the change set focused.

---

### Steps 3–4 verification run (2026-11-25)

* `make pplang` clean; `bin/unittest` 628/628 pass.
* Freestanding IR: no `malloc/realloc/free/memcmp`, no `llvm.memcpy`.
* `nm -u` on freestanding objects is empty for heap/memory symbols (only user `extern`s remain).
* Normal mode IR still declares `malloc/realloc/free/memcmp` — unchanged behavior.
* 1 KiB fixed-array copy under `--ffreestanding -O0`: no `memcpy`/`memset` in asm or undefined symbols;
  program runs correctly.
* `--ffreestanding -O2` prints the forcing warning and still produces a heap/memory-libcall-free object.

## Risks and mitigations

1. **`llvm.memcpy` lowering to a libcall.** Mitigated by the explicit byte-copy loop (Step 4) and by
   forcing no optimization pipeline in freestanding mode. Acceptance is symbol-level (`nm -u` empty),
   not source-level.
2. **Optimizer-synthesized `memset` at `-O1+`.** Same mitigation: force `-O0`; document. Future:
   `llvm.memcpy.inline` for constant sizes.
3. **Missing a gate.** `string`/`[T]` can enter through aliases, struct fields, params, returns,
   `const`/reference wrappers, or list literals. The tests in Step 7 must cover each entry point;
   the defensive codegen asserts turn a miss into a loud failure, not silent heap use.
4. **`string` alias assert.** `check_literal_expression` asserts the `string` alias at line 5981. Keep
   alias registration, and reject the literal case before that lookup.
5. **Handle regression.** `copy_T` must not copy handles; `drop_T` must not change. Step 5 pins this
   with a runtime smoke test.
6. **Name collisions.** `--ffreestanding` is long-only; no clash with `-f` or existing options.
7. **Mixed object reuse.** Not applicable here (runtime flag), but if `-DFREESTANDING` is later added as
   a build default, remember it changes object contents and needs a separate object directory.

## Acceptance criteria

* `bin/pplang --ffreestanding prog.pp` compiles a heap-free program; `nm -u` on the object shows no
  `malloc/calloc/realloc/free/memcpy/memset/memmove`.
* Any use of `string`, `"..."`, `[T]`, `[T; ...]`, `list_push/pop/clear` fails at semantic analysis
  with a clear "not available in freestanding mode" diagnostic and no codegen.
* `@ "..."`, fixed arrays, structs, `extern`, and `handle` (with drop on all paths) work.
* Normal (non-freestanding) compilation and the full unit test suite are unchanged.
* Help text and docs mention the flag and the available subset.

## Next steps after this migration

* Add `-DFREESTANDING` as a build-time default (single `#ifdef` seeding the bool) if a dedicated
  no-heap compiler binary is wanted.
* Add a `--no-stdlib` / prelude policy so `--ppi` files that reference string/list fail early with a
  clearer message.
* Consider `llvm.memcpy.inline` to lift the `-O0` restriction.
* Consider a freestanding `pp_std` subset (`pp_abort` only) if it proves useful.

---

## In-code checklists

Add above each changed function, matching the existing style (`[x]`, `[ ]`, `[-]`):

```c
/* pp_type_from_type_node Checklist (freestanding mode):
 * [ ] Reject PP_AST_TYPE_KIND_STRING when sa->freestanding
 * [ ] Reject PP_AST_TypeListNode when sa->freestanding
 * [ ] Fixed arrays, static string literals, handles remain allowed
 */
```

Functions expected to gain checklist blocks:

* `pp_type_from_type_node`
* `pp_semantic_analyzer_check_literal_expression`
* `pp_semantic_analyzer_check_collection_initializer_expression`
* `pp_semantic_analyzer_check_expression` (list builtin dispatch)
* `pp_code_generator_create`
* `pp_code_generator_emit_memcpy` / new inline byte-copy helper
* `pp_code_generator_gen_copy_body`
* `pp_code_generator_gen_drop_body`
