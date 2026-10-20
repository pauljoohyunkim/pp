# Newtype Hardening Migration – Review Guide

Review all changes in the `migration-newtype-hardening` branch. Check each item as you verify it.

## 1. Documentation
- [ ] `migration-newtype-hardening.md` exists and step plan is accurate
- [ ] Step 1-5 marked `(ready for review)`
- [ ] Risks section notes string interning for `name` field
- [ ] No other notes contain outdated newtype-alias assumptions

## 2. Pass 1 – Newtype Node Creation
- [ ] `src/pp_semantic_analyzer.c` `pp_semantic_analyzer_build_global_table_newtype_decl`
  - [ ] Allocates `PP_TK_Newtype` node via `pp_type_create`
  - [ ] Sets `name` and `underlying` correctly
  - [ ] Interns node via `pp_global_type_table_intern`
  - [ ] Registers named entry with `pp_global_type_table_register_named`
  - [ ] Duplicate name check remains
  - [ ] Base type const/reference check remains
  - [ ] Function/Lambda base rejection added
  - [ ] Proper cleanup on error via `pp_type_free`

## 3. Machine-type Helper
- [ ] `src/pp_type_table.c` `pp_type_resolve_machine_type`
  - [ ] Walks `PP_TK_Newtype` chain until non-newtype
  - [ ] Handles NULL input safely
  - [ ] Declared `const PPType*`

## 4. Cast Expression Hardening
- [ ] `src/pp_semantic_analyzer.c` `pp_semantic_analyzer_check_cast_expression`
  - [ ] Decays both expression and target types
  - [ ] Resolves machine types via `pp_type_resolve_machine_type`
  - [ ] Allows cast if machine types equal or both numerical
  - [ ] Reject cast to reference unchanged
  - [ ] Checklist updated to reflect machine-type resolution

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
