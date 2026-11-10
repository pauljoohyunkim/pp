# Operators

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
