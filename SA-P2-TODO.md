# Semantic Analyzer Pass 2 – TODO

Order is recommended execution order. Each major area gets its own heading.

> Note: Checklists are living documents. Items may be marked `[-]` with `->` explanations when a design decision deviates from the original CZ checklist. The list can be modified as implementation proceeds.

## 1. Member Access Expression
- [x] Implement `pp_semantic_analyzer_check_member_access_expression`
  - [x] Resolve base expression via `pp_semantic_analyzer_check_expression`
  - [x] Decay base type to nominal `Struct`/`Newtype`
  - [x] Lookup field name in struct layout; error if not found
  - [x] Propagate const/value category from base and field
  - [x] Reject member access on non-nominal types
  - [x] Create decoration with resolved field type, value category, is_constexpr, escapable_ref
- [x] Add checklist comment block above function
- [x] Unit test: struct member read/write, const propagation, missing field error

## 2. Array / List Indexing
- [x] Wire `PP_AST_ArrayAccessNode` in `pp_semantic_analyzer_check_expression`
- [x] Implement `pp_semantic_analyzer_check_array_access_expression`
  - [x] Resolve collection base expression
  - [x] Verify base is `Array`/`List` after decay
  - [x] Resolve index expression, require integral type and constexpr where needed
  - [x] Decay element type and create decoration for access result
  - [x] Reference ban for collection element types already enforced at type construction
- [x] Add checklist comment block
- [x] Unit tests: array index valid/invalid type, list index, const propagation

## 3. Binary / Unary Expression Hardening
- [x] Complete error handling TODOs in binary expression checker
  - [x] Type compatibility and promotion rules
  - [x] Const propagation for results
- [x] Complete error handling TODOs in unary expression checker
- [x] Ensure `pp_type_decay` is used consistently for operand comparison
- [x] Unit tests for const arithmetic
- [-] Unit tests for reference ban on operands -> PP allows references in binary/unary via decay; reference ban is enforced at type construction/assignment sites, not on arithmetic operands

## 4. Cast Expression
- [x] Finish `pp_semantic_analyzer_check_cast_expression`
  - [x] Validate target type exists
  - [x] Allow only permitted casts: nominal ↔ underlying, const stripping, decay
  - [x] Reject reference-to-value casts and unsafe downcasts
  - [x] Propagate const/value category correctly
- [x] Checklist + unit tests

## 5. Function Call / Lambda Invocation
- [x] Complete argument type checking with decayed comparison
- [x] Enforce const-correctness for reference parameters
- [-] Handle default arguments and varargs if applicable -> Not supported in current PP language (no defaults/varargs)
- [x] Verify return type decoration

## 6. Struct Initializer Finalization
- [x] Re-base struct initializer hardening from `sa-pass-2-struct-hardening`
  - [x] Duplicate field detection – done
  - [x] Field initializer type checking with decay – verify (field types compared directly; decay not needed for nominal types)
  - [x] Reference field default initializer ban – verify (implemented)
  - [-] Struct cycle detection for recursive types -> Not constructible in PP (forward refs disallowed)

## 7. Global / Variable / Function Declaration Pass 2 Completeness
- [x] `pp_semantic_analyzer_full_analyze_struct_decl_fields` – layout built with correct const handling
- [x] `pp_semantic_analyzer_full_analyze_global_variable_decl` – constexpr enforcement for globals
- [x] Ensure all Pass 2 checklists are marked complete in comments (including struct‑cycle detection marked N/A)

## 8. Test Coverage
- [ ] Add semantic tests for member access, array access, cast
- [ ] Add negative tests for reference ban, type mismatches
- [ ] Keep full suite green: `make bin/unittest && bin/unittest --gtest_filter="*Parser*:*Semantic*"`

## 9. Clean-up
- [ ] Remove remaining `#warning` directives
- [ ] Remove unused labels and TODO dead code
- [ ] Run clang-format on modified files

Notes:
- Keep collection initializer work as-is; oversize check is now enforced, undersize allowed.
- Reference ban for collections is enforced at type construction via `pp_type_validate_collection_element` and `pp_type_contains_reference`.
- Prefer conditional else over non-error goto; keep `goto error_cleanup` only for error paths.
- Reference predecessor `../cz/src/cz_semantic_analyzer.c` for check patterns:
  * Member access: base decay → nominal check → field lookup → const/value-category propagation.
  * Array access: base must be Array/List after decay, index integral constexpr, result = decayed element type.
  * Cast: enumerate allowed casts, reject reference-to-value and unsafe nominal casts.
  * Binary/Unary: use `pp_type_decay` for operand comparison, const-propagation table.
  * Struct initializer: duplicate field, decayed type compare, reference-field lvalue rule, default vs explicit initializer.
  Adapt conditions to PP types; CZ uses different AST/type layout.
