# Expressions

## Literals

* Integer: `42`, `5u`, `10ul`
  Suffixes:
  - `s` → `int16` / `uint16`
  - `ss` → `int8` / `uint8`
  - `l` → `int64` / `uint64`
  - `ll` → `int128` / `uint128`
  - `u` prefix makes unsigned

  Examples: `42` → `int32`, `42u` → `uint32`, `42s` → `int16`, `42ss` → `int8`, `42l` → `int64`, `42ll` → `int128`
* Float: `3.14`
* Bool: `true`, `false`
* String: `"hello"` – heap allocated `string`
* Static string: `@"hello"` – `[uint8 : N]` with null terminator

## Operators

Precedence from lowest to highest:

`logical_or` → `logical_and` → `equality` → `relational` → `bitwise_or` → `bitwise_xor` → `bitwise_and` → `bitshift` → `additive` → `multiplicative` → `cast` → `unary` → `postfix`

### Built-in operators
* Arithmetic: `+ - * / %`
* Unary: `- ! #`
  `#` is length operator for arrays and lists.
* Assignment: `= += -= *= /= %= &= |= ^= <<= >>=` 
* Bitwise: `& | ^`
* Bitshift: `<< >>`
  * Shift amount is unsigned. If the shift amount is ≥ the bit-width of the unsigned integer type, the result is defined to be zero. This applies to both binary shifts and compound assignment shifts. Signed integer shifts are not guarded.
* Comparison: `== != < > <= >=`
* Logical: `&& ||`
* Cast: `expr as type`
* Member access: `obj.field`
* Indexing: `array[index]`, `list[index]`
* Calls: `func(args)`, `lambda(args)`
* Operator calls: `a + b` is resolved via built-in or user-defined operator overload

### Cast expressions
Cast is written as `expr as type` and has higher precedence than multiplicative and lower than unary/postfix. It performs a compile-time checked numeric conversion or allows up-/down-cast between compatible types.

Examples:
```pp
let i :: int32 = 42u as int32;   // unsigned to signed
let f :: float = 5 as float;     // integer to float
let u64 :: uint64 = 1ul;
let u32 :: uint32 = u64 as uint32;
```

Rules:
* The target type must be a complete base type, alias, or newtype.
* Casts are explicit; implicit conversions are not performed except for default zero-initialization.
* Casting to/from `string` is not supported yet.
* Casting to/from reference types is rejected by the semantic analyzer.



## User-defined operators

Custom operators are declared with `operator`:

```pp
operator + :: (a :: const Vec&, b :: const Vec&) -> Vec { ... }
```

Constraints:
* Parameters must be `const T&`
* Return type must be by value, not reference
* Built-in operators for core types cannot be redefined

See [Operators](08-operators.md) for full details.

## Struct init

```pp
let p :: Point = Point { .x = 1.0, .y = 2.0 };
```

Struct fields may have default initializers:
```pp
struct Point {
    x :: float = 0.0;
    y :: float;
}
let p :: Point = Point { .x = 1.0 };  // y defaults to 0.0 or zero
```

Default initializers must be constexpr. Missing fields without defaults are zero-initialized. Reference fields cannot have default initializers at global scope.

Next: [Control Flow](05-control-flow.md)
