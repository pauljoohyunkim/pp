# List Copy / Move – Option A Implementation Checklist

Per-type `copy_T` / `drop_T` generation, lift move-only ban for lists.

## 0. Audit & baseline
- [x] Run `make clean && make` and confirm warnings:
  - [x] `pp_semantic_analyzer.c:10` List copy semantics temporarily banned
  - [x] `pp_code_generator.c:119` emit_drop only handles direct List
  - [x] `pp_code_generator.c:1418` List access assumes allocated
- [x] Locate semantic bans:
  - [x] `src/pp_semantic_analyzer.c ~2223` init from lvalue List
  - [x] `src/pp_semantic_analyzer.c ~2392` assignment from lvalue List
  - [x] `src/pp_semantic_analyzer.c ~1624, ~1755` by-value List parameters
- [x] Confirm existing infra:
  - [x] `pp_type_needs_drop` exists in `src/pp_type.c:139`
  - [x] Drop stack `push_drop` / `emit_scope_drops` exists
  - [x] No `copy_T` / `drop_T` generation exists yet

## 1. Make drop recursive and per-type
**Files:** `src/pp_code_generator.c`, `include/pp_code_generator.h`

- [x] Replace List-only `pp_code_generator_emit_drop` with dispatcher to generated `drop_T`
- [ ] Implement `drop_T` generation, cached per `PPType*`
  - [x] `PP_TK_List` → free data pointer
  - [x] `PP_TK_Struct` → iterate fields, recurse if `type_needs_drop`
  - [x] `PP_TK_Array` → recurse per element if needed
  - [x] `PP_TK_Newtype` → delegate to underlying
  - [x] `PP_TK_Const` → unwrap
- [x] Remove `#warning "emit_drop currently only handles direct List types..."`
- [ ] Verify struct containing List is freed correctly in scope exit

## 2. Default-initialize owning variables
**Files:** `src/pp_code_generator.c` – alloca emission

- [x] On alloca for type where `pp_type_needs_drop` → emit initializer
  - [x] List → `{null, 0, 0}`
  - [x] Struct → initialize owning fields recursively
- [x] Remove `#warning "List access currently assumes data is allocated..."`
- [ ] Ensure no segfault on uninitialized list access

## 3. Per-type copy generation
**Files:** `src/pp_code_generator.c`, `include/pp_code_generator.h`

- [x] Add `type_is_trivially_copyable(T)` helper
  - [x] Primitives → true
  - [x] Struct → true iff all fields trivial and no owning fields
  - [x] Array → trivial if element trivial
  - [x] Newtype → delegate
- [x] Generate `copy_T` on demand, cached per `PPType*`
  - [x] Trivial types → bitwise load/store stub
  - [x] `List(T)` → deep copy via malloc + memcpy for trivial elements
  - [x] `Struct` → field-by-field recursive copy
  - [x] `Newtype` → delegate to underlying
  - [x] `Const` → transparent unwrap
- [ ] Ensure `copy_T` and `drop_T` are paired per type

## 4. Wire copy sites
**Files:** `src/pp_code_generator.c`

### 4.1 Variable initialization
- [x] If initializer value category is lvalue and `type_needs_drop` → emit `copy_T(src, dst)`
- [x] If rvalue → move as before

### 4.2 Assignment
- [x] Evaluate RHS to temp
- [x] If LHS needs drop and RHS is lvalue → `copy_T(rhs_temp, lhs_addr)` then `drop_T(old lhs)`
- [x] Handle self-assignment safely: copy-first ordering

### 4.3 By-value parameters
- [x] Caller side: if arg is lvalue and `type_needs_drop(param)` → `copy_T(arg, param)`
- [x] Callee side: push param onto drop stack
- [x] Keep semantic ban on by-value owning params until wired, then lift

### 4.4 Whole-struct init/assign
- [ ] Verify struct init/assign uses generated `copy_T`/`drop_T` automatically

## 5. Update semantic analysis
**File:** `src/pp_semantic_analyzer.c`

- [x] Remove init ban at ~2223
- [x] Remove assignment ban at ~2392
- [ ] Add copyability check: if source is lvalue and `type_needs_drop`, ensure `copy_T` exists for type
- [ ] Keep temporary ban on by-value owning params until 4.3 complete
- [x] Remove `#warning "List copy semantics are temporarily banned for move-only v1..."`

## 6. Tests & validation
- [ ] Unit test: copy then mutate source → no aliasing
- [ ] Unit test: assign over existing list → no leak
- [ ] Unit test: self-assignment `x = x`
- [ ] Unit test: list of lists
- [ ] Unit test: struct containing list
- [ ] Unit test: early return after copy
- [ ] Run under ASan / valgrind
- [x] `make unittest` passes

## 7. Documentation
- [ ] Update `docs/10-semantics-arrays-lists.md` with copy/move rules
- [ ] Update `dev-docs/semantic-analysis.md` with copyable vs move-only notes
- [ ] Add design note to `notes/` e.g. `notes/list_copy_move_design.md`

## 8. Final cleanup
- [ ] Remove all temporary `#warning`s related to lists
- [ ] `make clean && make` produces zero warnings about list semantics
- [ ] Commit with clear message

---
**Status:** Step 0 complete
