# Declarations

## Variable declarations

```pp
let x :: int32 = 42;
let y :: const int32 = 10;
let r :: int32& = x;
```

See [Variables & Const](02-variables-and-const.md) for initialization rules and global restrictions.

Callable-typed variables must be initialized, because a callable value has no null representation:

```pp
func inc :: (a :: int32) -> int32 { return a + 1; }

func main :: () -> int32 {
    let f :: func[(int32) -> int32] = inc;   // function name -> callable value
    f = other_inc;                           // mutable bindings can be rebound
    return f(1);
}
```

At global scope the initializer must be the name of a `func` or `lambda` (a link-time constant). Callable struct
fields behave like const/reference fields: they need an initializer unless the field declares a default.

## Functions

```pp
func add :: (a :: int32, b :: int32) -> int32 {
    return a + b;
}
```

Functions are declared with `func`, signature after `::`, body in `{}`.

External functions:

```pp
extern func pp_std_print_string :: (str :: string) -> void;
```

## Lambdas

```pp
lambda add :: (a :: int32, b :: int32) -> int32 {
    return a + b;
}
```

Lambdas are anonymous functions with an explicit signature.

## Structs

```pp
struct Point {
    x :: float;
    y :: float;
}
```

Struct fields may have default initializers at global scope.

## Typedef / Newtype

```pp
typedef int32 MyInt;
newtype int32 Meters;
```

`typedef` creates an alias. `newtype` creates a distinct nominal type.

## Handles

`handle` declares a nominal type that owns a resource and binds the function that releases it:

```pp
handle File   :: int32     drop pp_file_close;
handle Buffer :: ptr[void] drop EVP_free;
```

A handle is born in a `let`, is shared only by reference, and is dropped automatically at the end of its scope.
It cannot be copied, passed by value, or released explicitly.

See [Handles](09-handles.md) for the full rules.

## Operators

User-defined operators are declared with `operator`:

```pp
operator + :: (a :: const Vec&, b :: const Vec&) -> Vec { ... }
```

See [Operators](08-operators.md) for constraints.

Next: [Expressions](04-expressions.md)
