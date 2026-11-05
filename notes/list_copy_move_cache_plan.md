# Per-type copy_T / drop_T caching plan

## Goal
Move copy/drop emission out of the inline `pp_code_generator_emit_copy` / `pp_code_generator_emit_drop` paths into per-`PPType*` cached helpers `pp.copy.<id>` / `pp.drop.<id>`. One helper per type, generated once per `PP_CodeGenerator`, reused everywhere.

## Current state
- Cache fields added to `PP_CodeGenerator`.
- `PP_TypeCacheEntry` with `copy_fn` / `drop_fn` exists.
- `pp_code_generator_find_cache_entry` / `pp_code_generator_get_or_create_cache_entry` implemented.
- `pp_code_generator_ensure_copy_fn` generates trivial-type helpers only.
- `pp_code_generator_ensure_drop_fn` generates no-op helpers for non-needing-drop types only.
- `emit_copy` / `emit_drop` lookup cache and call helper when present.

Tests pass with trivial-only caching.

## Checklist

### Step 1 – Design helper signatures
- [x] Confirm generic `void(i8*,i8*)` copy and `void(i8*)` drop signatures
- [x] Define dot-prefixed naming scheme `pp.copy.<hex>` / `pp.drop.<hex>`
- [x] Document pointer reinterpretation via bitcast inside helpers

### Step 2 – Refactor emit logic into reusable generators
- [x] Create `pp_code_generator_gen_copy_body(cg, func, type)`
- [x] Create `pp_code_generator_gen_drop_body(cg, func, type)`
- [x] Add builder save/restore helpers for helper generation context

### Step 3 – Extend `ensure_copy_fn`
- [x] Trivial types → memcpy helper
- [x] `PP_TK_List` → deep copy helper with element recursion
- [x] `PP_TK_Struct` → field-wise copy helper with field recursion
- [x] Cache helper in entry

### Step 4 – Extend `ensure_drop_fn`
- [x] No-drop types → no-op helper
- [x] `PP_TK_List` → element drop loop + free
- [x] `PP_TK_Struct` → field drop recursion
- [x] Cache helper in entry

### Step 5 – Wire emit functions
- [x] `pp_code_generator_emit_copy` calls ensure + emits call
- [x] `pp_code_generator_emit_drop` calls ensure + emits call
- [x] Remove inline paths for covered types

### Step 6 – Testing
- [x] `make && make unittest` passes
- [ ] Valgrind clean on ListOfListsDeepCopy
- [ ] Inspect LLVM IR for helper deduplication

### Step 7 – Optional improvements
- [ ] Hash map cache
- [ ] Generation stats
- [x] Recursion guard
