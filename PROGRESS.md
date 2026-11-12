# PROGRESS

## Current focus
Pointer support v1 → unblock C interop / stdlib. Debug `-g` deferred to later session.

Pointer const-correctness implementation completed 2026-01-11.
- `pp_type_is_top_level_const` + `pp_type_can_assign` added to semantic analyzer
- Assignment compatibility uses const matrix: `ptr[T]` → `ptr[const T]` allowed, const stripping prevented
- Casting to reference banned; `ref[T] as ptr[T]`, `ptr[T] as ptr[U]` with const matrix, `uint64 as ptr[T]` allowed
- Assignment mutability checks top-level const and const inside reference `const T&`
- `nullptr[T]` semantics corrected to `nullptr[T]` → `ptr[T]` with pointee type argument
- Semantic unit tests for pointer const-correctness added to tests/semantictest.cpp
- All 342 unit tests pass

Decision log:
- Mutual exclusion: pointer and reference types are mutually exclusive.
- Syntax: `ptr[T]` e.g. `ptr[uint8]`
- Const matrix: both pointee constness and pointer constness supported: `const ptr[T]` and `ptr[const T]`
- `@x` address-of from l-value allowed, `$x` dereference.
- Nullable by default. `nullptr[T]` required, `NULL` removed.
- Pointer arithmetic deferred to later iteration; v1 supports subscripting `p[n]`.
- Lambdas: no pointers allowed at all. Pointers not permitted inside lambda bodies.
- Pointer-to-pointer allowed: `ptr[ptr[T]]` supported in v1.
- Casting to reference not allowed.
- Casting from reference to pointer allowed: `ref[T] as ptr[T]` permitted.
- Pointer-to-pointer casts allowed.
- Integer to pointer casts allowed via `uint64 as ptr[T]`, pointer width is `uint64`.
- Deref `$` and address-of `@` are distinct operators, safety-signalling, precedence to be defined.

Open ambiguities to confirm:
- [x] Zero length arrays forbidden – already enforced in pp_semantic_analyzer.c
- [x] Pointer width = uint64 for 64-bit v1
- [x] `const ptr[T] as ptr[T]` not needed; pointer const removal not required
- [x] Precedence of `@`/`$` same as `!`, right-associative
- [ ] Finalize error location for extern lambda signature checks – decide during semantic analyzer implementation
- [ ] Confirm deref `*` precedence details and parsing associativity.


## Pointer Support v1

### Design & Docs
- [x] Finalize pointer design doc `notes/pointers_design.md` from `pointers_plan.md`
- [x] Decide nullable policy and document
- [x] Decide pointer arithmetic policy and document
- [x] Decide lambda address-of/deref policy and document
- [ ] Update `notes/general_coding_rule.md` with pointer safety notes

### Type System
- [x] Add `PP_TK_Pointer` to type kind enum
- [x] Extend `pp_type.c` with pointer creation, printing, equality
- [x] Add `type_contains_pointer` predicate, mirror `type_contains_reference`
- [ ] Const propagation for pointer: `const ptr[T]` vs `ptr[const T]`
- [ ] Disallow mixing pointer and reference in same type; enforce mutual exclusion in semantic analyzer
- [ ] Update type table, mangling, and `pp_type_table.c` cases

### Lexer / Parser
- [x] Lexer: add PP_TT_AT, PP_TT_DOLLAR, PP_TT_PTR, PP_TT_NULLPTR tokens
- [x] Lexer tests for @/$/ptr/nullptr
- [x] Parser: `ptr[T]` parsing via PP_AST_TypePointerNode
- [x] Parser: `@`/`$` unary parsing
- [x] Parser: `nullptr`/`nullptr[T]` parsing via PP_AST_NullLiteralNode
- [ ] Lexer: pointer syntax `ptr[` `]` as type token sequence
- [ ] Parser: precedence for `@`,`$` already same as unary
- [ ] AST nodes for pointer type, address-of, deref

### Semantic Analyzer
- [x] Type checking for `ptr[T]` construction
- [x] Validate address-of only on l-values
- [x] Validate deref only on `ptr[T]`
- [x] Null literal type checking and nullptr requires pointer type
- [x] Pointer equality comparisons
- [x] Prevent pointer/reference mixing in type construction
- [x] Const correctness for pointee const and pointer const
- [x] Assignment mutability for const-inside-reference enforced
- [x] Collection element predicates updated for pointers
- [x] Ensure `@` and `$` are not user-overloadable

### Code Generator
- [x] 1. Lower `ptr[T]` to LLVM pointer type – extend `pp_type_to_llvm_type` / `pp_code_generator_fill_type_map`
- [x] 2. Emit address-of `@` – `LLVMBuildAlloca` / GEP for lvalue, return pointer
- [x] 3. Emit dereference `$` – `LLVMBuildLoad2` for `ptr[T]`
- [ ] 4. Emit pointer subscript `p[n]` – `LLVMBuildGEP2` + load
- [ ] 5. Lower `nullptr[T]` to LLVM null pointer constant
- [ ] 6. Emit pointer casts: `ref as ptr`, `ptr as ptr`, `uint64 as ptr`, `ptr as uint64` via `BitCast` / `IntToPtr` / `PtrToInt`
- [ ] 7. Null comparisons `p == nullptr` → `ICmp`
- [ ] 8. Copy/move semantics – bitwise copy for pointers, no auto-drop of pointees
- [ ] 9. Interop with `pp_std` pointer parameters – ensure extern signatures match


### Tests
- [ ] Parser tests for `ptr[T]` types
- [ ] Semantic tests for address-of, deref, const matrix
- [ ] IR tests for address-of/load/store/GEP
- [ ] Runtime smoke tests vs `pp_std`
- [ ] Negative tests: pointer/reference mixing, invalid address-of

### Docs
- [ ] Update `docs/` with pointer syntax and const matrix
- [ ] Add pointer safety notes to `docs/14-known-limitations.md`

## Debug mode `-g` – deferred to later session/iteration
- [ ] Deferred

## Notes
Tick items as completed. Ambiguities listed above need decision before semantic/codegen work.
