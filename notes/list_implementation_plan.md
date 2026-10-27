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
2. **Type naming**: Create one named struct `%pp.list` once, registered in global backend env. Lowering `List(T)` for any T returns it.
3. **Element type availability**: Every list operation must take semantic element type and lower it for GEP and sizeof. Use DataLayout for alloc size.
4. **Ownership rule v1**: Lists are move-only / non-copyable initially.
   * Ban by-value list parameters and whole-list assignment in semantic analyzer.
   * Extend `type_contains_*` helpers to `type_contains_list` for copy ban.
5. **Globals**: Only empty list allowed as constant initializer. Non-empty initializers are runtime only.
6. **Bool/element layout**: Same `i1` vs `i8` decision as arrays applies to heap elements.
7. **Copy vs Move decision**: Do not store an explicit copy/move flag in AST decoration. Copy vs move is determined by the source expression's `value_category`:
   * rvalue source → move / take ownership of temporary buffer
   * lvalue source → copy (or rejected in move-only v1)
   * Return of a local is a special context: treat as move even though source is lvalue.
   Adding a separate flag duplicates `value_category` and can drift. Liveness-based move optimization can be added later if needed.

## Implementation Stages

### Stage 1 - Lowering
- [x] Add `PP_TK_List` case to `pp_backend_lower_type` returning shared `%pp.list`.
- [x] Create `%pp.list` named struct once, register in backend type map.
- [x] Ensure type is created only once with proper global backend type map registration.

### Stage 2 - Default/Empty Construction
- [x] Uninitialized `List(T)` defaults to `{null,0,0}` via implicit zero init.

### Stage 3 - Read-only Operations
- [x] `length` → `extractvalue` index 1.
- [x] Index access `list[i]`:
  - Load `data` pointer, GEP `[i]`, load/store element.
  - Optional bounds check vs `length`.
- [ ] Add null-check warning for uninitialized lists.

### Stage 4 - List Initializer
- [x] Collection initializer for `List`:
  - Stack alloca for now: `alloca [n x T]`, store elements, set data/len/cap.
  - Branch in `pp_code_generator_generate_rvalue_collection_init` for `PP_TK_List`.
- [x] Switch to heap allocation via `malloc` for real lists.
- [ ] Support global empty list only.

### Stage 5 - Cleanup
- [x] Free at scope exit on all paths, including early return.
- [x] Declare `malloc`, `realloc`, `free` intrinsics on first use.
- [x] Decide ownership semantics: move-only vs deep copy.
- [x] Ban or implement assignment semantics to avoid double-free.

### Stage 6 - Growth Operations
- [ ] `push`, `pop`, `resize` via `realloc` with doubling strategy.
- **Status:** BLOCKED – no syntax/AST/semantic support for `list.push(expr)`, `list.pop()`, `list.resize(n)` in parser/BNF. Member access is struct-only; no builtin list-method resolution exists.

## Progress Tracking
- [x] Stage 1 completed
- [x] Stage 2 completed
- [x] Stage 3 completed
- [x] Stage 4 completed
- [x] Stage 5 completed
- [ ] Stage 6 completed
- **Stage 6 status:** BLOCKED pending syntax/AST/semantic definition for push/pop/resize.

## Open Questions
- Index type conversion policy
- Exact ownership semantics for struct containing list
- Whether to allow const List<T>
- Stack alloca vs heap malloc trade-off
- Assignment behavior: move-only vs deep copy

## Improvements
- List handle caching as potential improvement
- Array size truncation fix
- Struct registration consistency
