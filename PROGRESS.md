# PROGRESS

## Current focus
Pointer support v1 → unblock C interop / stdlib. Debug `-g` deferred to later session.

Decision log:
- Mutual exclusion: pointer and reference types are mutually exclusive.
- Syntax: `ptr[T]` e.g. `ptr[uint8]`
- Const matrix: both pointee constness and pointer constness supported: `const ptr[T]` and `ptr[const T]`
- `@x` address-of from l-value allowed, `$x` dereference.
- Nullable by default. `nullptr[T]` required, `NULL` removed.
- Pointer arithmetic deferred to later iteration; v1 supports subscripting `p[n]`.
- Lambdas: no pointers allowed at all. Pointers not permitted inside lambda bodies.
- Pointer-to-pointer allowed: `ptr[ptr[T]]` supported in v1.
- Casting is explicit only, no implicit ref↔ptr conversions.
- Casting to reference allowed from pointer: `ptr[T] as T&` permitted.
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
- [ ] Null checks per nullable policy
- [x] Prevent pointer/reference mixing in type construction
- [ ] Const correctness for pointee const and pointer const
- [ ] Collection element predicates updated for pointers
- [x] Ensure `@` and `$` are not user-overloadable

### Code Generator
- [ ] Lower `ptr[T]` to LLVM pointer type
- [ ] Emit address-of, load/store
- [ ] Emit GEP for `p[n]` subscripting
- [ ] Null comparisons
- [ ] No auto-drop for pointed-to memory; bitwise copy for pointers
- [ ] Interop with `pp_std` pointer parameters

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
