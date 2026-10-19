# Collection Type Syntax Migration – Review Guide

Review all changes in the `collection-type-syntax-change` branch. Check each item as you verify it.

## 1. Documentation
- [x] `notes/bnf.md` – `<collection_type>` is prefix form ` "[" <type> [ ":" <expression> ] "]" | <base_type>`
- [x] BNF no longer allows suffix `[ ]` on base types
- [x] `notes/type_system.md` – Array/List section references prefix syntax `const [int32 : 4u]&`
- [x] `notes/pp_type_notes.md` – internal signatures show `Array(Size=5, Const(Base(Int32)))` for const collections, matching canonicalisation
- [x] No other notes files contain outdated suffix array specifier references

## 2. AST / Parser
- [x] `include/pp_ast.h` – `PP_AST_TypeBaseNode`, `PP_AST_TypeArrayNode`, `PP_AST_TypeListNode` exist and are used; legacy `array_spec` field removed
- [x] `src/pp_parser.c` – parser emits recursive type expression for `[T : N]` and `[T]`
- [x] `tests/parsertest.cpp` – all tests migrated to prefix syntax
  - [x] `ParserTest.NestedArrayType` – `[[int32 : 3u] : 4u]`
  - [x] `ParserTest.ArrayReferenceType` – `[int32 : 4u]&`

## 3. Semantic Analyzer – Type Construction
- [x] `src/pp_semantic_analyzer.c` – `pp_type_from_type_node` handles recursive collection nodes
- [x] Canonical const normalisation implemented
  - [x] `const [T : N]` is pushed inward to `[const T : N]`
  - [x] Nested arrays push const recursively
  - [x] No double `Const(Const(...))`
- [x] Element reference ban enforced
  - [x] `pp_type_validate_collection_element` rejects element types containing references
  - [x] `SemanticTest.IllegalElementReferenceArray` passes: `[int32& : 4u]` rejected

## 4. Semantic Analyzer – Collection Initializers
- [x] `pp_semantic_analyzer_check_collection_initializer_expression` re-enabled from HEAD
- [x] Element type resolved via `pp_type_from_type_node`
- [x] Array size computed via `pp_array_size_from_simple_expr`
- [x] Per-element constexpr and type matching enforced
- [x] `SemanticTest.CollectionInitializerSuccessCriteria` passes

## 5. Type System
- [x] `src/pp_type_table.c` – `pp_type_equal` handles Array/List element equality
- [x] `pp_type_decay` descends through Array/List elements, strips Const recursively
- [x] Decay stops at nominal types: Struct, Newtype

## 6. Tests
- [x] `tests/semantictest.cpp`
  - [x] `StructCycleViaArray` – `A[1]` → `[A : 1u]`
  - [x] `StructCycleViaConstArray` – `const A[1]` → `const [A : 1u]`
  - [x] `SemanticTest.NestedArrayTypeValid` passes
  - [x] All Semantic tests pass
- [x] `tests/parsertest.cpp` – all parser tests pass
- [x] Full suite: `make bin/unittest && bin/unittest --gtest_filter="*Parser*:*Semantic*"` → 176/176

## 7. Clean-up
- [x] `#warning` directives removed from `src/pp_semantic_analyzer.c`
- [x] Unused label warnings noted but non-blocking

## 8. Migration Plan
- [x] `migration_plan_collection_type_syntax.md` progress section updated and accurate
- [x] Design decisions documented: const push inward, decay structural vs nominal

---

**Review command**
```bash
git log --oneline collection-type-syntax-change
make bin/unittest
bin/unittest --gtest_filter="*Parser*:*Semantic*"
```
