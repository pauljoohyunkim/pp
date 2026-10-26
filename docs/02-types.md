# 02 – Types

## Primitive Types
- `int32`
- `float`
- `bool`
- `void` – cannot define variable of type void

Future: `int8`, `int16`, `uint32`, `int`, `pointer`

## Modifiers
- `const` – immutable after initialization
- `&` – reference type, wraps const

Reference wraps const: `const int32&` means `Ref(Const(Base(Int32)))`

## User-defined Types
- `typedef base identifier` – alias, equivalent internally
- `newtype base identifier` – distinct type based on primitive, not compatible except via cast

## Aggregates
### Struct
Composite type with named fields. Can have default initializers.
```pp
struct Point { x :: float; y :: float; }
```
Reference field cannot have default initializer.

### Array
Fixed-size compile-time determined.
`[int32 : 5]`
Element types must be complete default-constructible value types:
- No Reference
- No void
- No function/lambda
- No type transitively containing reference
- Complete, non-zero size
Reference allowed only as outer modifier: `const [int32 : 4]&`

### List
Resizable runtime array. Represented as shared handle `%pp.list = {ptr, i64, i64}`

## Type Construction
Base → Modifiers → Reference
`int32` → `const int32` → `int32&` → `const int32&`
