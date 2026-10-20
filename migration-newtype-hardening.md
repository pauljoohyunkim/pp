# Migration: Newtype Hardening

## Goal
Make `newtype` nominal and chainable. Currently `newtype` declarations are registered as aliases to the base type, collapsing chains such as `newtype int32 AGE; newtype AGE WEIGHT;` to `int32`. The migration creates a distinct `PP_TK_Newtype` entry per declaration, preserves a one-level `underlying` pointer, and provides a machine-type helper for casts/codegen.

## Current behavior
* `pp_semantic_analyzer_build_global_table_newtype_decl` resolves base type via `pp_type_from_type_node` and registers the base pointer directly:
  ```c
  pp_global_type_table_register_named(gtt, name, base_type);
  ```
* No `PP_TK_Newtype` node is created. Newtype names are aliases to the underlying type.
* Chain collapses: `WEIGHT` → `AGE` → `int32` becomes `WEIGHT` → `int32`.

## Desired behavior
* Each `newtype` declaration creates a distinct `PP_TK_Newtype` node:
  ```c
  Newtype { name: "WEIGHT", underlying: &AGE_entry }
  ```
* `named_types["WEIGHT"]` points to the Newtype node, not to the underlying type.
* Nominal typing is preserved: `AGE`, `WEIGHT`, `int32` are distinct.
* Machine type is obtained by walking the Newtype chain:
  ```c
  while (t && t->kind == PP_TK_Newtype) t = t->type.pp_type_newtype.underlying;
  ```
* Cast legality is based on machine-type equality, not nominal equality.

## Step-by-step plan

### 1. Create Newtype node in Pass 1
File: `src/pp_semantic_analyzer.c`, `pp_semantic_analyzer_build_global_table_newtype_decl`
* After resolving `base_type`:
  1. Check duplicate name against `named_types` and alias table.
  2. Check base type is not const/reference.
  3. Allocate `PPType* nt = pp_type_create(PP_TK_Newtype)`.
  4. Set `nt->type.pp_type_newtype.underlying = base_type`.
  5. Set `nt->type.pp_type_newtype.name = identifier` (copy string, manage lifetime or store pointer to interned name).
  6. Intern `nt` via `pp_global_type_table_intern`.
  7. Register named entry: `pp_global_type_table_register_named(gtt, identifier, interned)`.

### 2. Machine-type helper
File: `src/pp_type_table.c` or `src/pp_semantic_analyzer.c`
* Add `static const PPType* pp_type_resolve_machine_type(const PPType* t)`.
* Loop while `t && t->kind == PP_TK_Newtype` → `t = t->type.pp_type_newtype.underlying`.
* Return final type.

### 3. Update cast checker
File: `src/pp_semantic_analyzer.c`, `pp_semantic_analyzer_check_cast_expression`
* Replace one-level unwrap with machine-type helper.
* Validate target type exists.
* Allow cast if `pp_type_resolve_machine_type(expr_type)` and `pp_type_resolve_machine_type(target_type)` are compatible / share same machine type.
* Keep existing bans: cast to reference not allowed, reject unsafe nominal casts per policy.

### 4. Update checklists
* Update `pp_semantic_analyzer_check_cast_expression` checklist to reflect machine-type resolution.
* Mark any checklist items that become N/A.

### 5. Tests
* Add semantic tests for newtype chains:
  * `newtype int32 AGE; newtype AGE WEIGHT;` – `WEIGHT` and `AGE` are distinct, assignment between them errors.
  * Cast `AGE` → `WEIGHT` allowed via machine-type equality.
  * Member access on newtype whose underlying is primitive errors.
* Verify existing tests still pass.

### 6. Code locations to review
* `src/pp_semantic_analyzer.c`: `pp_semantic_analyzer_build_global_table_newtype_decl`, `pp_semantic_analyzer_check_cast_expression`, `pp_semantic_analyzer_check_member_access_expression`.
* `src/pp_type_table.c`: `pp_type_equal`, `pp_type_decay`, `pp_type_is_const`, `pp_type_contains_reference`.
* `include/pp_type.h`: ensure `PPType` has `name` storage for Newtype.

### 7. Risks
* String lifetime for `name` field in `PPType`. All strings are interned in PP except filenames which are owned in the main semantic analyzer state, so storing a pointer to the identifier name is safe.
* Existing code that assumes newtype is an alias may break. Audit uses of `pp_global_type_table_find_by_name` and `pp_global_type_table_find_alias`.
* Cycle detection for newtype chains: optional, but avoid infinite loops in `pp_type_resolve_machine_type`.

### 8. Acceptance criteria
* `newtype int32 AGE; newtype AGE WEIGHT;` creates two distinct `PP_TK_Newtype` entries.
* `named_types["WEIGHT"]` points to the `WEIGHT` Newtype node, whose `underlying` points to the `AGE` Newtype node.
* `pp_type_resolve_machine_type(WEIGHT)` returns `int32`.
* Cast `AGE` ↔ `WEIGHT` is allowed; direct assignment is rejected as nominal types differ.
* All existing `*Parser*:*Semantic*` tests pass.

## Next steps
Await review. Upon approval, implement step 1 and run tests incrementally.
