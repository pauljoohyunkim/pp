# `constant` Declarations — Implementation Checklist

## Goal

Add a global declaration form

```pp
constant SOME_CONSTANT :: uint64 = 1ul;
```

A `constant` is a **compile-time named value**, not storage:

* No LLVM global / object-file symbol is emitted.
* Declaring the same `constant` in a `.ppi` that is prepended to several `.pp` translation units
  therefore cannot cause a duplicate-symbol link error.
* References are substituted with the (already analyzed) initializer expression during codegen.
* Usable in any expression, including global initializers and inside lambda bodies.

Contrast with `let X :: const uint64 = 1ul;`, which is still a real linkable global and is not
currently constexpr when referenced.

## Design Summary

* Keyword: `constant` (`PP_TT_CONSTANT`). Global scope only.
* Grammar: `"constant" <identifier> "::" <type> "=" <expression> ";"`
* **Dedicated AST node** `PP_AST_ConstantDeclarationNode` with its own union member:

  ```c
  struct { /* ConstantDeclaration */
      const char* identifier;  /* pool-owned */
      PP_AST_Node* type_expr;
      PP_AST_Node* initializer;
  } constant_declaration;
  ```

  Not folded into `variable_declaration`, to keep the two declaration kinds clearly separate.
* **Allowed types: simple primitives only.**
  - integer kinds (`int8` … `uint128`)
  - `float`
  - `bool`
  - Rejected with a clear diagnostic: `const`/`&` modifiers, enum, flag, newtype, struct, array,
    list, pointer, string, handle, callable.
* Symbol: `PPSymbol.is_constant_decl` + non-owning `const PP_AST_Node* constant_initializer`.
* Semantic: registered in the SA symbol table (needed for lookup), but never emitted by codegen.
* Value category: `RVALUE` + `is_constexpr` at each reference, so assignment / `@X` fail via
  existing lvalue checks.
* Ordering: constants are resolved in source order before function bodies. A constant
  initializer may only reference constants declared earlier; forward refs / cycles are errors.
* Array sizes: **unsigned integer constants (`uint8`…`uint64`) are allowed** as array sizes.
  Signed, `float` and `bool` constants are rejected there. Because array sizes are evaluated in
  Pass 1 (`pp_type_from_type_node` → `pp_array_size_from_simple_expr`), a constant used as an
  array size must be declared **earlier in the source**.`

---

## 1. Grammar & Docs

- [x] `notes/bnf.md`: add `<constant_decl>` to `<global_declaration>`:
      `"constant" <identifier> "::" <type> "=" <expression> ";"`
- [x] `docs/reference/grammar.md`: add `constant` to the `declaration` alternatives.
- [x] `notes/global_statement_types.md`: add a "Constant Declaration" section.
- [x] `docs/language/02-variables-and-const.md`: add a "Constants" section (link safety, no
      address-of/assignment, constexpr use, PPI use, ordering rule, primitive-only rule).
- [x] Document array-size use: unsigned constants only, declared before use.

## 2. Lexer / Tokens

- [x] `include/pp_tokens.h`: add `PP_TT_CONSTANT` (near `PP_TT_CONST`).
- [x] `src/pp_tokens.c`: add `case PP_TT_CONSTANT: return "constant";`.
- [x] `src/pp_lexer.c`: add the `"constant"` keyword branch in the identifier/keyword block
      (boundary check already prevents `const` matching `constant`).
- [x] `tests/lexertest.cpp`: assert `constant` lexes to `PP_TT_CONSTANT`, and an identifier such
      as `constantX` stays `PP_TT_IDENTIFIER`.

## 3. Parser / AST

- [x] `include/pp_ast.h`:
  - [x] Add `PP_AST_ConstantDeclarationNode` to `PP_AST_NodeType`.
  - [x] Add the `constant_declaration` union member shown above.
- [x] `src/pp_parser.c`:
  - [x] Forward-declare `pp_parser_create_constant_declaration`.
  - [x] Add a global-scope `case PP_TT_CONSTANT:` dispatch.
  - [x] Implement `pp_parser_create_constant_declaration`:
        consume `constant`; consume the identifier; consume `::`; parse the type;
        **require** `=` (diagnostic: "constant declaration requires an initializer");
        parse the expression; consume `;`; create the node with `identifier` (pool pointer),
        `type_expr`, and `initializer`.
  - [x] Add `PP_TT_CONSTANT` to `pp_parser_is_sync_token` for error recovery.
- [x] `src/pp_ast.c`:
  - [x] `pp_ast_root_free_internal`: free `constant_declaration.type_expr` and `.initializer`
        (identifier is pool-owned, do not free).
  - [x] `pp_ast_root_print`: print `ConstantDeclaration <name>` and recurse into type/initializer.
- [x] `tests/parsertest.cpp`: parse a valid constant declaration and a missing-`=` case.

## 4. Symbol Model

- [x] `include/pp_symbol.h`:
  - [x] Forward-declare `typedef struct PP_AST_Node PP_AST_Node;`.
  - [x] Add `bool is_constant_decl;` (no storage / not linkable).
  - [x] Add `const PP_AST_Node* constant_initializer;` (non-owning; points into program AST).
  - [x] Add `bool constant_value_available;` (Pass-2 ordering/cycle guard).
- [x] `src/pp_symbol.c`: no change (relies on `calloc` zero-init); fields are set directly from SA.
- [x] Confirm `pp_symbol_free` does **not** free `constant_initializer` (only `free(symbol)`).
- [x] Check for any struct copy/compare of `PPSymbol` (none; always used via pointers).

## 5. Semantic Analyzer — Pass 1

- [x] `src/pp_semantic_analyzer.c`:
  - [x] Add `pp_semantic_analyzer_build_global_table_constant_decl`.
  - [x] Add its `case PP_AST_ConstantDeclarationNode:` to the Pass-1 dispatch.
  - [x] Duplicate-name check against `sa->gst`.
  - [x] Resolve `type_expr` via `pp_type_from_type_node`.
  - [x] Reject any type that is not exactly an integer kind, `float`, or `bool`
        (e.g. `pp_type_is_numerical(t) || t->kind == PP_TK_Bool`;
        this naturally rejects `const` wrappers, newtypes, enums, flags, pointers, aggregates).
        Diagnostic: "constant '%s' must be an integer, float, or bool".
  - [x] Create the symbol with `is_constexpr = true`, `is_global = true`,
        `is_definition = true`, `is_callable_decl = false`, then set
        `symbol->is_constant_decl = true` and
        `symbol->constant_initializer = node->constant_declaration.initializer`
        (set here, in Pass 1, so array-size evaluation can recurse into it).

## 6. Semantic Analyzer — Pass 2

- [x] Add `pp_semantic_analyzer_full_analyze_constant_decl`:
  - [x] Require an initializer (defensive; parser also enforces).
  - [x] `check_expression` in `sa->gst`.
  - [x] Require `initializer_decoration->is_constexpr`.
  - [x] Require decayed type equality with the declared type.
  - [x] Keep/confirm `symbol->constant_initializer` (already set in Pass 1).
- [x] In `pp_semantic_analyzer_full_analyze`, add a constants-resolution step **before** the
      existing step 4:
  - [x] Iterate global decls in source order and fully analyze `PP_AST_ConstantDeclarationNode`
        decls only.
  - [x] Add `case PP_AST_ConstantDeclarationNode:` to the step-4 switch as a no-op (already
        handled) rather than letting it hit `default: abort()`.
  - [x] Rationale: earlier constants are resolved first; forward refs/cycles fail deterministically
        and codegen never dereferences a NULL initializer.
- [x] `pp_semantic_analyzer_check_identifier_expression`:
  - [x] Whitelist `is_constant_decl` symbols in the lambda global-access rule.
  - [x] If `is_constant_decl && !constant_value_available`, emit
        "constant '%s' is used before its value is available" (forward reference / cycle).
  - [x] Decorate as `RVALUE` with `is_constexpr = is_callable_decl || is_constant_decl`
        (do **not** change const-global constexpr behavior in this feature).

## 7. Semantic Analyzer — Array Size Integration

- [x] `pp_array_size_from_simple_expr` (src/pp_semantic_analyzer.c:682): add a
      `case PP_AST_IdentifierNode:`
  - [x] Look up the identifier in `sa->gst`; require the symbol to be `is_constant_decl`.
  - [x] Require declared type `pp_type_kind_is_unsigned_up_to_64(symbol->type->kind)`
        (`uint8`…`uint64`); reject signed/float/bool with
        "Array size constant '%s' must have an unsigned integer type (uint8..uint64)".
  - [x] Require `symbol->constant_initializer != NULL`;
        otherwise error "constant '%s' must be declared before it is used as an array size".
  - [x] Recurse: `pp_array_size_from_simple_expr(sa, symbol->constant_initializer, val)`.
- [x] This automatically covers both type array sizes and collection-initializer size checks
      (the two Pass-2 call sites at ~line 6063 and ~6579), because they reuse the same helper.
- [x] Do not add a separate integer evaluator: the existing helper already supports unsigned
      literals, `Type::Member`, and `+ * / % << >>`.

## 8. Code Generator

- [x] Global emit loop (`pp_code_generator_generate`): add
      `case PP_AST_ConstantDeclarationNode: break;` — the link-safety guarantee.
- [x] `pp_code_generator_generate_rvalue_identifier`:
  - [x] If `identifier_symbol->is_constant_decl`, assert the initializer is set, then return
        `pp_code_generator_generate_rvalue(cg, symbol_table, env_b, identifier_symbol->constant_initializer)`.
  - [x] Keep the existing callable-declaration branch after this.
- [x] `pp_code_generator_generate_lvalue_identifier`: assert `!symbol->is_constant_decl`
      (SA decoration must make this unreachable).
- [x] Confirm no debug-info variable entry is produced for constants (no emit path).

## 9. Tests (`tests/`)

Semantic tests:
- [x] Valid constant declarations for `uint64`, `float`, `bool`.
- [x] Constant used in a function body and in a global `let` initializer (constexpr propagation).
- [x] Constant referencing an earlier constant (`constant B :: uint64 = A + 1ul;`).
- [x] Array size from an unsigned constant: `constant N :: uint64 = 4ul; let a :: [int32 : N];`.
- [x] Array size from a constant expression: `constant N :: uint64 = 2ul * 2ul;`.
- [x] Array size from a signed/`float`/`bool` constant → semantic error.
- [x] Array size referencing a constant declared later → semantic error.
- [x] Missing initializer → parser error.
- [x] Non-constexpr initializer → semantic error.
- [x] Type mismatch → semantic error.
- [x] Assignment to a constant → semantic error (not an lvalue).
- [x] `@CONST` address-of → semantic error.
- [x] Rejected types: `const uint32`, `string`, `[int32 : 4u]`, struct, pointer, enum,
      flag, newtype, handle, callable.
- [x] Duplicate name → semantic error.
- [x] Forward reference between constants → semantic error.
- [x] Constant referenced from a lambda body → accepted.

Codegen tests:
- [x] `constant K :: uint64 = 42ul;` produces **no** `@K` global in the IR.
- [x] A function returning `K` returns the folded literal.
- [x] A global initializer using `K` is emitted as a folded constant.
- [x] `float` and `bool` constants fold to `double` / `i1` literals.
- [x] `LLVMVerifyModule` succeeds.

Integration / PPI test:
- [x] Create a `.ppi` with a `constant` plus two `.pp` units that both receive it via `--ppi`;
      compile both TUs and link. Verify no duplicate-symbol error and correct execution.
- [x] Sanity-check the object symbols with `nm` (no symbol named after the constant).
- [x] Verify that a regular `let` global placed in a `.ppi` still produces a duplicate-symbol
      link error (confirms the contrast).

## 10. Validation

- [x] `clang-format -i src/*.c include/*.h`
- [x] `make -j`
- [x] `bin/unittest`
- [x] Manual smoke:
      `./bin/pplang /tmp/const.pp -o /tmp/const && /tmp/const`
- [x] Valgrind leak check:
      `valgrind --leak-check=yes ./bin/pplang <file.pp>`
- [x] Update the notebook page `pp-constants-feature` with results.

## Risks / Guard Rails

- **Duplicate globals at link time**: must skip constant decls in the codegen global loop.
- **New AST node exhaustiveness**: `PP_AST_ConstantDeclarationNode` must be handled in
  `pp_ast_root_free_internal`, `pp_ast_root_print`, SA Pass-1/Pass-2 dispatch, the codegen global
  switch, and the parser dispatch. Free/print defaults silently skip children.
- **Recursive codegen**: constant A → B → A would recurse forever. Mitigated by the ordered
  resolution pass + the "used before its value is available" error.
- **Array-size recursion**: `pp_array_size_from_simple_expr` recurses into the constant's raw
  initializer AST. Forward references are rejected (NULL initializer), so recursion terminates.
- **Source ordering**: an array-size constant must appear before its use; add a test and
  document this, since Pass 1 is a single forward pass.
- **Accidental lvalue path**: ensure references are decorated `RVALUE`; assert in
  `pp_code_generator_generate_lvalue_identifier`.
- **Parser recovery**: add `PP_TT_CONSTANT` to sync tokens, otherwise a bad constant decl can
  cascade into parser errors.
- **`PPSymbol` lifetime**: `constant_initializer` is non-owning into the program AST, which
  outlives codegen; do not free it from `pp_symbol_free`.
- **PPI duplication**: the same `.ppi` passed to multiple TUs is expected to redefine the
  constant in each SA unit; this is intentional and safe because nothing is emitted.

## Acceptance Criteria

1. `constant` parses at global scope (including inside `.ppi` files).
2. Only integer/float/bool constants are accepted; all other types are rejected cleanly.
3. Unsigned integer constants (`uint8`…`uint64`) work as array sizes; signed/float/bool do not.
4. A constant has no emitted symbol and no storage; references fold to a constant value.
5. A `.ppi`-declared constant linked into multiple TUs causes no duplicate-symbol error.
6. Misuse (write, address-of, duplicate, non-constexpr, forward ref, bad type, signed array
   size) is rejected with clear diagnostics and non-zero compile status.
7. `make -j`, `bin/unittest`, and the Valgrind check pass with no regressions.
