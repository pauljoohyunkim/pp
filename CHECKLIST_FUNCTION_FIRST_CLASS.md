# First-Class Function and Lambda Values — Implementation Checklist

Goal: make `func[...]` and `lambda[...]` first-class value types, e.g.

```pp
let f :: func[(int32, float) -> float] = predef_func;
let g :: func[(int32, float) -> float] = f;
g = predef_func;

let h :: func[(int32, float) -> float]& = f;   // reference to a function-typed variable
let c :: const func[(int32, float) -> float] = predef_func;
```

A function value is an ordinary pointer-sized value, **not** a separate "function pointer" type in the type
system. `func[...]` reuses `PP_TK_Function`, `lambda[...]` reuses `PP_TK_Lambda`. Const and `&` apply to the
binding exactly as for any other object.

No captures, no closures: a function value is a code pointer only. No null function value.

---

## Phase 0 — Investigation summary and design decisions

### 0.1 What already works  (verified 2026-11-18)

- [x] Confirm `PP_TK_Function` / `PP_TK_Lambda` already carry `return_type`, `param_types`, `param_count`
      (`include/pp_type.h`, union members `pp_type_function` / `pp_type_lambda` / `pp_type_operator`).
- [x] Confirm structural equality/interning compares callable signatures recursively
      (`src/pp_type_table.c:127-150`, plus `pp_global_type_table_intern` / `_find_type` at 195 / 248).
- [x] Confirm `pp_type_free` frees parameter arrays (`src/pp_type.c:49-53`).
- [x] Confirm `pp_type_push_param` works for Function/Lambda/Operator (`src/pp_type.c:102-113`).
- [x] Confirm function declarations already create `PP_TK_Function` symbols and lambdas create `PP_TK_Lambda`
      (`src/pp_semantic_analyzer.c:1030, 1494` function; `1405, 1696` lambda; extern paths nearby).
- [x] Confirm function names already resolve as expressions to function-typed symbols with `RVALUE`
      (`pp_semantic_analyzer_check_identifier_expression`, `src/pp_semantic_analyzer.c:4399-4467`).
- [x] Confirm lexer already has `PP_TT_FUNC` (`pp_tokens.h:71`), `PP_TT_LAMBDA` (`:72`), `PP_TT_RIGHT_ARROW`
      (`:54`), `PP_TT_LEFT/RIGHT_PARENTHESIS` (`:17-18`), `PP_TT_COMMA` (`:33`). Lexer emits both keywords
      (`src/pp_lexer.c:392,395`).
- [x] Confirm calls already go through `generate_rvalue(callee)` (`src/pp_code_generator.c:2651`), so calling an
      arbitrary callee expression is structurally supported.

### 0.2 Blockers  (all confirmed)

- [x] No type syntax for `func[...]` / `lambda[...]`. `pp_parser_parse_collection_type`
      (`src/pp_parser.c:502`) only handles `[`, `ptr[...]`, and base types.
- [x] `pp_type_decay` asserts on Function/Lambda (`src/pp_type_table.c:316`); `pp_type_decay_cg` falls to
      `default: goto error_cleanup` (`:389-390`). All assignment, argument, return and struct-initializer paths go
      through decay.
- [x] Codegen conflates a callable signature with its stored value type: `pp_code_generator_fill_type_map_complex`
      maps `PP_TK_Function/Lambda/Operator` to `LLVMFunctionType` (`src/pp_code_generator.c:1293-1347`), while
      `pp_backend_lower_type` returns `NULL` for them (`:805-811`).
- [x] Semantic analysis treats every Function/Lambda-typed symbol as a callable *name* (`RVALUE`), so a
      function-typed *variable* cannot be rebound, referenced, or distinguished from a predefined function
      (`src/pp_semantic_analyzer.c:4448-4453`).
- [x] Function-typed variables have no "initializer required" rule.
- [x] Struct fields of callable type have no "initializer required" rule.
- [x] Function-typed globals would fail late with a confusing "non constexpr initializer" error.
- [x] `pp_type_contains_reference_impl` wrongly walks into callable signatures
      (`src/pp_semantic_analyzer.c:500-527`).
- [x] Codegen has no DI type for callable values.

### 0.2.1 Additional findings from investigation (added to plan)

- AST type nodes form a family under a `PP_AST_TypeExpressionNode` wrapper (const/ref live on the wrapper):
  `PP_AST_TypeBaseNode`, `PP_AST_TypeArrayNode`, `PP_AST_TypeListNode`, `PP_AST_TypePointerNode`
  (`include/pp_ast.h:58-62, 155-185`). Phase 1 must add `PP_AST_TypeCallableNode` to the same family and a
  separate callable-kind enum; `PP_AST_TypeKind` is for primitives only.
- `pp_type_from_type_node` **whitelists** inner node types and errors with "Unsupported type node" otherwise
  (`src/pp_semantic_analyzer.c:741-747`); Phase 3 must extend that whitelist.
- `pp_type_validate_collection_element` already rejects Function/Lambda elements
  (`src/pp_semantic_analyzer.c:710-712`) — keep as is, no change needed.
- `pp_ast.h` has no `PP_AST_CallableKind` enum yet; it must be introduced (Phase 1).
- Lexer tokens are reused verbatim; no lexer work is needed at all for Phase 2.

### 0.3 Decisions (settled — restated and confirmed 2026-11-18)

- [x] **Lambda → func widening: explicit cast only.** A `lambda[...]` value may become a `func[...]` value only
      via an explicit `as func[...]` cast with an identical signature (pure where impure is allowed). No implicit
      widening in initialization, assignment, argument passing, return, or struct initialization. Never func →
      lambda. The cast is a no-op pointer reinterpretation.
- [x] **Function-typed globals: allow if simple.** Try to support them in v1. Requires:
      - an initializer;
      - the initializer to be a function or lambda name (link-time constant), with a narrow constexpr exception
        for callable names;
      - the named callable to be declared/emitted before the global initializer, or a prior LLVM declaration to
        exist.
      If forward-declaration/order handling turns out to be non-trivial, fall back to banning function-typed
      globals in v1 and record that here.
- [x] **Function-typed struct fields: allow, like const/reference fields.** A field of callable type needs a
      default initializer at global scope; otherwise a struct initializer must provide it.
- [x] **`ptr[func[...]]`: not allowed in v1.** Reject pointers whose pointee is `func[...]`, `lambda[...]`, or any
      other callable type. References to function-typed variables remain allowed.
- [x] **Comparisons: not allowed.** Reject `==` / `!=` (and all other operators) on callable values explicitly in
      v1.

### 0.4 Grammar first (before any implementation)  — DONE, awaiting review

- [x] Update `notes/bnf.md` **before** starting Phase 1. Added to the `<collection_type>` alternation:
      ```bnf
      <collection_type> ::= "[" <type> [ ":" <expression> ] "]"
                         | <pointer_type>
                         | <callable_type>
                         | <base_type>
      <callable_type>   ::= "func"   "[" "(" <type_list> ")" "->" <type> "]"
                         | "lambda" "[" "(" <type_list> ")" "->" <type> "]"
      <type_list>       ::= <type> | <type> "," <type_list> | ""
      ```
- [x] Recorded in `notes/bnf.md` that parameter names are not part of a callable type, `<type_list>` is used
      only inside callable types, empty `()` is allowed, and higher-order signatures are allowed.
- [x] Recorded in `notes/bnf.md` that `<pointer_type>` pointees must not be callable in v1, that `const` / `&`
      bind like any other object type, that lambda → func widening is explicit-only, and that comparisons and
      arithmetic/bitwise operators on callable values are rejected in v1.
- [ ] Have the grammar changes reviewed/agreed before implementation begins.  <-- current gate

**Phase 0 status:** 0.1 and 0.2 fully verified, 0.3 decisions confirmed unchanged, 0.4 grammar written into
`notes/bnf.md`. Implementation has not started; Phase 1 is gated on grammar review.

---

## Phase 1 — AST  — DONE

- [x] Add `PP_AST_TypeCallableNode` to `PP_AST_NodeType` in `include/pp_ast.h` (inserted after
      `PP_AST_TypePointerNode`).
- [x] Add union payload `type_callable` (`kind`, `param_types`, `param_count`, `param_capacity`, `return_type`).
      Reused the existing `PP_AST_CallableKind` enum (`PP_CK_Function` / `PP_CK_Lambda`); no new enum needed.
- [x] Add free logic in `pp_ast_root_free_internal` (`src/pp_ast.c`): frees each param type node, the param array,
      and the return type.
- [x] Add print logic in `pp_ast_root_print` (`src/pp_ast.c`): prints `TypeCallable func|lambda|operator`, then each
      parameter type, then the return type.
- [x] `pp_ast_get_decoration` has a `default: return NULL` arm, so it already ignores type nodes. No other
      node-type switch is exhaustive over type nodes (type nodes are only consumed by `pp_type_from_type_node`,
      which matches inner nodes explicitly). No further changes needed.

---

## Phase 2 — Parser  — DONE

- [x] In `pp_parser_parse_collection_type` (`src/pp_parser.c`), added a branch for `PP_TT_FUNC` / `PP_TT_LAMBDA`
      that delegates to the new `pp_parser_parse_callable_type`.
- [x] Consumes keyword, `[`, parameter **types** separated by commas, `)`, `->`, return type via
      `pp_parser_create_type`, `]`. Token failures rely on `pp_parser_consume_token`'s own diagnostic.
- [x] Empty parameter list `()` supported (pre-checked before the loop).
- [x] Higher-order signatures work recursively (parameter/return types are parsed with `pp_parser_create_type`).
- [x] Parameter names rejected: after parsing a parameter type, a following `::` yields
      "Parameter names are not allowed in a callable type".
- [x] No ambiguity with declaration-position `func name :: ...`: declarations are routed by
      `pp_parser_create_program_node` before any type parsing; both forms are covered by tests.
- [x] `const func[...]` and `func[...]&` work through the existing prefix/suffix handling in
      `pp_parser_create_type` (verified by test).
- [x] Parser tests added in `tests/parsertest.cpp`: `GoodCallableTypeFunctionSyntax`, `…LambdaSyntax`,
      `…ConstAndReference`, `…HigherOrder`, `…EmptyParamList`, `…AsParamAndReturn`,
      `BadCallableTypeWithParamNames`, `BadCallableTypeMissingArrow`. All 8 pass.

Note: malformed callable types at global scope recover with `parse_result == 1` and `parser_errors > 0`, matching
other `Bad*` tests in this suite.
Note: semantic analysis reported "Unsupported type node" for `func[...]`/`lambda[...]` until Phase 3 landed.

---

## Phase 3 — Type resolution  — DONE

- [x] In `pp_type_from_type_node` (`src/pp_semantic_analyzer.c`), accept `PP_AST_TypeCallableNode` (inner-node
      whitelist and the collection/pointer branch condition extended; new final `else` arm handles callables).
- [x] Resolve each parameter type and the return type with `pp_type_from_type_node` (recursive, so higher-order
      signatures work).
- [x] Create `PP_TK_Function` or `PP_TK_Lambda` according to the node's `kind` (`PP_CK_Lambda` → `PP_TK_Lambda`,
      otherwise `PP_TK_Function`; `PP_CK_Operator` cannot appear in a type node and is asserted away).
- [x] Push parameter types via `pp_type_push_param`, then set `return_type` in the matching union member.
- [x] Intern the type in the global type table; on failure push an error and go to `error_cleanup`, which frees
      the temporary `temp_type` unconditionally (NULL-safe).
- [x] Validate as needed: missing `return_type` node is rejected (defensive; the parser always produces one),
      `void` return type is allowed, a `void` parameter type is rejected with "Callable type parameter cannot be
      void". Parameter-name rejection stays in the parser.
- [x] Named-function identity: `pp_global_type_table_intern` compares signatures structurally
      (`pp_type_equal`, `src/pp_type_table.c:125-139`), so a declaration and a `func[...]` type of the same
      signature intern to the same `PP_TK_Function`. Exercising it through `let f :: func[...] = named_func;`
      needs Phase 4 (decay) and Phase 5 (symbol `is_callable_decl`).
- [x] `const func[...]` / `func[...]&` build the usual `PP_TK_Const` / `PP_TK_Reference` wrappers (wrapping code is
      outside the branch and applies to all types). Verified through parameter types: semantic analysis passes;
      codegen still asserts, which is Phase 4+ work.
- [x] `lambda[(...)]` and `func[(...)]` of the same signature are distinct types: `pp_type_equal` switches on
      `kind` before comparing signatures, so interning keeps them separate.

Verification: `make pplang` and `make unittest` clean (381 tests pass). Smoke tests via `bin/pplang`:
`typedef func[(int32, int32) -> int32] AddFn;`, `typedef lambda[(int32) -> int32] MakeFn;`,
higher-order `func[() -> func[(int32) -> int32]]` all compile; `func[(void) -> int32]` is rejected.
Known gap (later phases): a callable-typed struct field passes semantic analysis but aborts in
`pp_code_generator_fill_type_map_complex` (`assert(llvm_member_type)`) — Phase 4 type-map work.

---

## Phase 4 — Type-system predicates  — DONE

- [x] `pp_type_decay` (`src/pp_type_table.c`): returns Function/Lambda/Operator unchanged (no recursion into the
      signature).
- [x] `pp_type_decay_cg` (`src/pp_type_table.c`): same.
- [x] `pp_type_is_trivially_copyable_impl` (`src/pp_type.c`): returns `true` for Function/Lambda/Operator
      (grouped with the scalar/pointer cases, so `pp_type_is_copyable` also short-circuits to true).
- [x] `pp_type_contains_reference_impl` (`src/pp_semantic_analyzer.c`): returns `false` for
      Function/Lambda/Operator instead of traversing the signature. A function value is a code pointer;
      references inside the signature are type identity only.
- [x] Function/Lambda ban in `pp_type_validate_collection_element` unchanged (arrays/lists still rejected, same
      message).
- [x] `pp_type_needs_drop` unchanged: callable kinds fall into `default: return false`.
- [x] `pp_type_equal` unchanged (already compares signature recursively).

Verification: `make pplang` clean (pre-existing warnings only), `./bin/unittest` 381/381 pass.
Smoke (semantic stage): `let f :: func[(int32, int32) -> int32] = add;` is accepted;
`let a :: [func[(int32) -> int32]: 2];` still rejected with "Array/List element type cannot be function/lambda";
`typedef func[(int32&) -> int32] WithRef;` accepted (signature references no longer leak containment).
Note: `ptr[func[...]]` is still accepted by the type layer — Phase 6 item, not a Phase 4 regression.

---

## Phase 5 — Symbol and identifier semantics  — DONE

- [x] Added `bool is_callable;` to `PPSymbol` (`include/pp_symbol.h`) with a comment: true only for callable
      declaration names, false for storage locations holding a callable value.
- [x] `is_callable = true` only in the declaration paths: function decl, operator decl, lambda decl, extern function,
      extern operator (mangled), extern lambda. Globals, parameters and locals pass `false`.
- [x] `pp_symbol_create` gained a trailing `bool is_callable` parameter; all 11 call sites updated.
- [x] `pp_semantic_analyzer_check_identifier_expression`:
  - [x] callable declaration names are `RVALUE` (via `symbol->is_callable_decl`);
  - [x] function-typed variables/parameters/fields are `LVALUE` (type kind is no longer used for the category).
- [x] Lambda global restriction now keys on `is_callable_decl` as well: a `lambda` declaration name stays allowed, a
      `func` declaration name is still rejected as a "regular function used inside lambda", and a function-typed
      *global variable* (even one holding a func value) now falls through to the ordinary global-state checks and
      reports "mutable symbol declared outside of lambda".

Verification (semantic stage via `bin/pplang`, codegen still pending Phase 7/8):
- `let f :: func[(int32, int32) -> int32] = add; f = sub;` — accepted.
- `let h :: func[...]& = add;` — rejected: "Variable \"h\" is a reference, but initializer is not an l-value"
  (function names are not storage locations).
- `let h :: func[...]& = f;` — accepted (function-typed variable is an l-value).
- `let c :: const func[...] = add; c = sub;` — rejected: "LHS of assignment should be mutable (non-const)".
- Lambda parameter of type `lambda[...]` called inside a lambda — accepted; same with a `func[...]` parameter —
  rejected "Lambda/operator function cannot call regular function".
- `make pplang` builds, `./bin/unittest` 381/381 pass.

---

## Phase 6 — Semantic checks  — DONE

New helpers in `src/pp_semantic_analyzer.c`:
- `pp_type_is_callable(type)` — `PP_TK_Function / PP_TK_Lambda / PP_TK_Operator` on an already-decayed type.
- `pp_semantic_callable_signature_equal(a, b)` — signature identity ignoring the callable kind, used by the
  widening cast.

- [x] Variable declaration (`pp_semantic_analyzer_full_analyze_variable_declaration`):
  - [x] initializer is mandatory for callable types: "Variable \"%s\" has a callable type, but initializer does
        not exist.";
  - [x] const/reference-initializer rules unchanged;
  - [x] exact type match after decay (existing `pp_type_equal` on decayed types), so no implicit
        lambda → func widening.
- [x] Assignment statement: decay handles callables (Phase 4), `pp_type_can_assign` requires the same kind and
      signature, so no implicit widening; compound assignment operators still reject callables.
- [x] Return statement: existing decayed `pp_type_equal` already gives exact match / no widening.
- [x] Function-call argument checking: existing decayed `pp_type_equal` already gives exact match / no widening.
- [x] Function-call callee checking (`pp_semantic_analyzer_check_function_call`):
  - [x] callee type is decayed first (`callee_type`), so `const func[...]` and `func[...]&` callees work;
  - [x] kind check, purity restriction, param counts, parameter types and return type all use the decayed type.
- [x] Struct initializer checks: callable fields now require an entry when no default exists ("No initializer given
      for const, reference, or callable field \"%s\""), with exact type matching afterward.
- [x] Struct field default initializers: allowed for callable fields; constexpr requirement satisfied because
      callable declaration names are constexpr (see below).
- [x] Global variable declaration: callable-typed globals require an initializer ("Global variable \"%s\" has a
      callable type, but initializer does not exist."); the narrow constexpr exception is implemented by marking
      identifier decorations of callable declaration names as constexpr, which also covers struct defaults and
      global struct instances containing callable fields. No special-casing needed in the global/struct constexpr
      checks. Codegen ordering is still open (Phase 8).
- [x] Cast expression: only `lambda[...]` → `func[...]` with an identical signature is allowed (decayed, via
      `pp_semantic_callable_signature_equal`). Everything else involving a callable is rejected with "Only a
      lambda[...] value can be cast to an identical func[...] signature." — func → lambda, lambda → lambda,
      func → func, callable ↔ non-callable, mismatched signature.
- [x] Array/list element validation: ban kept, message unchanged.
- [x] Pointer validation: `ptr[T]` rejects a callable pointee ("Pointer cannot point to a callable type"), and
      `nullptr[T]` rejects a callable pointee too. References to function-typed variables are unaffected.
- [x] Callable operands rejected explicitly: binary ("Callable values do not support binary operators in v1.",
      covers `==`/`!=`/arithmetic/bitwise) and unary ("Callable values do not support unary operators in v1.",
      checked before custom-operator lookup, so `@f` is included).

Verification (semantic stage; codegen failures below are Phase 7/8 work):
- Rejected: uninitialized callable local; uninitialized callable global; struct instance missing a callable field;
  implicit lambda → func in initialization, argument and return; func → lambda cast; func → func cast; mismatched
  signature cast; callable ↔ non-callable cast; `==` on two callable variables; `@callable_variable`;
  `ptr[func[...]]`; `nullptr[func[...]]`.
- Accepted: `let g :: func[...] = named_func;` at global scope; struct field with `= named_func` default; global
  struct instance initialized with `.f = named_func`; call through `const func[...]` and `func[...]&` parameters;
  exact-signature `lambda[...]` argument/return values; `doubler as func[(int32) -> int32]`.
- `make pplang` builds; `./bin/unittest` 381/381 pass.

---

## Phase 7 — Code generation: lowering split

Core idea: **value representation = opaque pointer; callable signature = `LLVMFunctionType` built on demand.**

- [ ] `pp_backend_lower_type` (`src/pp_code_generator.c`): lower Function/Lambda/Operator to
      `LLVMPointerTypeInContext(cg->ctx, 0)`.
- [ ] `pp_code_generator_fill_type_map_complex`: stop mapping callable kinds to `LLVMFunctionType`; ensure the
      pointer type is registered in the type map instead.
- [ ] Add `pp_code_generator_llvm_subroutine_type(cg, callable_type)` that builds `LLVMFunctionType` using
      `lookup_type` for each parameter and the return type. Because nested callable types now lower to `ptr`,
      function-typed parameters/returns are correct.
- [ ] Use the helper at every site that needs the true subroutine type:
  - [ ] `pp_code_generator_emit_function`
  - [ ] extern declaration emission (`pp_code_generator_generate`)
  - [ ] `pp_code_generator_generate_lvalue_function_call`
  - [ ] operator call sites in `pp_code_generator_generate_rvalue_binary`
  - [ ] operator call sites in `pp_code_generator_generate_rvalue_unary`
- [ ] Verify struct fields of callable type get `ptr` in `fill_type_map_complex` struct-body construction.
- [ ] Verify array/list element paths do not encounter callable kinds (they are banned).

---

## Phase 8 — Code generation: values, calls, copies, debug info

- [ ] `pp_code_generator_generate_rvalue_identifier`:
  - [ ] return the LLVM function value for callable declaration names;
  - [ ] load the pointer from the slot for function-typed variables/parameters.
  Use `is_callable_decl` or the decoration value category to distinguish.
- [ ] `pp_code_generator_generate_lvalue_identifier`: confirm reference dereference and slot addressing work for
      function-typed variables after the lowering change.
- [ ] `pp_code_generator_l_value_to_r_value`: confirm it loads a `ptr` for callable values.
- [ ] Variable declaration / assignment codegen: use `ptr`; trivial store/load path; no drop.
- [ ] Function parameter setup: allocate `ptr` slots for callable-typed parameters.
- [ ] Function call codegen: callee value is `ptr`; use `LLVMBuildCall2` with the subroutine type from the helper.
- [ ] Return of a callable value: return the `ptr` directly.
- [ ] Copy/drop: callable types are trivially copyable and drop-free; ensure fallback bitwise copy is used.
- [ ] Struct containing callable fields: copy/drop of the struct must treat those fields as trivial.
- [ ] Cast codegen: explicit `lambda[...] as func[...]` is a no-op pointer reinterpretation; ensure the cast path
      accepts callable source/target lowering to `ptr`.
- [ ] Global function-typed variable emission:
  - [ ] emit the initializer as a function/lambda pointer constant;
  - [ ] ensure the named callable is already available as an LLVM function (declaration or definition) before the
        global initializer is emitted;
  - [ ] if ordering is not trivial, pre-create LLVM declarations for named callables or ban function-typed
        globals per Phase 0.3.
- [ ] Debug info (`pp_code_generator_get_di_type`): add Function/Lambda as
      `LLVMDIBuilderCreatePointerType(pp_code_generator_di_subroutine_type(...), 64, 64, ...)`.
- [ ] Watch for recursive DI construction if a callable type is reachable from its own parameter types through a
      typedef; add/verify a recursion guard in the DI cache.

---

## Phase 9 — Extern and `.ppi`

- [ ] Confirm `extern func ...` signatures parse and type-check unchanged.
- [ ] Confirm extern emission uses the subroutine-type helper.
- [ ] Confirm a `lambda[...]` in an extern signature still carries the purity claim under the existing trust
      model.
- [ ] Add at least one extern test with a callable-typed parameter or return, or explicitly document the
      limitation.

---

## Phase 10 — Tests

### Parser
- [ ] all syntax cases from Phase 2

### Semantic (accept)
- [ ] `let f :: func[(int32) -> int32] = named_func;`
- [ ] `f(...);`
- [ ] rebinding `f = other_func;`
- [ ] `const func[...]` binding cannot be rebound
- [ ] `func[...]&` aliases and rebinds the original variable
- [ ] explicit widening: `let f :: func[...] = some_lambda as func[...];`
- [ ] callable parameter and return value
- [ ] callable struct field with mandatory initializer
- [ ] callable global with a function/lambda name initializer (if Phase 0.3 keeps globals)
- [ ] higher-order signature

### Semantic (reject)
- [ ] assignment of a function name to a reference (`func_name` is not a storage location)
- [ ] uninitialized function-typed variable
- [ ] uninitialized function-typed struct field
- [ ] arrays/lists of callable values
- [ ] `ptr[func[...]]` and `nullptr[func[...]]`
- [ ] implicit `lambda[...]` → `func[...]` initialization
- [ ] implicit `lambda[...]` → `func[...]` assignment, argument, and return
- [ ] mismatched signature
- [ ] func → lambda assignment or cast
- [ ] casting between callable and non-callable types
- [ ] calling a `func[...]` value inside a lambda
- [ ] `==` / `!=` and all other operators on callable values
- [ ] uninitialized function-typed global (if globals are allowed)

### Codegen
- [ ] `let f :: func[...] = named_func; f(args);`
- [ ] rebinding and calling again
- [ ] function value passed as an argument and called in the callee
- [ ] function value returned from a function and called
- [ ] reference to a function-typed variable
- [ ] explicit `lambda[...] as func[...]` cast and call
- [ ] function-typed global initialized from a function name (if globals are allowed)
- [ ] `-g` build emits valid debug info
- [ ] valgrind smoke test

---

## Phase 11 — Validation and cleanup

- [ ] `make pplang` succeeds with no new warnings.
- [ ] `make unittest` passes.
- [ ] `./bin/unittest` passes.
- [ ] Run demo programs to check for regressions.
- [ ] Run `valgrind --leak-check=yes ./bin/pplang <test.pp>` on new smoke cases.
- [ ] Run `clang-format -i` on touched C/H files.
- [ ] Update documentation:
  - [ ] Verify the implemented grammar matches the already-updated `notes/bnf.md` from Phase 0.4.
  - [ ] `docs/language/01-types.md`
  - [ ] `docs/language/03-declarations.md`
  - [ ] `docs/language/04-expressions.md`
  - [ ] `docs/language/07-functions-and-lambdas.md`
  - [ ] `docs/reference/grammar.md`
  - [ ] `docs/reference/known-limitations.md` (arrays/lists, `ptr[func[...]]`, comparisons, explicit-only
        lambda → func widening)
  - [ ] `dev-docs/parser-ast-mapping.md`
  - [ ] `notes/type_system.md` if callable identity rules belong there
- [ ] Re-check the recorded Phase 0.3 decisions against the implementation; update if the global fallback was
      taken.
- [ ] Review the final diff for scope and consistency.

---

## Reference: affected files and key functions

| Area | File | Key functions |
| --- | --- | --- |
| AST | `include/pp_ast.h`, `src/pp_ast.c` | node enum, `PP_AST_TypeCallableNode`, free/print |
| Parser | `src/pp_parser.c` | `pp_parser_parse_collection_type`, `pp_parser_create_type`, `pp_parser_create_param_decl` |
| Types | `include/pp_type.h`, `src/pp_type.c`, `src/pp_type_table.c` | `pp_type_decay`, `pp_type_decay_cg`, `pp_type_is_trivially_copyable_impl`, `pp_type_equal` |
| Symbols | `include/pp_symbol.h`, `src/pp_symbol.c` | `PPSymbol.is_callable`, `pp_symbol_create` |
| Semantics | `src/pp_semantic_analyzer.c` | `pp_type_from_type_node`, `pp_type_contains_reference_impl`, `pp_type_validate_collection_element`, identifier/assignment/return/call/struct/global/cast checkers |
| Codegen | `src/pp_code_generator.c` | `pp_backend_lower_type`, `pp_code_generator_fill_type_map_complex`, `pp_code_generator_generate_rvalue_identifier`, `pp_code_generator_generate_lvalue_function_call`, `pp_code_generator_emit_function`, `pp_code_generator_get_di_type`, `pp_code_generator_di_subroutine_type` |
| Tests | `tests/parsertest.cpp`, `tests/semantictest.cpp`, `tests/codegentest.cpp` | add cases per Phase 10 |
| Docs | `docs/`, `dev-docs/`, `notes/` (including `notes/bnf.md` grammar) | see Phase 11 |

---

## Suggested implementation order

0. Phase 0.4: update and agree on `notes/bnf.md` before any implementation.
1. Phase 1–2: AST + parser.
2. Phase 3: type resolution.
3. Phase 4: decay and predicates (otherwise semantic checks assert).
4. Phase 5: symbol/value-category distinction.
5. Phase 6: semantic rules and restrictions.
6. Phase 7–8: codegen lowering split and value handling.
7. Phase 9: extern validation.
8. Phase 10–11: tests, validation, docs.

---

## Notes

- No new `PPTypeKind` is introduced. "First-class function object" means the existing callable signature type is
  also used as a value type; only codegen distinguishes signature lowering from value lowering.
- Function values are pointer-sized, trivially copyable, and never need dropping.
- There is no null function value. Every callable-typed binding and struct field must be initialized.
- No closures/captures are involved, so no lifetime analysis is needed.
- Lambda → func conversion is explicit-only via `as func[...]` with an identical signature. There is no implicit
  widening anywhere.
- `ptr[func[...]]` is rejected in v1. References (`func[...]&`) remain allowed.
- Comparisons and arithmetic/bitwise operators are rejected for callable values.
- Keep this checklist updated as implementation proceeds.
