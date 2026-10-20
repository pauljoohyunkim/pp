# Newtype Hardening Migration – Review Guide

Review all changes in the `migration-newtype-hardening` branch. Check each item as you verify it.

## 1. Documentation
- [ ] `migration-newtype-hardening.md` exists and step plan is accurate
- [ ] Step 1-5 marked `(ready for review)`
- [ ] Risks section notes string interning for `name` field
- [ ] No other notes contain outdated newtype-alias assumptions

## 2. Pass 1 – Newtype Node Creation
- [x] `src/pp_semantic_analyzer.c` `pp_semantic_analyzer_build_global_table_newtype_decl`
  - [x] Allocates `PP_TK_Newtype` node via `pp_type_create`
  - [x] Sets `name` and `underlying` correctly
  - [x] Interns node via `pp_global_type_table_intern`
  - [x] Registers named entry with `pp_global_type_table_register_named`
  - [x] Duplicate name check remains
  - [x] Base type const/reference check remains
  - [x] Function/Lambda base rejection added
  - [x] Proper cleanup on error via `pp_type_free`

## 3. Machine-type Helper
- [x] `src/pp_type_table.c` `pp_type_resolve_machine_type`
  - [x] Walks `PP_TK_Newtype` chain until non-newtype
  - [x] Handles NULL input safely
  - [x] Declared `const PPType*`

## 4. Cast Expression Hardening
- [x] `src/pp_semantic_analyzer.c` `pp_semantic_analyzer_check_cast_expression`
  - [x] Decays both expression and target types
  - [x] Resolves machine types via `pp_type_resolve_machine_type`
  - [x] Allows cast if machine types equal or both numerical
  - [x] Reject cast to reference unchanged
  - [x] Checklist updated to reflect machine-type resolution

## 5. Type Equality / Decay
- [ ] `src/pp_type_table.c` `pp_type_equal` for `PP_TK_Newtype` compares by name only – nominal
- [ ] `pp_type_decay` returns Newtype unchanged – nominal stops
- [ ] Newtype chain not walked by decay

## 6. Tests – Newtype Chain
- [ ] `tests/semantictest.cpp` `SemanticTest.NewtypeChainNominalDistinct`
  - [ ] Assignment between `AGE` and `WEIGHT` errors without cast
  - [ ] Passes
- [ ] `SemanticTest.NewtypeChainCastAllowed`
  - [ ] Cast `AGE` → `WEIGHT` succeeds via machine type
  - [ ] Passes
- [ ] `SemanticTest.NewtypeChainCastToUnderlying`
  - [ ] Cast `WEIGHT` → `int32` succeeds
  - [ ] Passes
- [ ] Full suite `bin/unittest --gtest_filter="*Semantic*"` passes, no regressions

## 7. Manual Smoke Tests
- [ ] `/tmp` sample with `newtype int32 AGE; newtype AGE WEIGHT;`
  - [ ] `let a :: AGE = 5 as AGE;` succeeds
  - [ ] `let w :: WEIGHT = a;` errors
  - [ ] `let w :: WEIGHT = a as WEIGHT;` succeeds
  - [ ] Type table shows distinct pointers for AGE and WEIGHT

## 8. Clean-up & Safety
- [ ] No `abort()` on unsupported newtype bases
- [ ] No memory leaks on error paths
- [ ] `SA-P2-TODO.md` note about living checklist present

---

**Review commands**
```bash
git log --oneline migration-newtype-hardening
make bin/unittest
bin/unittest --gtest_filter="*Semantic*"
# manual smoke
/home/pbjk/pp/bin/pplang /tmp/newtype_test_simple.pp
/home/pbjk/pp/bin/pplang /tmp/newtype_test_fail.pp
```
