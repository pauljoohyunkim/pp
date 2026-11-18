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
Note: until Phase 3 lands, semantic analysis reports "Unsupported type node" for any `func[...]`/`lambda[...]`
— expected.

---

## Phase 3 — Type resolution

- [ ] In `pp_type_from_type_node` (`src/pp_semantic_analyzer.c`), accept `PP_AST_TypeCallableNode`.
- [ ] Resolve each parameter type and the return type with `pp_type_from_type_node`.
- [ ] Create `PP_TK_Function` or `PP_TK_Lambda` according to the node's `kind`.
- [ ] Push parameter types via `pp_type_push_param`, then set `return_type`.
- [ ] Intern the type in the global type table and free the temporary on failure.
- [ ] Validate parameter and return types as needed (`void` return is allowed; `void` parameter should be
      rejected).
- [ ] Confirm named functions produce a structurally identical `PP_TK_Function` type for the same signature, so
      `let f :: func[...] = predef_func;` type-checks exactly.
- [ ] Confirm `const func[...]` and `func[...]&` create the usual `PP_TK_Const` / `PP_TK_Reference` wrappers.
- [ ] Confirm `lambda[(...)]` and `func[(...)]` of the same signature are distinct types.

---

## Phase 4 — Type-system predicates (hard blocker)

- [ ] `pp_type_decay` (`src/pp_type_table.c`): return Function/Lambda/Operator unchanged. Do **not** recurse into
      signature parameters/return; const/ref inside a signature is part of type identity.
- [ ] `pp_type_decay_cg` (`src/pp_type_table.c`): same.
- [ ] `pp_type_is_trivially_copyable_impl` (`src/pp_type.c`): return `true` for Function/Lambda/Operator.
- [ ] `pp_type_contains_reference_impl` (`src/pp_semantic_analyzer.c`): return `false` for Function/Lambda
      instead of traversing the signature. A function value does not store a reference.
- [ ] Keep the Function/Lambda ban in `pp_type_validate_collection_element` (arrays/lists) for v1.
- [ ] Confirm `pp_type_needs_drop` remains `false` for callable types.
- [ ] Confirm `pp_type_equal` requires no change.

---

## Phase 5 — Symbol and identifier semantics

- [ ] Add `bool is_callable;` to `PPSymbol` (`include/pp_symbol.h`).
- [ ] Set `is_callable = true` only for function/lambda/operator declaration symbols in the global-table build
      paths.
- [ ] Update `pp_symbol_create` signature and all call sites.
- [ ] In `pp_semantic_analyzer_check_identifier_expression`:
  - [ ] callable declaration names remain `RVALUE`;
  - [ ] function-typed variables/parameters/fields become `LVALUE`.
- [ ] Verify `f = g;` is accepted for a mutable function-typed variable.
- [ ] Verify `let h :: func[...]& = f;` is accepted (and rejected when `f` is a function name, which is not a
      storage location).
- [ ] Verify `let c :: const func[...] = ...;` rejects rebinding.
- [ ] Verify lambda-body restrictions still reject global mutable state and regular-function calls where intended.
- [ ] Verify a call through a value inside a lambda is allowed only for `lambda[...]`.

---

## Phase 6 — Semantic checks

- [ ] Variable declaration (`pp_semantic_analyzer_full_analyze_variable_declaration`):
  - [ ] require an initializer when the type is callable (no null function value);
  - [ ] confirm const/reference-initializer rules still hold;
  - [ ] require exact type match after decay; no implicit lambda → func widening.
- [ ] Assignment statement:
  - [ ] decay handles callable types (Phase 4);
  - [ ] exact type match; no implicit lambda → func widening.
- [ ] Return statement: exact type match; no implicit widening.
- [ ] Function-call argument checking: exact type match; no implicit widening.
- [ ] Function-call callee checking (`pp_semantic_analyzer_check_function_call`):
  - [ ] decay the callee type before checking `kind`;
  - [ ] support `const func[...]` and `func[...]&` callees;
  - [ ] use decayed parameter/return types;
  - [ ] preserve lambda purity restrictions based on the decayed kind.
- [ ] Struct initializer checks:
  - [ ] require an initializer for callable-typed fields (same as const/reference fields);
  - [ ] exact type match; no implicit widening.
- [ ] Struct field default initializers:
  - [ ] allow a function/lambda name as a global default initializer for a callable-typed field;
  - [ ] apply the same constexpr exception for callable names;
  - [ ] still require the initializer when the field is used in a global-scope struct instance.
- [ ] Global variable declaration:
  - [ ] allow function-typed globals with a mandatory initializer that is a function or lambda name;
  - [ ] add a narrow constexpr exception for callable declaration names (link-time constants);
  - [ ] verify codegen ordering supports the initializer (see Phase 8);
  - [ ] fall back to an explicit ban if declaration-order handling is not simple.
- [ ] Cast expression: allow **only** explicit `lambda[...]` → `func[...]` with an identical signature. Reject
      every other cast involving a callable type (func → lambda, lambda → lambda, func → func, callable ↔
      non-callable).
- [ ] Array/list element validation: keep the ban; error text unchanged.
- [ ] Pointer validation: reject `ptr[func[...]]` / `ptr[lambda[...]]` and any pointer whose pointee is
      callable. Do not affect references to function-typed variables.
- [ ] `nullptr[func[...]]` is therefore rejected because it would build `ptr[func[...]]`.
- [ ] Comparisons and all binary/unary operators: explicitly reject callable operands in v1.

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
  Use `is_callable` or the decoration value category to distinguish.
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
