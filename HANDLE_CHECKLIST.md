# Handle Declaration — Implementation Plan

Feature: `handle HANDLE :: underlying drop free_func;`
Type-introducing declaration, same family as `newtype` and `typedef`.

Baseline: tree reset to `716fbc3` (parser/AST/tokens/BNF only).

**Status:** Phases 0-4 (type representation + semantic analyzer) are implemented and green.
`bin/unittest` → **455 pass** (424 baseline + 31 handle tests). Phase 5 (code generator) is intentionally not
started yet: a handle declaration currently reaches `pp_code_generator_emit` and hits the 
`default: abort()` branch, so no handle program can be built into a binary until Phase 5 lands.

---

## 1. Language contract (implicit-only ownership)

A handle is a **scope-bound, non-transferable resource**. Three appearances only:

| Position | Example | Rule |
|---|---|---|
| Birth | `let h :: File = pp_open();` | initializer is required, must be an rvalue call returning `File` |
| Borrow | `f :: (f :: const File&)` → `consume(h)` | by reference only; `File&` / `const File&` |
| Death | automatic at end of scope/block, every path | unconditional; null value skipped for pointer underlyings |

Illegal everywhere else:

* by-value handle parameter or argument
* `return <handle local>;` (but `return <call returning handle>;` is legal)
* cast **to** a handle (`0 as File`); reverse `h as int32` is legal
* handle-typed globals, struct fields, collection elements, lambda parameters/returns/locals
* missing initializer, explicit `drop h;` (blocks are the early-release mechanism)

Legal and must keep working:

* reassignment `h = pp_open();` → drop old value, store new (sound: no moves exist, old is always live)
* handle as a temporary bound to a `const File&` parameter, dropped at end of statement
* underlying `ptr[void]` / `int32`; opaque underlyings are the primary use case

Ownership is **lexical**: no runtime flags, no moved-from state, no sentinel.

### Known limitations (accept, document)

* An `extern func ... -> File;` must be declared **after** the handle (pass 1 is sequential).
* A factory cannot return a handle it received as a local — write `return pp_open();`.
* C APIs that take ownership outright (`EVP_PKEY_CTX_set0_*`) get a hand-written `pp_*` wrapper in `pp_std`.
* Borrows are not escape-checked (already true of references in general).
* `.ppi` and user docs deferred.

---

## 2. Phases

Each phase ends with a gate: `make -j && ./bin/unittest` green before continuing.

### Phase 0 — Type representation
Files: `include/pp_type.h`, `src/pp_type.c`

- [x] Add to `pp_type_newtype`: `const char* drop_func_name;` and `struct PPSymbol* drop_symbol;`
      (forward-declare `struct PPSymbol`)
- [x] `bool pp_type_is_handle(const PPType* t)` — `PP_TK_Newtype && drop_func_name != NULL`
- [x] `pp_type_needs_drop_impl` → true for handle
- [x] `pp_type_is_copyable_impl` → false for handle
- [x] `pp_type_is_trivially_copyable_impl` → false for handle

Note: cast/decay/DI machinery already walks `PP_TK_Newtype`, so handle↔underlying needs no extra work in
`pp_type_resolve_machine_type`, `pp_type_decay`, `pp_environment_backend_lookup_type`.

### Phase 1 — Semantic Pass 1: registration
File: `src/pp_semantic_analyzer.c` (model: `..._build_global_table_newtype_decl`, ~line 1975)

- [x] Prototype `pp_semantic_analyzer_build_global_table_handle_decl`; dispatch in
      `pp_semantic_analyzer_build_global_table` next to `PP_AST_NewtypeDeclarationNode` (~line 1038)
- [x] Reject duplicate name (alias + named-type lookup)
- [x] Resolve underlying via `pp_type_from_type_node`
- [x] Guards: reject top-level const/reference; reject `PP_TK_Function` / `PP_TK_Lambda`
- [x] New helpers `pp_type_contains_handle` / `_impl` (mirror `pp_type_contains_reference`, cycle/depth guard):
      reject underlying containing a **reference** or another **handle**, recursive through
      newtype/const/array/list/struct
- [x] Create `PP_TK_Newtype`, set `name`, `underlying`, `drop_func_name` (string), intern, then
      `pp_global_type_table_register_named`

Gate: guard-error semantic tests pass.

### Phase 2 — Semantic Pass 2: drop-function resolution
File: `src/pp_semantic_analyzer.c`

- [x] Prototype `pp_semantic_analyzer_full_analyze_handle_decl`; dispatch in Pass 2 next to
      `PP_AST_NewtypeDeclarationNode` (~line 2188, currently "Nothing to do")
- [x] Re-run the recursive underlying guards here too — struct layouts are only complete after Pass 2 step 1,
      so a handle over a struct that contains a reference is only detectable now
- [x] Look up `drop_func_name` in the value namespace
- [x] Require a callable **declaration** (not lambda, not operator, not a func-typed variable). Concretely the
      drop function must be either `func name :: (underlying) -> void { ... }` (PP bodies are mandatory, so a
      `func` is always a definition) or `extern func name :: (underlying) -> void;` (implementation resolved by
      the linker). Those are the only two shapes that pass `is_callable_decl && PP_TK_Function`.
- [x] The drop parameter must be the **underlying value**, never a handle: `pp_type_mentions_handle(drop_param)`
      is rejected. Without this, the machine-type comparison in `pp_type_same_for_drop` let a drop taking a
      *different* handle over the same underlying type through.
- [x] Require signature `func[(underlying) -> void]`: exactly 1 param, param type == underlying, void return
- [x] Store the resolved symbol on the interned type via
      `pp_global_type_table_find_by_name_mut` → `type.pp_type_newtype.drop_symbol`

Gate: drop-signature semantic tests pass.

### Phase 3 — Type-position rules
File: `src/pp_semantic_analyzer.c`

- [x] **Relax** the signature guard: a handle **behind a reference** is legal in a signature
      (`File&`, `const File&`); a bare handle in a parameter or return type is an error.
      Basis: `pp_type_contains_handle` must distinguish "bare" from "behind `PP_TK_Reference`".
- [x] Ban by-value handle parameters in function, extern function, operator, lambda declarations
      (`..._function_decl` ~1067, `..._extern_function_decl` ~1530, `..._lambda_decl` ~1442)
- [x] Ban handle-typed globals (`..._build_global_table_variable_decl` ~1875)
- [x] Ban handle-containing struct fields (`..._full_analyze_struct_decl_fields` ~2479)
- [x] Ban handle-containing collection elements (`pp_type_validate_collection_element` ~685)
- [x] Ban casts **to** a handle (`pp_semantic_analyzer_check_cast_expression` ~4969)

Gate: each new negative test fails with the intended message.

### Phase 4 — Body-level rules
File: `src/pp_semantic_analyzer.c`

- [x] Handle variable requires an initializer (`..._full_analyze_variable_declaration` ~2859)
- [x] Ban `return <handle local>;`; allow `return <call returning handle>;`
      (`..._full_analyze_return_statement` ~3001) — accept `PP_AST_FunctionCallNode` (and calls) only
- [x] By-value handle **arguments**: no separate call-site check. Phase 3 already forbids any bare handle
      parameter, so a parameter that could accept a handle by value cannot be declared. Passing a handle is only
      possible through `H&` / `const H&`, which is the borrow case.
- [x] Confirm reassignment is accepted unchanged (`..._full_analyze_assignment_statement` ~3084)
- [x] **The bound drop function is not callable from source.** `pp_is_handle_drop_function` scans the type table
      for a newtype whose `drop_func_name` matches; `check_function_call` rejects a direct call
      (`pp_close(1)`), and the identifier checker rejects every other use, including storing it in a
      `func[...]` variable that could then be called. Only the compiler's scope-exit drop may call it.

Gate: positive/negative tests in Phase 6 pass.

### Phase 5 — Code generator
File: `src/pp_code_generator.c`, `include/pp_code_generator.h` (no struct changes expected)

- [x] Top-level dispatch: add `PP_AST_HandleDeclarationNode` to the ignorable declarations (~line 1568,
      currently `default: abort()`)
- [x] `pp_code_generator_emit_drop` (~205): unwrap `PP_TK_Newtype` only when **not** a handle; a handle falls
      through to `ensure_drop_fn`
- [x] `pp_code_generator_gen_drop_body` (~4484): new first branch for handle newtypes —
      arg0 is `i8*`; bitcast to `ptr[underlying]`; load underlying; if the underlying lowers to a pointer,
      skip the call when null; then call the bound function (resolved via
      `pp_environment_backend_lookup_val(cg->global_env_b, drop_symbol)`) passing the underlying value
- [x] Handle locals/params: plain `pp_code_generator_push_drop` at scope entry (~1779, ~1949) — no flags,
      no changes to `pp_code_generator_emit_scope_drops` (~556)
- [x] Reassignment: existing path (~1986, ~2036) already drops the old value before storing — verify only
- [x] Temporary handle bound to `const H&`: caller drops it via `ref_temp_addrs` (~2929) — verify only
- [x] Confirm `return <call>` leaves no drop of the returned user in the callee
- [x] **Reassignment must drop the overwritten value.** The assignment path only dropped the old value when the
      RHS was an lvalue; `h = pp_open();` stored over a live handle and leaked it (found by the demo: one close
      instead of two). Fixed by emitting the handle drop before the store in the rvalue path.

Gate: codegen tests assert the drop call and the null guard in IR.

### Phase 6 — Tests
`tests/semantictest.cpp` (naming: `Handle*`)

- [x] Negative: duplicate name (`HandleDuplicateNameError`, `HandleCollidesWithNewtypeError`); handle-of-handle;
      underlying ref; underlying struct containing ref; underlying func; missing drop fn; wrong param type;
      two params; non-void return; lambda as drop; func-typed variable as drop; const underlying
- [x] Negative: by-value param; handle global; struct field; list element; cast to handle; missing initializer;
      `return h;`; handle local inside a lambda; lambda param/return
- [x] Positive: `const File&` / `File&` param and borrow call; `return pp_open();`; reassignment; `h as int32`;
      handle over `ptr[void]`; handle local in a nested block; extern declared after handle
- [x] Negative: direct call of the drop function (`HandleDropFunctionDirectCallError`); the drop function used as
      a value / stored in a `func[...]` variable (`HandleDropFunctionAsValueError`); drop taking another handle
      (`HandleDropTakingAnotherHandleError`)
- [x] Positive: drop declared as `extern func` (`HandleDropAsExternIsAccepted`) and as a defined `func`
      (`HandleDropAsDefinedFuncIsAccepted`)

`tests/codegentest.cpp`

- [x] `HandleDropsViaBoundFunction` — one `call void @pp_close(i32` in the scope-exit path
- [x] `HandlePointerSkipsNullDrop` — null guard (`handle_is_null`) + `call void @pp_free(ptr`
- [x] `HandleDropsOnEveryReturnPath` — a two-return function has two drop sites
- [x] `HandleBorrowDoesNotDrop` — `const File&` parameter produces no drop of the parameter
- [x] `HandleReassignDropsOldValue` — two drop sites for one variable
- [x] `HandleDeclarationEmitsNoCode` — the declaration itself produces no IR
- [x] `HandleReassignDropsOld` — two drop calls for one variable

### Phase 7 — Demo
`demo/06_handle/`

- [x] `main.pp` + `handle_demo.c` (C counters) + `Makefile` + `README.md`
- [x] One round trip: write `12345678` to a file through a write handle, then read it back through a read handle
      into a `[uint8 : 8u]` array (`@buf as ptr[uint8]`) and print it. Covers birth, drop at scope exit, drop on
      an early-return path, the reverse cast `f as int32`, and pointer value use
- [x] Assert exact drop counts from C; exit non-zero on mismatch
- [x] Do not commit the built binary

### Phase 8 — Docs / cleanup
- [ ] `docs/language/` section for handles (contract table from §1)
- [ ] `notes/` ownership note update
- [ ] Confirm no `HANDLE_OWNERSHIP_PROBLEM.md` remains

---

## 3. Review guide

| # | Step | Verified by | Done |
|---|---|---|---|
| 0 | Type representation changes only | build + 424 tests still green | [x] |
| 1 | Pass 1 registration + underlying guards | guard-negative tests | [x] |
| 2 | Pass 2 drop resolution | signature tests | [x] |
| 3 | Type-position bans + `H&` allowance | position tests | [x] |
| 4 | Body rules (initializer, return, args) | body tests | [x] |
| 5 | Codegen: dispatch, drop body, no flags | IR assertions in codegen tests | [x] |
| 6 | Full test suite | `./bin/unittest` all green | [x] |
| 7 | Demo | `demo/06_handle` prints `12345678`, exit 0, valgrind clean | [x] |
| 8 | Docs | read-through | [ ] |

## 4. Definition of done

* A handle is born in a `let`, borrowed by reference, and dropped exactly once at scope exit.
* No runtime ownership state of any kind in the generated IR.
* `./bin/unittest` green; demos 01–06 build and run; valgrind clean for demo 06.
* Contract table in §1 matches the implemented errors one-for-one.
