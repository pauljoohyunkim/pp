# 15 – Semantics: Expressions

## Operator compatibility
Binary and unary operators require compatible operand types.

Rules:
- Arithmetic `+ - * / %` require numeric types.
- Bitwise `& | ^` require integer types.
- Bitshift `<< >>` require `uint32` operands, result `uint32`. Shift amount `>= 32` is defined to yield `0`; otherwise the shift is logical and the result is truncated to 32 bits. Compound forms `<<=` `>>=` are allowed only for `uint32`.
- Comparison `== != < > <= >=` require comparable types, result `bool`.
- Logical `&& ||` require `bool` operands.
- Assignment requires left-hand side l-value.

## Literals
- Numeric literals must use unsigned suffix for array sizes.
- `true` / `false` are boolean literals.

## Identifiers
- Must be in scope.
- Type must be resolved.

## Struct access
- Member must exist.
- Access type matches field type.

## Function calls
- Argument count must match parameter count.
- Argument types must match parameter types after decay.
- Function must be defined.

## Cast
- `expr as type` requires explicit compatibility.
- Cast does not implicitly change const/reference.

## Array / List access
- Index must be integer expression.
- Index in bounds is not checked at compile time.

## Struct initializer
- Must match struct name.
- Field names must match.
- Types must be compatible.

Errors:
- Identifier not found
- Type mismatch
- Invalid expression
- Expected a return expression
