# 04 – Expressions & Statements

## Precedence
`logical_or` → `logical_and` → `equality` → `relational` → `bitwise_or` → `bitwise_xor` → `bitwise_and` → `additive` → `multiplicative` → `cast` → `unary` → `postfix`

## Operations
- Arithmetic: `+ - * / %`
- Assignment: `= += -= *= /= %= &= |= ^=`
- Bitwise: `& | ^`
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
