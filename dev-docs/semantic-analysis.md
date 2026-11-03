# 07 – Semantic Analysis

## Pass 1 – Build Global Table
- Detect duplicate symbols for functions, lambdas, structs, variables, typedefs, newtypes
- For functions/structs: previous definition check
- For variables: duplicate in current scope, disallow reference globals
- Typedef/Newtype: base type defined, not const/ref, duplicate name, newtype cycle detection

## Pass 2 – Full Analysis

### Structs
- Duplicate field names
- Populate layout
- Verify field types well-defined
- Initializers: constexpr, type match, reference binding, const-correctness
- Cycle detection via `PP_StructRecursiveCycleState`

### Global Variables
- Lookup symbol
- No initializer → error if const/ref; struct const/ref fields need defaults
- constexpr initializer, type match, reference binding

### Functions / Lambdas
- Lookup, verify kind
- Set return type, verify all paths return if non-void
- Parameter scope creation, duplicate names
- Body scope, statement analysis

### Expressions
- Binary/unary type compatibility
- Literals, identifiers lookup
- Struct access, struct init
- Function calls argument match
- Cast expressions
- Collection initializer

### Copyability / Move Semantics
- `pp_type_needs_drop` identifies owning types: List, Struct containing owning fields, Array of owning elements, Newtype wrappers.
- `pp_type_is_copyable` determines whether a type can be deep-copied:
  - Trivially copyable types are copyable.
  - `List(T)` is copyable iff `T` is copyable and currently required to be trivially copyable for the fast-path `malloc+memcpy` implementation.
  - `Struct` is copyable iff all fields are copyable.
- Semantic checks:
  - Variable initialization: if initializer is lvalue and target type needs drop, require `pp_type_is_copyable` else error "Cannot copy initialize … type is move-only or contains non-copyable elements".
  - Assignment: if RHS is lvalue and LHS type needs drop, require `pp_type_is_copyable` else error "Cannot assign from lvalue: type is move-only or contains non-copyable elements".
- These checks prevent silent shallow copies that would alias buffers and cause double-free.

## Errors
Reported via `pp_error_list_push_error` with file/line/col.
