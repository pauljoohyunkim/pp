# 08 – Semantics: Variables

## Global variables
Scope: file scope.

Rules:
- Duplicate global names are errors.
- Global variables cannot be references.
- Global struct variables cannot contain reference fields.
- If no initializer:
  - Type must not be `const`.
  - Type must not be reference.
  - Otherwise zero-initialized.
- If initializer present:
  - Initializer must be compile-time constant.
  - Initializer type must match variable type after decay.
  - If variable is `const`, it becomes constexpr after successful check.
- Global variables are always considered escapable.

Errors:
- Symbol previously defined
- Global variable cannot be a reference
- Global variable is a struct, but global variable cannot contain any references
- Variable declared as const must have initializer
- Variable has an invalid initializer
- Global variable has a non constexpr initializer
- Type mismatch during variable declaration

## Local variables
Declared with `let` inside function/lambda bodies.

Rules:
- Name must be unique within current scope.
- Type must be fully defined.
- Initializer, if present, must be valid expression and type compatible.
- No compile-time requirement for locals.

## Const variables
- Must be initialized at point of declaration.
- Cannot be reassigned after initialization.
- Reference to const can bind to const or immutable data.
- Non-const reference cannot bind to const value.

## Reference variables
- Allowed only in local scope and as function parameters/returns.
- Must be initialized with an l-value.
- Non-const reference cannot bind to const value.
- Cannot be global.
