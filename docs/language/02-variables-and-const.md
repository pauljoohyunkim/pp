# Variables & Const

## Declaration

Variables are declared with `let`.

```pp
let x :: int32 = 42;
let s :: string = "hello";
let a :: [int32 : 4u] = [int32 : 4u; 1, 2, 3, 4];
```

Type is always explicit after the variable name with `::`.

### Default initialization
Local variables without an initializer are zero-initialized. Const and reference variables must always have an initializer.

```pp
let x :: int32;      // zero-initialized
let y :: const int32; // error: const requires initializer
let r :: int32&;      // error: reference requires initializer
```

## Initialization

Initializer is optional for local variables of non-const, non-reference types. Struct initialization uses field names:

```pp
struct Point { x :: float; y :: float; }
let p :: Point = Point { .x = 1.0, .y = 2.0 };
```

Arrays and lists are initialized with collection initializers:

```pp
let a :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];
let v :: [int32] = [int32; 1, 2, 3];
let empty :: [int32] = [int32;];
```

## Const

`const` makes a value immutable.

```pp
let x :: const int32 = 10;
x = 20;  // error: const variable cannot be assigned
```

Const also appears in reference types:

```pp
let r :: const int32& = x;
```

Const references are the only permitted parameter mode for user-defined operators.

## References

PP uses explicit reference types with `&`.

```pp
let r :: int32& = x;
r = 5;  // assigns to x
```

Const references bind to rvalues via materialized temporaries:

```pp
operator + :: (a :: const Vec&, b :: const Vec&) -> Vec { ... }
```

Mutable references are not allowed for operator parameters.

## Constants

`constant` declares a named compile-time value. Unlike a global variable, it has **no storage and
no link-time symbol**: every reference is folded to its value during code generation. This makes
constants safe to declare in `.ppi` files that are shared by several translation units.

```pp
constant MAX_ITEMS :: uint64 = 1024ul;
constant PI_APPROX :: float = 3.14159;
constant DEBUG :: bool = true;
```

Rules:

* The declared type must be a plain integer, `float`, or `bool` primitive. `const`/`&` modifiers,
  enums, flags, newtypes, structs, arrays, lists, pointers, strings, handles and callables are
  rejected.
* The initializer is mandatory and must be a `constexpr` expression exactly matching the declared
  type.
* A constant is immutable and cannot be assigned to or addressed (`@K` is rejected).
* A constant initializer may reference only constants declared earlier in the source.
* Constants may be used anywhere a value is expected, including inside lambda bodies.

An unsigned integer constant may be used as an array size, but it must be declared before the
array type that uses it:

```pp
constant N :: uint32 = 4u;
let a :: [int32 : N] = [int32 : N; 1, 2, 3, 4];
```

Signed, `float`, and `bool` constants cannot be array sizes.

## Global variables

Global variables must be initialized with a compile-time constant expression. Reference types are disallowed at global scope.

* Initializer must be `constexpr`.
* Lists and strings may only be initialized to empty at global scope.
  Non-empty list initializers and non-empty string literals are rejected.
* Struct globals cannot contain reference fields.

```pp
let g :: int32 = 5;          // ok
let g2 :: [int32] = [int32;];      // ok, empty list
let g3 :: string = "";       // ok, empty string
let g4 :: [int32] = [int32; 1,2];   // error: non-empty list at global scope
```

Next: [Declarations](03-declarations.md)
