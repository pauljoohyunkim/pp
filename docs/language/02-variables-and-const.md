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
