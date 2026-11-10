# 14 – Semantics: Const Correctness

## Const qualifier
`const T` means immutable value.

Rules:
- Const variable must be initialized at declaration.
- Const variable cannot be reassigned.
- Const can wrap any base type, including arrays and structs.
- Const on collection pushes const to elements.

## References
`T&` is a reference to `T`. Reference is always to const data internally.

Rules:
- Reference variable must be initialized with an l-value.
- Non-const reference cannot bind to const value.
- Reference fields cannot have default initializers.
- Global variables cannot be references.
- Reference to reference is not allowed.

## Interaction
- `const T&` binds to const or mutable data; binding is read-only.
- Assignment through non-const reference modifies target.
- Cast does not change const/reference qualifiers implicitly.

Errors:
- Non-const reference cannot bind to const value
- Const variable must have initializer
- Assignment to const variable is error
