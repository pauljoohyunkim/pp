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
  * Hexadecimal integers are supported: `0x41`, `0xDEADBEEFu`. Hex literals use the same suffix rules.
  * There are no octal literals: a leading zero is decimal, so `010` is ten (not eight).
* Character: `'A'`, `'\n'`, `'\x41'`
  Character literals are automatically typed as `uint8`. Escapes: `\n`, `\t`, `\r`, `\0`, `\'`, `\\`, `\xHH`.
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
  Callable values support only `==` / `!=` between identical callable types (pointer identity). Ordering,
  arithmetic and bitwise operators are rejected.
* Logical: `&& ||`
* Cast: `expr as type`
* Member access: `obj.field`
* Indexing: `array[index]`, `list[index]`, `string[index]`, `ptr[index]`
  * The index must be a `usize` (a pointer-width unsigned quantity). Any other type, including
    `uint32` and signed types, is rejected: write `a[0uz]` and cast loop counters with `as usize`.
  * Indexing a string yields a `uint8`.
  * Indexing a pointer (`p[i]`) is unchecked user-accepts-risk memory access equivalent to `*(p + i)`:
    the offset is scaled by the pointee size by the backend, there is no bounds check, and the result is
    an l-value denoting the storage at that address (`p[i] = v` writes). Pointee constness carries over,
    so `ptr[const uint8][i]` is a read-only l-value; `ptr[void]` is rejected because it has no element
    size. Arrays and lists are bounds-checked only when the index is a compile-time constant.
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
* Casting involving callable types is restricted to `lambda[...] as func[...]` with an identical signature.
  `func[...] as lambda[...]`, `func[...] as func[...]`, `lambda[...] as lambda[...]`, and callable ↔ non-callable
  casts are all rejected.



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
