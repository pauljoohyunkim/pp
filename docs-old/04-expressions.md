# 04 – Expressions & Statements

## Precedence
`logical_or` → `logical_and` → `equality` → `relational` → `bitwise_or` → `bitwise_xor` → `bitwise_and` → `bitshift` → `additive` → `multiplicative` → `cast` → `unary` → `postfix`

## Operations
- Arithmetic: `+ - * / %`
- Unary: `-` `!` `#`
  - `#` length operator: prefix operator returning `uint32` length of an array or list.
    - For array `[N] T`, `#` yields compile-time constant `N`.
    - For list `list(T)`, `#` yields runtime length loaded from the list handle.
- Assignment: `= += -= *= /= %= &= |= ^= <<= >>=`
- Bitwise: `& | ^`
- Bitshift: `<< >>` – defined only for `uint32`. Shift amount is checked at runtime; if the shift amount is ≥ 32 the result is defined to be `0`. Otherwise the operation is performed as a logical shift and the result is truncated to 32 bits.
- Comparison: `== != < > <= >=`
- Logical: `&& ||`
- Casting: `expr as type`
- Calls: `func_name(args)`
- Member access: `obj.field`
- Array/List access: `array[index]`

## Statements
- Variable declaration: `let x :: int32 = 42;`
- Assignment: `x = 5;`
- Return: `return expr;`
- Control flow:
```pp
if (cond) { ... } else { ... }
while (cond) { ... }
for (init; cond; update) { ... }
```
- Blocks: `{ ... }`

## Struct Init
```pp
let p :: Point = Point { .x = 1.0, .y = 2.0 };
```

## User-defined Operators
Custom operators are declared with the `operator` keyword.

### Syntax
```pp
operator <symbol> :: (param1 :: const T1&, param2 :: const T2&) -> Ret { ... }
operator <symbol> :: (param :: const T&) -> Ret { ... }
```

* `<symbol>` is one of `+ - * / % & | ^ << >> == != < > <= >=`.
* Parameters must be `const T&` reference types. Mutable references and by-value parameters are rejected for operators.
* Return type must be a value type, not a reference.
* Operators are resolved by decayed operand types only. Overloads that differ solely by pass mode are rejected.
* Built-in operators for core numeric, string and bool types cannot be redefined. Attempting to do so is a semantic error.

Example:
```pp
struct Vec { x :: float; y :: float; }
operator + :: (a :: const Vec&, b :: const Vec&) -> Vec {
    return Vec { .x = a.x + b.x, .y = a.y + b.y };
}
operator - :: (a :: const Vec&) -> Vec {
    return Vec { .x = -a.x, .y = -a.y };
}
```

Const reference parameters accept rvalues via materialized temporaries. The temporary lives for the full expression and is dropped afterwards.

