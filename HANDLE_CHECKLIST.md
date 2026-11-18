# Handle Declaration — Implementation Plan

Feature: `handle HANDLE :: underlying drop free_func;`
Type-introducing declaration, same family as `newtype` and `typedef`.

Baseline: tree reset to `716fbc3` (parser/AST/tokens/BNF only). Semantic analyzer and code generator start
fresh. `bin/unittest` → 424 pass.

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

- [ ] Add to `pp_type_newtype`: `const char* drop_func_name;` and `struct PPSymbol* drop_symbol;`
      (forward-declare `struct PPSymbol`)
- [ ] `bool pp_type_is_handle(const PPType* t)` — `PP_TK_Newtype && drop_func_name != NULL`
- [ ] `pp_type_needs_drop_impl` → true for handle
- [ ] `pp_type_is_copyable_impl` → false for handle
- [ ] `pp_type_is_trivially_copyable_impl` → false for handle

Note: cast/decay/DI machinery already walks `PP_TK_Newtype`, so handle↔underlying needs no extra work in
`pp_type_resolve_machine_type`, `pp_type_decay`, `pp_environment_backend_lookup_type`.

### Phase 1 — Semantic Pass 1: registration
File: `src/pp_semantic_analyzer.c` (model: `..._build_global_table_newtype_decl`, ~line 1975)

- [ ] Prototype `pp_semantic_analyzer_build_global_table_handle_decl`; dispatch in
      `pp_semantic_analyzer_build_global_table` next to `PP_AST_NewtypeDeclarationNode` (~line 1038)
- [ ] Reject duplicate name (alias + named-type lookup)
- [ ] Resolve underlying via `pp_type_from_type_node`
- [ ] Guards: reject top-level const/reference; reject `PP_TK_Function` / `PP_TK_Lambda`
- [ ] New helpers `pp_type_contains_handle` / `_impl` (mirror `pp_type_contains_reference`, cycle/depth guard):
      reject underlying containing a **reference** or another **handle**, recursive through
      newtype/const/array/list/struct
- [ ] Create `PP_TK_Newtype`, set `name`, `underlying`, `drop_func_name` (string), intern, then
      `pp_global_type_table_register_named`

Gate: guard-error semantic tests pass.

### Phase 2 — Semantic Pass 2: drop-function resolution
File: `src/pp_semantic_analyzer.c`

- [ ] Prototype `pp_semantic_analyzer_full_analyze_handle_decl`; dispatch in Pass 2 next to
      `PP_AST_NewtypeDeclarationNode` (~line 2188, currently "Nothing to do")
- [ ] Re-run the recursive underlying guards here too — struct layouts are only complete after Pass 2 step 1,
      so a handle over a struct that contains a reference is only detectable now
- [ ] Look up `drop_func_name` in the value namespace
- [ ] Require a callable **declaration** (not lambda, not operator, not a func-typed variable)
- [ ] Require signature `func[(underlying) -> void]`: exactly 1 param, param type == underlying, void return
- [ ] Store the resolved symbol on the interned type via
      `pp_global_type_table_find_by_name_mut` → `type.pp_type_newtype.drop_symbol`

Gate: drop-signature semantic tests pass.

### Phase 3 — Type-position rules
File: `src/pp_semantic_analyzer.c`

- [ ] **Relax** the signature guard: a handle **behind a reference** is legal in a signature
      (`File&`, `const File&`); a bare handle in a parameter or return type is an error.
      Basis: `pp_type_contains_handle` must distinguish "bare" from "behind `PP_TK_Reference`".
- [ ] Ban by-value handle parameters in function, extern function, operator, lambda declarations
      (`..._function_decl` ~1067, `..._extern_function_decl` ~1530, `..._lambda_decl` ~1442)
- [ ] Ban handle-typed globals (`..._build_global_table_variable_decl` ~1875)
- [ ] Ban handle-containing struct fields (`..._full_analyze_struct_decl_fields` ~2479)
- [ ] Ban handle-containing collection elements (`pp_type_validate_collection_element` ~685)
- [ ] Ban casts **to** a handle (`pp_semantic_analyzer_check_cast_expression` ~4969)

Gate: each new negative test fails with the intended message.

### Phase 4 — Body-level rules
File: `src/pp_semantic_analyzer.c`

- [ ] Handle variable requires an initializer (`..._full_analyze_variable_declaration` ~2859)
- [ ] Ban `return <handle local>;`; allow `return <call returning handle>;`
      (`..._full_analyze_return_statement` ~3001) — accept `PP_AST_FunctionCallNode` (and calls) only
- [ ] Ban by-value handle **arguments** in the call checker (where `PP_AST_FunctionCallNode` args are
      verified against parameter types)
- [ ] Confirm reassignment is accepted unchanged (`..._full_analyze_assignment_statement` ~3084)

Gate: positive/negative tests in Phase 6 pass.

### Phase 5 — Code generator
File: `src/pp_code_generator.c`, `include/pp_code_generator.h` (no struct changes expected)

- [ ] Top-level dispatch: add `PP_AST_HandleDeclarationNode` to the ignorable declarations (~line 1568,
      currently `default: abort()`)
- [ ] `pp_code_generator_emit_drop` (~205): unwrap `PP_TK_Newtype` only when **not** a handle; a handle falls
      through to `ensure_drop_fn`
- [ ] `pp_code_generator_gen_drop_body` (~4484): new first branch for handle newtypes —
      arg0 is `i8*`; bitcast to `ptr[underlying]`; load underlying; if the underlying lowers to a pointer,
      skip the call when null; then call the bound function (resolved via
      `pp_environment_backend_lookup_val(cg->global_env_b, drop_symbol)`) passing the underlying value
- [ ] Handle locals/params: plain `pp_code_generator_push_drop` at scope entry (~1779, ~1949) — no flags,
      no changes to `pp_code_generator_emit_scope_drops` (~556)
- [ ] Reassignment: existing path (~1986, ~2036) already drops the old value before storing — verify only
- [ ] Temporary handle bound to `const H&`: caller drops it via `ref_temp_addrs` (~2929) — verify only
- [ ] Confirm `return <call>` leaves no drop of the returned user in the callee

Gate: codegen tests assert the drop call and the null guard in IR.

### Phase 6 — Tests
`tests/semantictest.cpp` (naming: `Handle*`)

- [ ] Negative: duplicate name; handle-of-handle; underlying ref; underlying struct containing ref;
      underlying func/lambda; missing drop fn; wrong param type; two params; non-void return; lambda as drop;
      func-typed variable as drop
- [ ] Negative: by-value param; by-value argument; handle global; struct field; list element;
      cast to handle; missing initializer; `return h;`; handle local inside a lambda
- [ ] Positive: `const File&` / `File&` param and borrow call; `return pp_open();`; reassignment;
      `h as int32`; handle over `ptr[void]`; extern declared after handle

`tests/codegentest.cpp`

- [ ] `HandleDropsViaBoundFunction` — one `call void @pp_close(i32` in the scope-exit path
- [ ] `HandlePointerSkipsNullDrop` — null guard + `call void @pp_free(ptr`
- [ ] `HandleDropsOnEarlyReturn` — both branches of an `if` return → exactly one drop on each path
- [ ] `HandleBorrowDoesNotDrop` — `const File&` parameter produces no drop of the parameter
- [ ] `HandleReassignDropsOld` — two drop calls for one variable

### Phase 7 — Demo
`demo/06_handle/`

- [ ] `main.pp` + `handle_demo.c` (C counters) + `Makefile` + `README.md`
- [ ] Scenarios: scope drop, drop in a loop (3×), early return on both branches, null-pointer skip,
      borrow call, reassignment (old value dropped), reverse cast read
- [ ] Assert exact drop counts from C; exit non-zero on mismatch
- [ ] Do not commit the built binary

### Phase 8 — Docs / cleanup
- [ ] `docs/language/` section for handles (contract table from §1)
- [ ] `notes/` ownership note update
- [ ] Confirm no `HANDLE_OWNERSHIP_PROBLEM.md` remains

---

## 3. Review guide

| # | Step | Verified by | Done |
|---|---|---|---|
| 0 | Type representation changes only | build + 424 tests still green | [ ] |
| 1 | Pass 1 registration + underlying guards | guard-negative tests | [ ] |
| 2 | Pass 2 drop resolution | signature tests | [ ] |
| 3 | Type-position bans + `H&` allowance | position tests | [ ] |
| 4 | Body rules (initializer, return, args) | body tests | [ ] |
| 5 | Codegen: dispatch, drop body, no flags | IR assertions in codegen tests | [ ] |
| 6 | Full test suite | `./bin/unittest` all green | [ ] |
| 7 | Demo | `demo/06_handle` exit 0 + valgrind clean | [ ] |
| 8 | Docs | read-through | [ ] |

## 4. Definition of done

* A handle is born in a `let`, borrowed by reference, and dropped exactly once at scope exit.
* No runtime ownership state of any kind in the generated IR.
* `./bin/unittest` green; demos 01–06 build and run; valgrind clean for demo 06.
* Contract table in §1 matches the implemented errors one-for-one.
