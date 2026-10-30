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
- By-value List parameters are disallowed in move-only v1; use reference. *Temporary restriction for move-only v1.*
- List copy semantics are temporarily banned for move-only v1. Copy/drop will be implemented when value semantics are desired.

Initialization:
```pp
let v :: list[int32] = [int32; 1, 2, 3];
```

Access:
- Index must be integer expression.
- Assignment to element requires mutable l-value.
