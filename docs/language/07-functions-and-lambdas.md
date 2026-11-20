# Functions & Lambdas

## Functions

```pp
func add :: (a :: int32, b :: int32) -> int32 {
    return a + b;
}
```

Functions are declared with `func`. Signature is mandatory, body is a block.

External functions:

```pp
extern func pp_print_string :: (str :: string) -> void;
```

## Lambdas

```pp
lambda add :: (a :: int32, b :: int32) -> int32 {
    return a + b;
}
```

Lambdas have explicit signatures and can be assigned to variables.

Lambdas are pure: they cannot call regular `func` definitions, only other lambdas or operators. Lambdas cannot capture mutable global symbols.

## Function values (callable types)

A `func` or `lambda` name is a callable value of the corresponding type, and callable values can be stored,
passed, returned and called through:

```pp
func inc :: (a :: int32) -> int32 { return a + 1; }
func dec :: (a :: int32) -> int32 { return a - 1; }

func apply :: (g :: func[(int32) -> int32], x :: int32) -> int32 {
    return g(x);
}

func pick :: (which :: bool) -> func[(int32) -> int32] {
    if (which) { return inc; }
    return dec;
}

func main :: () -> int32 {
    let f :: func[(int32) -> int32] = inc;
    f = dec;                                   // rebinding a mutable binding
    let g :: func[(int32) -> int32]& = f;      // reference to the stored value
    let h :: func[(int32) -> int32] = apply(f, 3);
    let chosen :: func[(int32) -> int32] = pick(true);
    return g(1) + h + chosen(1);
}
```

* A callable value is a pointer-sized code pointer: trivially copyable and drop-free.
* Calling a callable declaration name is a direct call; calling a stored value is an indirect call through the
  loaded pointer.
* Callable-typed variables must be initialized (no null callable).
* `const func[...]` cannot be rebound; `func[...]&` rebinds the original variable.
* Only `lambda[...] as func[...]` (identical signature) converts between callable kinds; there is no implicit
  widening.
* Callable struct fields are supported, and function-typed globals are allowed when initialized with a callable
  declaration name.
* Pointers to `func[...]` are supported (`@f`, `$p`, `nullptr[func[...]]`), while `ptr[lambda[...]]` is rejected.
* Arrays and lists of callable values are supported; a non-empty *global* list initializer stays banned by the
  general global rule.
* `==` / `!=` compare identical callable types by identity; other operators are rejected.

## Calling

```pp
let x :: int32 = add(1, 2);
```

Arguments are passed by value. Const-ref parameters accept rvalues via materialized temporaries.

## Return types

All non-void functions must return on all paths. The semantic analyzer enforces exhaustive returns.

Next: [Operators](08-operators.md)
