# Types

PP has a static, nominal type system with explicit modifiers.

## Primitive Types

* `bool`
* Signed integers: `int8`, `int16`, `int32`, `int64`, `int128`
* Unsigned integers: `uint8`, `uint16`, `uint32`, `uint64`, `uint128`
* `float` – IEEE 754 double precision
* `string` – heap-allocated owned string, literal `"..."`
* Static byte arrays for string literals – `[uint8 : N]` with `@"..."` literal, null-terminated

Static string literals:
```pp
let s :: [uint8 : 6u] = @"hello";  // includes null terminator
```

Heap strings:
```pp
let s :: string = "hello";
```

## Modifiers

* `const T` – immutable value
* `T&` – reference to T
* `const T&` – const reference

References are explicit. Ownership is tracked by the compiler.

## Composite Types

### Struct
```pp
struct Point {
    x :: float;
    y :: float;
}
```

### Typedef / Newtype
```pp
typedef int32 MyInt;
newtype int32 Meters;
```

### Array
Fixed-size array type: `[T : N]` where `N` is an unsigned integer literal, e.g. `4u`, `4ul`.
Currently array sizes are evaluated via `strtoull`, so literals larger than 64 bits are not supported in practice.

```pp
let a :: [int32 : 4u] = [int32 : 4u; 1, 2, 3, 4];
```

### List
Heap-allocated growable list type: `[T]`

```pp
let v :: [int32] = [int32; 1, 2, 3];
```

## Type Matching

Types are matched structurally for primitives and nominally for structs/typedefs/newtypes. Operator overloads are resolved on decayed operand types.

Array sizes must be unsigned integer literals, e.g. `4u`. In practice sizes are evaluated via `strtoull`, so literals larger than 64 bits are not supported.

Next: [Variables & Const](02-variables-and-const.md)
