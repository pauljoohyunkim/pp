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
* `ptr[T]` – pointer to T
* `ptr[const T]` – pointer to const T
* `const ptr[T]` – const pointer to mutable T

References are explicit. Ownership is tracked by the compiler.

### Pointers

Pointers are explicit and first-class value types in PP.

* Type syntax: `ptr[T]`
* Address-of: `@x` yields `ptr[T]` where `x : T`
* Dereference: `$p` yields the pointee type of `p : ptr[T]`
* Null literal: `nullptr[T]` yields a null pointer of type `ptr[T]`
* Pointer casts are explicit with `as`. `ptr[T] as ptr[U]` is allowed for any `T/U` with const matrix enforced: const may be added, but never stripped.
* `ref[T] as ptr[T]` is allowed, `ptr[T] as ref[T]` is banned.
* Numeric ↔ pointer casts are user-accepts-risk and go through an i64 intermediate on 64-bit hosts.
* Pointer equality `==`/`!=` is supported.
* Copy/move semantics: pointers are trivially copyable and non-dropping.
* `ptr[T] → ptr[const T]` is implicit via const matrix.
* **Safety note:** using pointers in PP is user-accepts-risk. The type system does not prevent null dereference, aliasing, or invalid casts. Pointer casts, numeric ↔ pointer casts, and dereference are all explicitly risky operations.

Example:

```pp
let p :: ptr[uint8] = nullptr[uint8];      // uint8_t *p = NULL;
let x :: uint8 = 42uss;
let q :: ptr[uint8] = @x;                 // uint8_t *q = &x;
let v :: uint8 = $q;                      // uint8_t v = *q;
let hello :: [uint8 : 6u] = @"hello";     // static array
let r :: ptr[uint8] = @hello as ptr[uint8]; // uint8_t *r = (uint8_t*)hello;  array→pointer for C interop
```

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

### Callable types

A callable signature is itself a value type:

```pp
typedef func[(int32, int32) -> int32] BinOp;
let f :: func[(int32) -> int32] = some_function;
let g :: lambda[(int32) -> int32] = some_lambda;
let h :: func[() -> func[(int32) -> int32]] = factory;   // higher-order
```

* `func[(T1, T2) -> R]` – regular function signature
* `lambda[(T1, T2) -> R]` – pure lambda signature
* Parameter names are not part of the type; an empty parameter list `()` is allowed.
* A callable value is a pointer-sized code pointer: trivially copyable, needs no drop, and has no null value, so a
  callable-typed variable must be initialized.
* `const func[...]` and `func[...]&` bind exactly like any other object type.
* `lambda[...]` and `func[...]` with the same signature are **distinct** types. The only conversion is the explicit
  `a_lambda as func[...]` widening with an identical signature; there is no implicit widening.
* Not available in v1: `ptr[func[...]]`, arrays/lists of callable values, and comparisons or arithmetic/bitwise
  operators on callable values.

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

Lists are mutable heap buffers with an internal header `{data, len, cap, elem_size}`. Element types must not transitively contain references.

Built-ins for lists are polymorphic over `T`:

* `list_push(list, value)` – append a copy of `value` to `list`. Capacity grows 0 → 4 → `*2`.
* `list_pop(list)` – remove and return the last element; aborts if the list is empty.
* `list_clear(list)` – drop elements if needed, free the buffer and reset `len/cap` to zero.

Indexing `v[i]` is undefined behavior if `i` is out of bounds; no runtime bounds check is performed.

See [Built-ins](docs/reference/builtins.md) for full semantics.

## Type Matching

Types are matched structurally for primitives and nominally for structs/typedefs/newtypes. Operator overloads are resolved on decayed operand types.

Array sizes must be unsigned integer literals, e.g. `4u`. In practice sizes are evaluated via `strtoull`, so literals larger than 64 bits are not supported.

Next: [Variables & Const](02-variables-and-const.md)
