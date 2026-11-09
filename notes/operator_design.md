# Operator Design (Draft)

## Core Semantics
- Custom operator keyword: `operator`.
- Operator declarations are essentially `lambda` underneath, with these restrictions:
  - Parameters must be `const T&` reference.
  - Return type must be non-reference (by value).
- Const references accept rvalues through **materialized temporaries** (value -> hidden lvalue, lifetime ends after the full expression).
- Mutable references still require lvalues.

## Why const-ref-only for operators
- One canonical signature per operator/type combination.
- No ambiguity between by-value and const-reference overloads.
- Operators remain side-effect-free in intent.
- `s + "123"` works via rvalue materialization for const reference parameters.
- Pass-by-value becomes an internal ABI optimization (e.g., small trivial types in registers), not a language feature.

## Parameter / Return Rules
- Allowed parameter form for v1: `const T&`.
- Return type: non-reference, by value.
- Match on decayed operand types only; reject overloads that differ solely by pass mode.
- Do not implement explicit by-value operator parameters in v1; if added later, restrict them to types where `type_needs_drop` is false.

## Implementation Notes
- Semantic analyzer: `const T&` may bind rvalues; mutable `T&` requires lvalue.
- Codegen: bind rvalue to const ref by materializing a temporary (entry `alloca`, initialize, pass address) and drop it at full-expression boundary.
- Ensure operator expressions that involve owning types (strings/lists) run temporary drops correctly.

## Limitation / Tradeoff
- Operators are primarily for pure, const-correct semantics; raw C interop should stay on pointer types.
## Allowed operator token subset (v1)
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Bitwise: `&`, `|`, `^`, `<<`, `>>`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- `~` is unary bitwise NOT and is intentionally omitted from v1.
