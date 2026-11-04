# Allow Recursive Types via Declare-Then-Body Codegen

## Goal
Allow `struct S { s :: [S]; }` and mutually recursive structs by fixing per-type copy/drop helper generation to support recursion, without banning the type.

## Current Problem
`pp_code_generator_ensure_copy_fn` / `ensure_drop_fn` create LLVM function, generate body immediately, then cache. Recursive types cause infinite codegen recursion.

## Plan

### Step 1 – Make ensure_* helpers declare-then-body
- [x] In `pp_code_generator_ensure_copy_fn`:
  - Create function via `LLVMAddFunction`
  - Register `entry->copy_fn = func` **before** generating body
  - Save/restore builder, generate body
- [x] Same for `pp_code_generator_ensure_drop_fn`
- [x] Ensure builder save/restore still works with forward declaration

### Step 2 – Verify codegen termination
- [x] Smoke test: `struct Node { v :: int32; kids :: [Node]; }` copy
- [x] Smoke test: mutually recursive `struct A { b :: [B]; }`, `struct B { a :: [A]; }`
- [x] Verify IR contains `pp.copy.Node` and recursive call to itself via cache

### Step 3 – Runtime semantics decision
- [ ] Document current behavior: deep copy of cyclic owned graph may loop at runtime
- [ ] Add runtime guard option? For now accept and document limitation
- [ ] Add test for tree copy 3 levels deep to ensure no leak/double free

### Step 4 – Tests
- [x] Add unit test `CodeGenTest.RecursiveStructCopy` 
- [x] Add unit test `CodeGenTest.MutuallyRecursiveStructCopy`
- [x] Run `make unittest`, verify pass

### Step 5 – Documentation
- [ ] Update `notes/list_copy_move_cache_plan.md` to note recursive types now supported
- [ ] Update notebook page `list-copy-recursive-codegen`

## Risks
- Runtime infinite loop on user-built cycles via mutation
- If runtime cycles become issue later, need visited-set deep copy

## Potential Fix Considered but Rejected
A recursion guard was prototyped in `pp_type_is_copyable` and `pp_type_needs_drop` using a static visited set to break cycles. This would allow the copyability check to terminate for recursive types. However, this is a band-aid that hides the underlying semantic issue: the type system still permits deep copy of potentially cyclic owned graphs, which may diverge at runtime. The guard also uses static mutable state which is not re-entrant safe.

Decision: keep the guard out for now and revisit with a cleaner design, e.g. making copyability checks cycle-safe via explicit parameter passing or tightening semantics.

## Success Criteria
- Codegen succeeds for recursive structs
- Existing 282 tests still pass
- New tests for recursive structs pass
