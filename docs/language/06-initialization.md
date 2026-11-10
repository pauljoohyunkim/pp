# Initialization

## Variable initialization

Variables can be initialized at declaration:

```pp
let x :: int32 = 42;
let s :: string = "hello";
```

Local variables without initializer are zero-initialized. Const and reference variables must have initializer.

## Struct initialization

```pp
struct Point { x :: float; y :: float; }
let p :: Point = Point { .x = 1.0, .y = 2.0 };
```

Fields may be omitted if they have default initializers; otherwise they are zero-initialized.

## Array initialization

Fixed-size arrays:

```pp
let a :: [int32 : 3u] = [int32 : 3u; 1, 2, 3];
```

Undersized initializer is allowed and remaining elements are zero-filled. Oversized is an error.

## List initialization

```pp
let v :: [int32] = [int32; 1, 2, 3];
let empty :: [int32] = [int32;];
```

Lists are heap allocated.

## Global initialization

Global variables require constexpr initializers. Lists and strings at global scope must be empty.

```pp
let g :: int32 = 5;
let g2 :: [int32] = [int32;];
let g3 :: string = "";
```

Next: [Functions & Lambdas](07-functions-and-lambdas.md)
