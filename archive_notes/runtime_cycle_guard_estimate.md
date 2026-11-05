# Runtime Cycle Guard for Deep Copy / Drop – Estimate

## Current State

`pp_code_generator.c` generates per-type helpers:

* `pp.copy.<type>(i8* dst, i8* src)` via `pp_code_generator_ensure_copy_fn`
* `pp.drop.<type>(i8* addr)` via `pp_code_generator_ensure_drop_fn`

`pp_code_generator_gen_copy_body` and `pp_code_generator_gen_drop_body` are responsible for the bodies.

* **List**: copy allocates new buffer and recurses per element via `pp_code_generator_emit_copy`. Drop loads data pointer, iterates length, calls element drop, then `free`.
* **Struct**: iterates fields, calls per-field copy/drop via ensure helpers.
* **Array**: loops over elements, calls per-element copy/drop.

Both paths are recursive with no runtime visited set. Codegen recursion is already guarded by early registration of the helper in `ensure_copy_fn`/`ensure_drop_fn`, but runtime recursion is unbounded.

With `struct Node { next :: [Node]; }` a deep copy of a cyclic graph:
```
copy(Node) -> copy(List) -> copy(Node) -> copy(List) -> ...
```
will recurse forever at runtime. Same for drop.

`pp_code_generator_emit_copy` / `pp_code_generator_emit_drop` are used both in generated copy bodies and in normal expression codegen. They currently call helpers with only dst/src pointers.

## Design Options

### Option A – Full visited set passed through copy/drop
Change signature to:
```
pp.copy.<T>(i8* dst, i8* src, i8** visited_stack, i32* visited_depth)
pp.drop.<T>(i8* addr, i8** visited_stack, i32* visited_depth)
```

Visited set implemented as a small fixed-size stack of pointers allocated by the caller of the top-level copy/drop. For each object pointer encountered:

1. Check if pointer already in visited stack → skip recursion / treat as already copied / already dropped.
2. Push pointer, recurse, pop.

Pros: correct for arbitrary cycles, re-entrant.
Cons: API change touches every call site, every generated helper, and the two internal emit helpers. Needs runtime support for visited stack allocation and comparison.

### Option B – Thread-local visited stack with push/pop
Keep signatures unchanged, maintain thread-local `void* visited[128]` and depth counter. `pp_code_generator_emit_copy` / `pp_code_generator_emit_drop` push pointer before recursive call and pop after.

Pros: No signature change for generated helpers.
Cons: Not re-entrant for nested copies in same thread, breaks with callbacks, global mutable state, still requires codegen changes to insert push/pop in every recursive path.

### Option C – Shallow copy for recursive types
Semantic restriction: disallow deep copy of self-referential containers, fall back to shallow pointer copy or abort. This aligns with Option 1 from notebook `list-copy-recursive-types`.

Pros: minimal change.
Cons: changes language semantics.

Recommendation for a true runtime guard: Option A. Option B is a pragmatic compromise if single-threaded usage is guaranteed.

## Implementation Impact

### Signature changes
* `PP_TypeCacheEntry` helpers: `pp_code_generator_ensure_copy_fn`, `pp_code_generator_ensure_drop_fn`
* `pp_code_generator_gen_copy_body`, `pp_code_generator_gen_drop_body`
* `pp_code_generator_emit_copy`, `pp_code_generator_emit_drop`
* All call sites in `pp_code_generator_emit_copy` / `pp_code_generator_emit_drop` for List, Struct, Array, and trivial fallback.

### LLVM IR changes
* All `pp.copy.*` functions go from 2 to 4 parameters: `i8* dst, i8* src, i8** visited, i32* depth`.
* All `pp.drop.*` functions go from 1 to 3 parameters: `i8* addr, i8** visited, i32* depth`.
* `gen_copy_body` for List element loop must load visited pointer, check membership, push/pop.
* Same for Struct field loop and Array loop.
* Need runtime helpers for visited stack operations: `pp.visited_contains`, `pp.visited_push`, `pp.visited_pop`. Could inline with a small loop given fixed depth <= 128.

### Per-type codegen modifications
* List copy: after allocating new buffer, for each element, call `elem_copy` with visited args.
* List drop: before iterating elements, check if list pointer already visited; if so skip.
* Struct copy/drop: before recursing into each field, propagate visited args.
* Array copy/drop: propagate visited args.

### Performance impact
* Membership test is O(depth). Depth typically small.
* Extra parameters increase call overhead.
* Visited stack allocation per top-level copy/drop: one `alloca [128 x i8*]` + `i32` on entry. Acceptable.

## Files to touch

* `include/pp_code_generator.h` – if visited context struct is exposed.
* `src/pp_code_generator.c`
  * `pp_code_generator_ensure_copy_fn`
  * `pp_code_generator_ensure_drop_fn`
  * `pp_code_generator_gen_copy_body`
  * `pp_code_generator_gen_drop_body`
  * `pp_code_generator_emit_copy`
  * `pp_code_generator_emit_drop`
  * `pp_code_generator_push_drop` / `pp_code_generator_emit_scope_drops` – may need visited for drops at scope exit.
* Tests: `tests/codegentest.cpp` – update expectations for function signatures and IR.
* Possibly runtime library: add `pp.visited` helpers in generated module.

## Effort Estimate

* Design + prototype visited stack handling in LLVM IR: 3h
* Change signatures and update `ensure_copy_fn` / `ensure_drop_fn`: 2h
* Update `gen_copy_body` for List / Array / Struct with visited propagation and checks: 4h
* Update `gen_drop_body` similarly: 3h
* Update `pp_code_generator_emit_copy` / `pp_code_generator_emit_drop` inline paths: 2h
* Update top-level call sites where copy/drop is invoked from expression codegen: 2h
* Add small runtime helpers and tests: 3h
* Debugging, Valgrind, edge cases: 4h

**Total ≈ 23 hours** for full Option A implementation with tests.

Alternative lightweight Option B (thread-local visited):
* Change emit helpers to push/pop via thread-local: ~8-10h
* Risk of re-entrancy and test fragility.

If the goal is only to avoid stack overflow on cyclic graphs, the much cheaper alternative is semantic restriction to disallow recursive list types: ~1-2h per notebook `list-copy-recursive-types`.

## Risks
* Changing signatures invalidates all existing IR tests.
* Visited set for drop is subtle: dropping a node twice is UB, skipping via visited is correct for cycles but must ensure final free still occurs for the original allocation.
* For copy, skipping already visited nodes means shared structure is preserved – must decide copy semantics: shallow alias vs deep copy with cycle preservation. Implementation above preserves identity, which is typical for cycle-safe copy.
* Fixed stack size 128 may overflow for deeply nested non-cyclic structures. Need overflow guard.

## Conclusion
Adding a correct runtime cycle guard for deep copy/drop is feasible but non-trivial: signature change, pervasive codegen updates, and new runtime visited logic. Estimated 1-2 working days for a robust implementation. If product requirements allow, semantic disallowance of recursive owned lists is significantly cheaper.
