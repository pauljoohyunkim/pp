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
- [x] Update `notes/list_copy_move_cache_plan.md` to note recursive types now supported
- [x] Update notebook page `list-copy-recursive-codegen`
- [x] Update `docs/10-semantics-arrays-lists.md` with recursive type support notes
- [x] Update `dev-docs/semantic-analysis.md` with copyable vs move-only notes

## Risks
- Runtime infinite loop on user-built cycles via mutation. With correct copy/drop emission this cannot be built by user code under exclusive ownership; it would indicate an aliasing bug in the compiler.
- If shared ownership is added later, owned graph is no longer guaranteed to be a tree and a visited-set deep copy becomes necessary.

## Example of problematic aliasing bug
The following looks like a cycle but is *not* reachable with correct deep-copy semantics because RHS is evaluated before the store:
```pp
struct Node { v :: int32; kids :: [Node]; }
let n1 :: Node = Node { .v = 1, .kids = [Node; ] };
let n2 :: Node;
n1.kids[0u] = n2;   // OK if kids is pre-allocated
```
A *real* problem would be a compiler bug where element stores skip `copy_T`/`drop_T`, e.g. collection initializer storing lvalue elements by plain store:
```pp
// Buggy codegen: element stored by memcpy instead of copy_T
let lst :: [Node] = [Node; n1, n2];
```
If `n1`/`n2` are the same allocation as `lst`'s storage, the resulting owned graph aliases and a subsequent deep copy of `lst` will recurse forever. This is the aliasing bug the warnings refer to, not a user program that intentionally creates a cycle.


## Potential Fix Considered but Rejected
A recursion guard was prototyped in `pp_type_is_copyable` and `pp_type_needs_drop` using a static visited set to break cycles. This would allow the copyability check to terminate for recursive types. However, this is a band-aid that hides the underlying semantic issue: the type system still permits deep copy of potentially cyclic owned graphs, which may diverge at runtime. The guard also uses static mutable state which is not re-entrant safe.

Decision: keep the guard out for now and revisit with a cleaner design, e.g. making copyability checks cycle-safe via explicit parameter passing or tightening semantics.

## Next Phase – Remove Static Globals
Branch: `allow-recursive-types-clean` from `75483d6`
Goal: remove `__thread` guard stacks and use per-call explicit stacks.

### Step 6 – Static-free cycle detection for semantic checks
- [x] Replace `__thread guard_stack/guard_depth` in `src/pp_type.c` with local stack passed to impl functions
- [x] Refactor `pp_type_needs_drop`, `pp_type_is_copyable`, `pp_type_is_trivially_copyable` to use `_impl(type, const PPType** stack, int depth)` with small fixed stack
- [x] Remove `__thread ref_guard` from `src/pp_semantic_analyzer.c` and use local stack for `pp_type_contains_reference`
- [x] Ensure codegen path with `PP_TypeVisitState*` remains unchanged
- [x] Build and run `make unittest`, verify no regression
- [x] Search for remaining `__thread` guard usage and eliminate


## Success Criteria
- Codegen succeeds for recursive structs
- Existing 282 tests still pass
- New tests for recursive structs pass
