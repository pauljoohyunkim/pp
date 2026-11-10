# 07 – Semantics: Types & Constraints

## Type construction
Types are built by stacking modifiers on a base.

Base → `const` → `&`

Rules:
- `const` may wrap any base type, including aggregates.
- `&` (reference) must be the outermost modifier. `const T&` is a reference to const data.
- `const` on a collection pushes const inward to the element type, e.g. `const [int32 : 4]` means elements are const.

## Primitive types
- `int32`, `uint32`, `float`, `bool`, `void`
- `void` cannot be used as a variable type.

## User-defined types
### Typedef
- Alias, fully interchangeable with base.
- Base type must exist and be defined.
- Base type must not be `const` or `&`.
- Duplicate names are errors.

### Newtype
- Distinct type, not implicitly compatible with base.
- Base type must exist and be defined.
- Base type must not be `const` or `&`.
- Base cannot be function or lambda.
- Duplicate names are errors.
- Use `as` cast to convert between newtype and base.

## Collection element constraints
For arrays and lists, element types must be value types:
- Element type cannot be a reference.
- Element type cannot be `void`.
- Element type cannot be function or lambda.
- Element type cannot transitively contain a reference.
- Array size must be a compile-time constant non-zero unsigned integer expression using `+ * / %`. Size literals use unsigned suffix, e.g. `4u`.
- List element type follows same rules.

Allowed examples:
```pp
let a :: [int32 : 4u];
let b :: const [int32 : 4u];   // const elements
let c :: [int32 : 4u]&;        // reference to whole array
```

Disallowed:
```pp
let bad :: [int32& : 4];      // element is reference
let bad2 :: [void : 4];
let bad3 :: [S : 4];          // S contains reference
```

Errors:
- Element type is null
- element type cannot be a reference
- Array/List element type cannot be void
- Array/List element type cannot be function/lambda
- Array/List element type cannot transitively contain a reference
- Array size cannot be zero
- Could not compute array size
