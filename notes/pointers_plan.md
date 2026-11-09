# Pointer Support Plan (Draft)

## Purpose
Pointers are provided primarily as an escape hatch for C interop (`pp_std`). They are discouraged for general PP code; `reference` remains the default safe abstraction.

## Core Decisions Needed (Ambiguities)
- Type spelling: `ptr[T]` vs `T*`.
- Nullable by default vs explicit `Option[ptr[T]]`.
- Address-of (`&x`) and deref (`*p`) precedence and grammar.
- Pointer arithmetic: allow raw arithmetic (`p + n`) or only subscripting (`p[n]`).
- Const matrix:
  - `const ptr[T]` = pointer itself is immutable
  - `ptr[const T]` = pointee is immutable
  - transitive const across collections/struct fields
  - narrowing/widening rules across calls and C boundaries
- Mutually exclusive with references: a type cannot be both reference and pointer.
- Ownership / lifetime rule: PP does not own pointed-to C memory; no auto-drop on destruction.
- Cast policy: explicit `cast` only; no implicit reference↔pointer conversion.

## Type System Changes
- Add `PP_TK_Pointer` (pointee type).
- Add `type_contains_pointer` (complement to `type_contains_reference`).
- Define equality/compatibility rules and const propagation.
- Keep reference rules unchanged.

## Implementation Plan

### 1. Lexer / Parser
- Add pointer syntax and `&` / `*` handling.
- Resolve precedence ambiguities with member access, indexing, deref.

### 2. Semantic Analysis
- Type checking for pointer construction and deref.
- Null checks and optional/nullable policy.
- Ensure no mixing with references.
- Add predicates to collection element rules.

### 3. Codegen
- Lower `ptr[T]` to LLVM `ptr` or typed pointer form.
- Emit address-of, load/store, GEP for indexing, null comparisons.
- Keep pointed-to memory external; do not auto-generate drops.
- Interoperate with `pp_std` pointer parameters.

### 4. Tests
- Parser and semantic tests for pointer types.
- Codegen IR tests for address-of/load/store.
- Runtime smoke tests against `pp_std`.

## Non-goals for First Pass
- General pointer arithmetic beyond indexing.
- Implicit null safety (unless chosen as explicit language policy).
- Ownership-based lifetime inference.

## Risks / Migration Notes
- Any current reference-field assumptions must be revisited.
- Const martix must be fully defined before semantic codegen.
- C interop tests should pin exact ABI (`void*`, struct layouts).