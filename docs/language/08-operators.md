# Operators

## Built-in pointer operators

Pointer operators are built-in, non-overloadable and signal user-accepts-risk semantics.

* `@x` – address-of. `x` must be an l-value. Result type is `ptr[T]` where `x : T`.  C equivalent: `&x`.
  Use of address-of on non-l-values is rejected.

* `$p` – dereference. `p` must be `ptr[T]`. Result type is `T`.  C equivalent: `*p`. Null dereference is undefined behaviour.

* `p[i]` – pointer subscript. `p` must be `ptr[T]` with `T` not `void`; `i` must be `usize`. Result is an
  l-value of type `T` at `p + i * sizeof(T)`. C equivalent: `p[i]`. Unchecked: no bounds check, null and
  out-of-range access are undefined behaviour. Pointee constness carries over, so `q[i]` on `ptr[const T]`
  is a read-only l-value.

* `nullptr[T]` – null pointer literal of type `ptr[T]`.

Pointer operations are user-accepts-risk: the type system will not prevent aliasing, null dereference, or invalid pointer arithmetic. Casting between pointer types and numeric types is explicitly user-accepts-risk.

Examples:

```pp
let x :: int32 = 5;
let p :: ptr[int32] = @x;      // uint32_t *p = &x;
let v :: int32 = $p;           // int32_t v = *p;
let n :: ptr[int32] = nullptr[int32];

let arr :: [int32 : 4u] = [int32 : 4u; 1, 2, 3, 4];
let p2 :: ptr[int32] = @arr[0uz];
p2[1uz] = 20;                  // *(p2 + 1) = 20; scales by sizeof(int32)
let v2 :: int32 = p2[1uz];     // int32_t v2 = p2[1];
```

## Built-in string operators

`string` is a `{ptr, len}` pair, so its built-in operators are not pointer or integer operations:

* `a + b` – concatenation, allocates a new owned string.
* `a == b`, `a != b` – equality yields `bool`. The comparison checks length first and then compares the
  payloads byte-wise for exactly `len` bytes, so embedded NUL bytes are compared correctly (unlike `strcmp`).
  Two empty strings compare equal without any byte comparison; a `{nullptr, 0}` string is therefore safe.
* `#s` – length in bytes as `uint32`.
* `s[i]` – byte at index `i` as `uint8`.

These are builtin and cannot be replaced by user-defined operators; `operator == :: (const string&, const string&) -> bool` is rejected.

## User-defined operators

Custom operators are declared with `operator`:

```pp
operator + :: (a :: const Vec&, b :: const Vec&) -> Vec {
    // body
}
```

### Constraints

* Parameters must be `const T&`. Mutable references are forbidden.
* Return type must be by value. Returning a reference is rejected.
* Built-in operators for core types cannot be redefined. Example: `operator + :: (int32, int32) -> int32` is rejected.
  The same applies to the built-in unary `~` on integer primitives and flags, which is why `~` is overloadable
  for other types (e.g. `operator ~ :: (v :: const W&) -> W`).
* Operator bodies may only call lambdas or other operators, not regular functions.
* Operators support overload resolution by type. Overloads must differ in parameter types.

### Overload resolution

The compiler selects the most specific matching operator based on argument types. Rvalue arguments to `const T&` parameters are materialized into temporaries and dropped at full-expression boundaries.

### Examples

```pp
operator * :: (M :: const Mat2&, v :: const Vec2&) -> Vec2 { ... }
operator / :: (v :: const Vec2&, s :: const float&) -> Vec2 { ... }
```

See `demo/03_eigenvector` for a complete example.

Next: [Initialization](06-initialization.md)
