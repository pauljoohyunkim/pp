# Semantic Analyzer Pass 2 – TODO

Order is recommended execution order. Each major area gets its own heading.

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
- [ ] Wire `PP_AST_ArrayAccessNode` in `pp_semantic_analyzer_check_expression`
- [ ] Implement `pp_semantic_analyzer_check_array_access_expression`
  - [ ] Resolve collection base expression
  - [ ] Verify base is `Array`/`List` after decay
  - [ ] Resolve index expression, require integral type and constexpr where needed
  - [ ] Decay element type and create decoration for access result
  - [ ] Reference ban for collection element types already enforced at type construction
- [ ] Add checklist comment block
- [ ] Unit tests: array index valid/invalid type, list index, const propagation

## 3. Binary / Unary Expression Hardening
- [ ] Complete error handling TODOs in binary expression checker
  - [ ] Type compatibility and promotion rules
  - [ ] Const propagation for results
- [ ] Complete error handling TODOs in unary expression checker
- [ ] Ensure `pp_type_decay` is used consistently for operand comparison
- [ ] Unit tests for const arithmetic, reference ban on operands

## 4. Cast Expression
- [ ] Finish `pp_semantic_analyzer_check_cast_expression`
  - [ ] Validate target type exists
  - [ ] Allow only permitted casts: nominal ↔ underlying, const stripping, decay
  - [ ] Reject reference-to-value casts and unsafe downcasts
  - [ ] Propagate const/value category correctly
- [ ] Checklist + unit tests

## 5. Function Call / Lambda Invocation
- [ ] Complete argument type checking with decayed comparison
- [ ] Enforce const-correctness for reference parameters
- [ ] Handle default arguments and varargs if applicable
- [ ] Verify return type decoration

## 6. Struct Initializer Finalization
- [ ] Re-base struct initializer hardening from `sa-pass-2-struct-hardening`
  - [ ] Duplicate field detection – done
  - [ ] Field initializer type checking with decay – verify
  - [ ] Reference field default initializer ban – verify
  - [ ] Struct cycle detection for recursive types
- [ ] Consolidate checklists and unit tests from that branch

## 7. Global / Variable / Function Declaration Pass 2 Completeness
- [ ] `pp_semantic_analyzer_full_analyze_struct_decl_fields` – ensure layout built with correct const
- [ ] `pp_semantic_analyzer_full_analyze_global_variable_decl` – initializer constexpr enforcement for globals
- [ ] Ensure all Pass 2 checklists are marked complete in comments

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
