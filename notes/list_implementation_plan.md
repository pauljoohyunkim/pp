# List Support Implementation Plan

## Overview
Add dynamic `List<T>` support to the PP compiler. Lists are represented as a shared handle struct in LLVM, with runtime allocation for contents.

## Design Decision
Use a fixed-size handle struct, not a bare opaque pointer.

```
%List = type { ptr, i64, i64 }   ; data, length, capacity
```

One shared LLVM type for all `List(T)`. Element type is carried by semantic type and lowered on demand.

### Rationale
* Handle carries length/capacity → empty list is trivial `{null,0,0}`.
* `length` is a plain `extractvalue`.
* Works uniformly in structs, arrays, parameters.
* Aligns with existing struct/array codegen patterns.

## Design Choices

1. **Length type**: `i64` for length and capacity. Indexing with PP `uint32` requires explicit conversion in codegen.
2. **Type naming**: Create one named struct `%List` once, registered in global backend env. Lowering `List(T)` for any T returns it.
3. **Element type availability**: Every list operation must take semantic element type and lower it for GEP and sizeof. Use DataLayout for alloc size.
4. **Ownership rule v1**: Lists are move-only / non-copyable initially.
   * Ban by-value list parameters and whole-list assignment in semantic analyzer.
   * Extend `type_contains_*` helpers to `type_contains_list` for copy ban.
5. **Globals**: Only empty list allowed as constant initializer. Non-empty initializers are runtime only.
6. **Bool/element layout**: Same `i1` vs `i8` decision as arrays applies to heap elements.

## Implementation Stages

### Stage 1 - Lowering
- [ ] Add `PP_TK_List` case to `pp_backend_lower_type` returning shared `%List`.
- [ ] Create `%List` named struct once, register in backend type map.
- [ ] Ensure type map passes apply same unique-creation assertion as structs.

### Stage 2 - Default/Empty Construction
- [ ] Uninitialized `List(T)` defaults to `{null,0,0}` via implicit zero init.

### Stage 3 - Read-only Operations
- [ ] `length` → `extractvalue` index 1.
- [ ] Index access `list[i]`:
  - Load `data` pointer, GEP `[i]`, load/store element.
  - Optional bounds check vs `length`.

### Stage 4 - List Initializer
- [ ] Collection initializer for `List`:
  - `malloc(n * sizeof T)`, store elements, set data/len/cap.
  - Branch in `pp_code_generator_generate_rvalue_collection_init` for `PP_TK_List`.

### Stage 5 - Cleanup
- [ ] Free at scope exit on all paths, including early return.
- [ ] Declare `malloc`, `realloc`, `free` intrinsics on first use.

### Stage 6 - Growth Operations (Future)
- [ ] `push`, `pop`, `resize` via `realloc` with doubling strategy.

## Progress Tracking
- [x] Stage 1 completed
- [x] Stage 2 completed
- [x] Stage 3 completed
- [ ] Stage 4 completed
- [ ] Stage 5 completed
- [ ] Stage 6 completed

## Open Questions
- Index type conversion policy
- Exact ownership semantics for struct containing list
- Whether to allow const List<T>
