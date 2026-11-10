# Expected Design: Pointer Support v1 for PP

## Purpose
Provide a minimal, explicit pointer facility as an escape hatch for C interop (`pp_std`). Pointers are discouraged for general PP code; `reference` remains the default safe abstraction.

## Guiding principles
* Explicit is better than implicit.
* Pointers and references are mutually exclusive type kinds.
* No hidden ownership; pointers are non-owning, bitwise copy, no drop.
* Lambdas stay pointer-free to preserve safety/purity.
* Minimal v1 surface area; pointer arithmetic deferred.
* Address-of `@` and dereference `$` are built-in, non-overloadable operators.

## Syntax
* Type spelling: `ptr[T]` e.g. `ptr[uint8]`, `ptr[ptr[int32]]`
* Address-of: `@expr` – only on l-values
* Dereference: `$expr` – only on `ptr[T]`
* Null literal: `nullptr[T]` required. `nullptr` is reserved. Bare `nullptr` not allowed. `nullptr[void]` allowed. `nullptr[const T]` allowed.
* Cast syntax: `expr as TargetType` – explicit only

## Type system
* New kind: `PP_TK_Pointer` with pointee type `T`
* Predicates: `type_contains_pointer`, complement to `type_contains_reference`
* Const matrix:
  * `const ptr[T]` – pointer itself immutable
  * `ptr[const T]` – pointee immutable
  * Both const slots independent
  * Implicit conversion: `ptr[T]` → `ptr[const T]` allowed at outermost pointee only, reverse not allowed. No deep const stripping.
* Mutual exclusion: a type cannot be both reference and pointer.
* Pointer-to-pointer allowed: `ptr[ptr[T]]`
* `ptr[void]` allowed for C interop. No dereference, no meaningful subscript.
* Pointer comparison `==`/`!=` allowed for same type, with implicit const conversion on both sides.
* Backend type map: `ptr[T]` lowers to opaque LLVM ptr. Lowering does not descend into `T`. `type_needs_drop` is false for pointers, even if pointee needs drop.

## Semantics
* Nullable by default. `nullptr[T]` has type `ptr[T]`.
* Address-of requires l-value. Taking address of temporary is disallowed. `@x` on const lvalue yields `ptr[const T]`. `@r` where r is reference yields pointer to referent.
* Dereference requires `ptr[T]`; null dereference is UB for v1. Dereference of `ptr[void]` is error. `@` of `$expr` where result is pointer is pointer arithmetic via `@p[n]`.
* Subscript `p[n]` allowed via GEP, no bounds check. `@p[n]` therefore performs pointer arithmetic. Subscript on `ptr[const T]` yields const lvalue. Assignment to `$p` for `ptr[const T]` is error. `$` on `const ptr[T]` is fine.
* No implicit conversions:
  * No implicit `ref` ↔ `ptr`
  * No implicit array decay to pointer. Explicit `@arr[0u]` required.
* Implicit const conversion as above.
* Explicit casts:
  * `ptr[T] as T&` – pointer to reference. Result is lvalue with `is_escapable_ref = false`, `is_constexpr = false`. Target constness must match pointee constness: `ptr[const T] as const T&` only, never `ptr[const T] as T&`. Null is UB.
  * `T& as ptr[T]` – reference to pointer
  * `ptr[T] as ptr[U]` – pointer-to-pointer, pointee const may not be removed at any nesting level.
  * `uint64 as ptr[T]` – integer to pointer. v1 targets 64-bit, so pointer width is `uint64`. Document 32-bit limitation.
  * `ptr[T] as uint64` – pointer to integer, for printing/hardware. Same width rule.
  * Cast may not remove pointee const at any nesting level.
* Global pointer initializers: legal only as `nullptr[T]` or `uint64 as ptr[T]` of a constant. `is_constexpr = true`.
* Lambdas: pointer types, address-of, deref forbidden inside lambda bodies, transitively via structs/newtypes. Operators inherit ban. `extern lambda` signatures checked at declaration, error points at offending param type.

## Codegen
* Lower `ptr[T]` to opaque LLVM ptr; pointee type used at load/store.
* `@expr` uses existing `generate_lvalue` address.
* `$expr` is an lvalue whose address is the loaded pointer value.
* `p[n]` lowers to GEP.
* Pointers are plain values: bitwise copy, no drop, no ownership transfer.

## Safety notes
* PP does not own pointed-to C memory.
* Dangling after stack escape, realloc, growth.
* Const does not propagate through pointer fields of const struct.
* Self-referential structs via pointer are legal: `struct Node { next :: ptr[Node]; }`. Pointer edge breaks cycle.

## Non-goals v1
* Implicit null safety
* Ownership-based lifetime inference
* Pointer use inside lambdas

## Deliverables
* `notes/pointers_design.md` finalized
* Type system changes in `pp_type.c`, `pp_type_table.c`
* Parser/Lexer support for `ptr[T]`, `@`, `$`, `nullptr[T]`
* Semantic checks for const matrix, mutual exclusion, lambda prohibition, cast rules
* Codegen lowering and `pp_std` interop tests
* Documentation updates

## Review request
Design ready for implementation.
