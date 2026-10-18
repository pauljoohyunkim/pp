# Collection Type Syntax Migration – Review Guide

Review all changes in the `collection-type-syntax-change` branch. Check each item as you verify it.

## 1. Documentation
- [x] `notes/bnf.md` – `<collection_type>` is prefix form ` "[" <type> [ ":" <expression> ] "]" | <base_type>`
- [x] BNF no longer allows suffix `[ ]` on base types
- [x] `notes/type_system.md` – Array/List section references prefix syntax `const [int32 : 4u]&`
- [x] `notes/pp_type_notes.md` – internal signatures show `Array(Size=5, Const(Base(Int32)))` for const collections, matching canonicalisation
- [x] No other notes files contain outdated suffix array specifier references

## 2. AST / Parser
- [ ] `include/pp_ast.h` – `PP_AST_TypeBaseNode`, `PP_AST_TypeArrayNode`, `PP_AST_TypeListNode` exist and are used
- [ ] `src/pp_parser.c` – parser emits recursive type expression for `[T : N]` and `[T]`
- [ ] `tests/parsertest.cpp` – all tests migrated to prefix syntax
  - [ ] `ParserTest.NestedArrayType` – `[[int32 : 3u] : 4u]`
  - [ ] `ParserTest.ArrayReferenceType` – `[int32 : 4u]&`

## 3. Semantic Analyzer – Type Construction
- [ ] `src/pp_semantic_analyzer.c` – `pp_type_from_type_node` handles recursive collection nodes
- [ ] Canonical const normalisation implemented
  - [ ] `const [T : N]` is pushed inward to `[const T : N]`
  - [ ] Nested arrays push const recursively
  - [ ] No double `Const(Const(...))`
- [ ] Element reference ban enforced
  - [ ] `pp_type_validate_collection_element` rejects element types containing references
  - [ ] `SemanticTest.IllegalElementReferenceArray` passes: `[int32& : 4u]` rejected

## 4. Semantic Analyzer – Collection Initializers
- [ ] `pp_semantic_analyzer_check_collection_initializer_expression` re-enabled from HEAD
- [ ] Element type resolved via `pp_type_from_type_node`
- [ ] Array size computed via `pp_array_size_from_simple_expr`
- [ ] Per-element constexpr and type matching enforced
- [ ] `SemanticTest.CollectionInitializerSuccessCriteria` passes

## 5. Type System
- [ ] `src/pp_type_table.c` – `pp_type_equal` handles Array/List element equality
- [ ] `pp_type_decay` descends through Array/List elements, strips Const recursively
- [ ] Decay stops at nominal types: Struct, Newtype

## 6. Tests
- [ ] `tests/semantictest.cpp`
  - [ ] `StructCycleViaArray` – `A[1]` → `[A : 1u]`
  - [ ] `StructCycleViaConstArray` – `const A[1]` → `const [A : 1u]`
  - [ ] `SemanticTest.NestedArrayTypeValid` passes
  - [ ] All 38 Semantic tests pass
- [ ] `tests/parsertest.cpp` – all parser tests pass
- [ ] Full suite: `make bin/unittest && bin/unittest --gtest_filter="*Parser*:*Semantic*"` → 172/172

## 7. Clean-up
- [ ] `#warning` directives removed from `src/pp_semantic_analyzer.c`
- [ ] Unused label warnings noted but non-blocking

## 8. Migration Plan
- [ ] `migration_plan_collection_type_syntax.md` progress section updated and accurate
- [ ] Design decisions documented: const push inward, decay structural vs nominal

---

**Review command**
```bash
git log --oneline collection-type-syntax-change
make bin/unittest
bin/unittest --gtest_filter="*Parser*:*Semantic*"
```
