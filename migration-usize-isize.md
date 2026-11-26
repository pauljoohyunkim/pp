# Migration: `usize` / `isize` (pointer-width integers)

Branch: `feature/size-usize`
Status: **Step 1 implemented — ready for review**

## Goal

Add a pair of nominal, target-dependent integer types so pointer-width quantities ("how many", "which
position") stop being modelled as fixed 64-bit values. On a 32-bit target these currently produce wrong IR,
wrong struct layouts, wrong DWARF, and memory corruption at the C boundary (see `c48fd01`).

- `usize` — unsigned, pointer width (`LLVMIntPtrTypeInContext`)
- `isize` — signed, pointer width
- Literal suffix **`z`** / **`uz`** (e.g. `42z`, `42uz`). **NOT `a`**: `a` is `isxdigit()`, so `0x42a` is
  already the number 1066 in `src/pp_lexer.c:526`; a hex-digit suffix silently breaks in hex literals.
  `z` matches C++23's `size_t` literal suffix and is unambiguous.

They are **nominal types, not typedefs of `uint64`**. Two widths that happen to coincide on 64-bit targets
must not compare equal, so 64-bit-only code breaks loudly on 32-bit instead of silently.

## Decisions (settle before coding)

- [x] **D1 — Index acceptance policy: STRICT.** Only `usize` is accepted as an index, e.g. `a[0uz]`,
  `a[1uz + 3uz]`. `uint8..uint64` and bare literals stop being valid indices. Enforced in `Step 2` at
  `src/pp_semantic_analyzer.c:6289` (replace `pp_type_kind_is_unsigned_up_to_64` with `kind == PP_TK_USize`).
- [x] **D2 — Unsuffixed literals stay `int32` (hard default).** No contextual literal typing. Consequence:
  indices must carry the `uz` suffix (`a[0uz]`), never bare `a[0]`.
- [x] Public spelling confirmed: **`usize` / `isize` only**.

## Current vs Desired behavior

| Quantity | Current | Desired |
|---|---|---|
| `#` operator result | `uint32` (`pp_semantic_analyzer.c:5782`) | `usize` (constexpr for arrays) |
| Index type accepted | any unsigned 8..64 | per D1 |
| `pp.string.len` | `uint64` (`pp_std_types.h:18`) | `usize` (`size_t`) |
| `pp.list.len/cap/elem_size` | `uint64` (`pp_std_types.h:24-26`) | `usize` (`size_t`) |
| `pp.str` view | `{ptr, uint64}` (no C struct yet) | `{ptr, usize}` |
| List codegen index/stride math | `i64` everywhere | pointer-width int |
| `pp_alloc`/`pp_realloc`/`pp_list_grow` size args | `uint64_t` | `size_t` (+ overflow check) |
| Pointer↔int cast canonical form | `uint64 as ptr[T]` | `usize as ptr[T]` / `ptr[T] as usize` |
| DI basic type for `usize` | n/a | target pointer size |
| C interop | `size_t`→? | `size_t`→`usize`, `ssize_t`/`ptrdiff_t`→`isize` |

## Step-by-step plan

Each step is a commit. Step 3 **must** be a single commit (half-migrated layout corrupts memory silently).

### Step 1 — Types, suffix, casts  `(ready for review)`
- [x] `include/pp_type.h`: add `PP_TK_USize`, `PP_TK_ISize` after `PP_TK_UInt128`.
- [x] `include/pp_type.h` predicates: added to `pp_type_kind_is_integer` / `_is_signed` / `_is_unsigned`;
  added `pp_type_kind_is_pointer_width(k)`. Intentionally **not** in `pp_type_kind_is_unsigned_up_to_64`.
- [x] `include/pp_tokens.h` + `src/pp_lexer.c`: keywords `usize`/`isize`; lexer now also consumes a trailing
  `z` suffix (never `a`; hex `isxdigit` scan untouched).
- [x] `include/pp_ast.h` + `src/pp_parser.c`: `PP_AST_TYPE_KIND_USIZE`/`_ISIZE` mapped from the tokens.
- [x] `src/pp_type_table.c`: `pp_type_equal`, `pp_type_decay`, `pp_type_decay_cg` treat the kinds as primitives
  → nominal, distinct from uint64/int64.
- [x] `src/pp_type.c`: `pp_type_parse_int_literal_suffix` handles `z`/`uz` from the tail (overlong `lll` still
  resolves via last two letters, keeping existing diagnostics); `pp_type_is_numerical` /
  `pp_type_is_unsigned_integer` / trivially-copyable updated.
- [x] `src/pp_code_generator.c` `pp_backend_lower_type`: `usize`/`isize` →
  `LLVMIntPtrTypeInContext(cg->ctx, native_target_data)`.
- [x] Literal codegen: uses lowered type automatically via `lit_type`.
- [x] Casts: numeric→ptr and ptr→numeric normalise through `pp_code_generator_size_type(cg)` (pointer width),
  not hardcoded `i64` (fixed a latent 32-bit bug). Semantic cast check passes via `pp_type_is_numerical`.
- [x] DI basic type for `usize`/`isize` uses `pp_code_generator_pointer_bits(cg)`.
- [x] Analyzer: pre-interned `usize`/`isize` aliases; `integer_kind_name`/`_width`/`int_literal_fits`;
  builtin binary/unary support; binary expression type resolution and unary minus accept the kinds.
- [x] Codegen shift-overflow bound for `usize` uses pointer bits.
- [x] Tests: lexer (`z`/`uz`, `0x42a`==1066, keywords), semantic (nominal distinct, casts, arithmetic),
  codegen (pointer-width lowering, ptr cast width).
- [x] No `PP_TK_*` ordering/array-size assumptions found (`PP_TK_Operator` is last; no kind-count arrays).
- [x] Verified: 650 unit tests pass; `demo/01_fibo` builds+runs; 32-bit i386 IR shows `usize`→`i32` and
  `inttoptr i32`; valgrind clean on a `usize` smoke program.

### Step 2 — `#` and indexing in analyzer  `(pending)`
- [ ] `#` returns `usize` (`src/pp_semantic_analyzer.c:5782`), keeping constexpr flag for arrays.
- [ ] Index check (`:6284-6296`): enforce D1 policy; update comment at `:6287`.
- [ ] Array-size / constexpr contexts: range-check `usize` values against target pointer width during
  constant folding (wide host type, target-width bound).
- [ ] Tests: `#arr` type is `usize`; `a[#a - 1z]` typechecks; underflow wraps but following index traps (bounds
  check).

### Step 3 — Layout + codegen + C headers (single commit)  `(pending)`
- [ ] `lib/pp_std/include/pp_std_types.h`: `pp_string_t.len`, `pp_list_t.len/cap/elem_size` → `size_t`.
  Update comments at `:15`, `:21`.
- [ ] Add/align `pp.str` C view `{const uint8_t*; size_t len;}`.
- [ ] `lib/pp_std/include/pp_heap.h`: `pp_alloc_bytes_*` / reallocate take `size_t`.
- [ ] `src/pp_code_generator.c` layout + ops → pointer-width:
  - [ ] `pp.list` struct fields + DI (`:1724-1762`), `pp.string` DI (`:1694-1716`).
  - [ ] list push/grow/pop/index math (`:5903`, `:5913-5981`, `:6006`), list copy (`:556`, `:5365`),
    list alloc/build (`:5042-5085`), array shift/alloc (`:5489`).
  - [ ] Drop sign/zero-extensions that existed only to widen `uint32` index to `i64`.
- [ ] `pp_alloc` overflow check: `len * elem_size` computed at pointer width, trap/handle on 32-bit.
- [ ] Contract test: `tests/codegentest.cpp` `ListElementStrideUsesAllocatedSize` (`:3327`) and
  `ListOfStringsDropUsesElementStride` (`:2441`) — assert against **target `sizeof`**, not literal 8.
- [ ] Add layout contract test asserting `pp.list`/`pp.string` field types match C `size_t`.

### Step 4 — Stdlib signatures / `.ppi`  `(pending)`
- [ ] `pp_args_count` → `usize`; `pp_file_read` length → `usize`, result → `isize`.
- [ ] `pp_alloc_bytes_reallocate` etc. → `usize`; grep `lib/pp_std` for `uint64`/`int64` length-like params.
- [ ] C interop mapping: `size_t→usize`, `ssize_t`/`ptrdiff_t→isize`; document `int`/`long long`/`long`
  per-target mapping; avoid `char`/`long` in shim headers (use `<stdint.h>`).

### Step 5 — Demos / tests  `(pending)`
- [ ] `demo/`, `tests/` that used `uint32`/`uint64` lengths or `uint64 as ptr[T]` → `usize` forms.
- [ ] Update examples in `notes/handle_design.md` (`:48`, `:54`, `:110`).

## Risks

- **D1(a) without D2** makes indexing painful (`0z` everywhere) — do not ship D1(a) before deciding D2.
- **Half-migrated layout** (PP 32-bit, C 64-bit) corrupts memory with no compile error → Step 3 is atomic.
- **DI/DWARF** for `usize` must read pointer size from data layout; a hardcoded 64 hides the bug on 64-bit
  hosts and only breaks under 32-bit codegen.
- **Constant folding** uses host types today; `usize` array sizes need target-width range checks.
- **Nominal distinctness** may break existing tests that treat `uint64` as universal; audit semantic tests.

## Acceptance criteria

- [ ] `usize`/`isize` parse, lower to pointer-width int, and are nominal (distinct from `uint64`/`int64`).
- [ ] `#` yields `usize`; indexing policy enforced per D1 and documented in one place.
- [ ] `pp.list`/`pp.string`/`pp.str` layouts identical on PP and C sides, field type `size_t`, verified by
  contract tests against target `sizeof`.
- [ ] List codegen produces no `i64` index/stride math and no leftover width-extension ops.
- [ ] Full suite green: `make bin/unittest && bin/unittest`; demos compile + run (`bin/pplang`).
- [ ] **32-bit IR check**: emit IR for a 32-bit triple (`-m32` / `--target=i386-…` or 32-bit data layout) and
  confirm `usize` is `i32` and `pp.list` fields are 4 bytes — even if not executed.

## Verification commands

```bash
make bin/unittest
bin/unittest --gtest_filter="*Semantic*:*CodeGen*:*Lexer*:*Parser*"
./bin/pplang demo/<smoke>.pp
# 32-bit IR sanity
./bin/pplang --emit-llvm --target=i386-unknown-linux-gnu demo/<smoke>.pp | grep -E 'i32|pp.list'
clang-format -i src/*.c include/*.h
```

## Next steps

1. Resolve D1 and D2 above (one-line answers unblock Step 1).
2. Implement Step 1, tick boxes, commit.
3. Proceed step-by-step; keep this file authoritative. Add `migration-usize-isize_REVIEW_GUIDE.md` when
   implementation starts (per incremental-plan-review workflow).
