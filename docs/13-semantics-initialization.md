# 13 – Semantics: Initialization

## Compile-time constants
Global initializers and struct default initializers must be constexpr.

What is constexpr:
- Literals
- Constant expressions built from literals, operators `+ - * / %`, and constexpr values
- No runtime function calls
- No non-const globals

Rules:
- Global variable initializer must be constexpr.
- Struct field default initializer must be constexpr.
- Local variable initializer may be runtime.

## Type matching
Initializer type must match target type after decay.

Decay rules:
- Const and reference qualifiers are considered.
- Newtype requires explicit cast to base or vice versa.

## References
- Reference variable must be initialized with an l-value.
- Reference binding respects const-correctness: non-const reference cannot bind to const value.
- Reference fields require initializer at instantiation; default initializer not allowed.

## Struct initialization
- Designated initializers: `Point { .x = 1, .y = 2 }`
- All fields must be supplied unless default exists.
- Field types must match initializer types.

Errors:
- Global scope initializer for struct field should be constexpr
- Global variable has a non constexpr initializer
- Initializer for struct field does not match the field type
- Type mismatch during variable declaration
- Reference binding const violation
