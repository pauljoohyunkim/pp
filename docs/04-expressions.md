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
