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
extern func pp_std_print_string :: (str :: string) -> void;
```

## Lambdas

```pp
lambda add :: (a :: int32, b :: int32) -> int32 {
    return a + b;
}
```

Lambdas have explicit signatures and can be assigned to variables.

Lambdas are pure: they cannot call regular `func` definitions, only other lambdas or operators. Lambdas cannot capture mutable global symbols.

## Calling

```pp
let x :: int32 = add(1, 2);
```

Arguments are passed by value. Const-ref parameters accept rvalues via materialized temporaries.

## Return types

All non-void functions must return on all paths. The semantic analyzer enforces exhaustive returns.

Next: [Operators](08-operators.md)
