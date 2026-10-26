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

## Errors
Reported via `pp_error_list_push_error` with file/line/col.
