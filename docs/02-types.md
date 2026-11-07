# 02 – Types

## Primitive Types
- `int8`, `uint8`
- `int16`, `uint16`
- `int32`, `uint32`
- `int64`, `uint64`
- `int128`, `uint128`
- `float`
- `bool`
- `void` – cannot define variable of type void

Future: `int`, `pointer`

### Integer Literal Suffixes
Integer literals can be suffixed to specify type explicitly. The suffix is case-insensitive and the unsigned prefix `u` always precedes the size suffix.

| Suffix | Type |
|--------|------|
| `s` | int16 |
| `ss` | int8 |
| `l` | int64 |
| `ll` | int128 |

Unsigned forms:
- `us` → uint16
- `uss` → uint8
- `ul` → uint64
- `ull` → uint128

Examples:
```pp
let a :: int16 = 42s;
let b :: uint8 = 255uss;
let c :: int64 = 100l;
let d :: uint128 = 1ull;
```

No implicit promotion; use `as` for explicit casts.


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

## String Types
* No `str` type. Static strings are fixed-size byte arrays `[uint8 : N]`.
* Heap-allocated strings use builtin type `string`.

### Static string literals
* Syntax: `@ "text"` with no whitespace between @ and "."
* Can only initialize `[uint8 : N]` arrays.
* Implicit NUL terminator is part of the array length: required N == unescaped_byte_count + 1.
* Exact size match required; e.g., `let a :: [uint8 : 4] = @"abc";` is valid, `let b :: [uint8 : 3] = @"abc";` is error.
* Supported escapes: \n \t \r \" \\ \xHH \0 etc. Escape sequences are interpreted at semantic analysis.
* Example: `@"a\0"` requires `[uint8 : 3]` for `'a'`, explicit NUL, implicit NUL.

### Heap string type `string`
* Literal syntax: `"text"` with no @ prefix.
* Length is escaped byte count, no NUL terminator.
* Global variables of type `string` may only be initialized with empty literal `""`. Non-empty literals allowed only for locals.
* Requires copy/drop semantics.
* Concatenation via + is planned but not prioritized now.

## Type Construction
Base → Modifiers → Reference
`int32` → `const int32` → `int32&` → `const int32&`
