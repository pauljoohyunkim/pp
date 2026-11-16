# Operators

## Built-in pointer operators

Pointer operators are built-in, non-overloadable and signal user-accepts-risk semantics.

* `@x` – address-of. `x` must be an l-value. Result type is `ptr[T]` where `x : T`.  C equivalent: `&x`.
  Use of address-of on non-l-values is rejected.

* `$p` – dereference. `p` must be `ptr[T]`. Result type is `T`.  C equivalent: `*p`. Null dereference is undefined behaviour.

* `nullptr[T]` – null pointer literal of type `ptr[T]`.

Pointer operations are user-accepts-risk: the type system will not prevent aliasing, null dereference, or invalid pointer arithmetic. Casting between pointer types and numeric types is explicitly user-accepts-risk.

Examples:

```pp
let x :: int32 = 5;
let p :: ptr[int32] = @x;      // uint32_t *p = &x;
let v :: int32 = $p;           // int32_t v = *p;
let n :: ptr[int32] = nullptr[int32];
```

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
