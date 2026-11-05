# 10 – Semantics: Arrays & Lists

## Arrays
Fixed size, size known at compile time.

Syntax:
```pp
let a :: [int32 : 4u] = [int32 : 4u; 1, 2, 3, 4];
```

Rules:
- Size expression must be a compile-time constant unsigned integer using literals with `u` suffix and `+ * / %`.
- Size must be > 0.
- Element type must be value type:
  - No reference
  - No void
  - No function/lambda
  - No transitive reference
- Const on array pushes const to elements.
- Reference to array is allowed as outer modifier: `[int32 : 4u]&`.

Initialization:
- Collection initializer must specify element type and size.
- Number of elements must match size.
- Each element must be compatible with element type.

Errors:
- Array size cannot be zero
- Array size addition overflow
- Array size multiplication overflow
- Division by zero in array size expression
- Modulo by zero in array size expression
- For array size expression, literals can only be unsigned integer
- Could not parse literal to number
- Array size literal out of range for size_t
- The only binary operator allowed in array size are '+', '*', '/', and '%'
- Element type is null
- element type cannot be a reference
- Array/List element type cannot be void
- Array/List element type cannot be function/lambda
- Array/List element type cannot transitively contain a reference

## Lists
Resizable runtime collection, represented internally as handle.

Rules:
- Element type follows same value constraints as arrays.
- List value semantics are now supported: copy on lvalue source, drop on scope exit.
- Copy is performed for variable initialization and assignment when the RHS is an lvalue and the type needs drop.
- A type is copyable if it is trivially copyable, or if it is an owning type whose elements are recursively copyable. Currently list elements must be trivially copyable for a deep copy to be generated; non-trivial element types make the list move-only and are rejected by semantic analysis.
- By-value List parameters are allowed; caller copies if argument is lvalue and type needs drop.
- Recursive owned types are allowed: `struct Node { kids :: [Node]; }` is legal. Codegen uses declare-then-body per-type helpers, so compilation succeeds. Deep copy/drop of a cyclic owned graph is undefined behavior – trees are fine, cycles created by mutation lead to infinite recursion.

Initialization:
```pp
let v :: [int32] = [int32; 1, 2, 3];
let w :: [int32] = v;  // deep copy generated
```

Access:
- Index must be unsigned 32-bit integer expression.
- Assignment to element requires mutable l-value.
