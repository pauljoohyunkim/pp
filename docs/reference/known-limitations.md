# Known Limitations

* Standard library documentation is deferred for beta. See `pp_std` repository for stdlib.
* No modules yet; all code is in single files.
* No templates/generics.
* Global variables must be constexpr; no runtime globals.
* `--freestanding` disables the heap-backed `string` and list types, and forces unoptimized IR so the
  object never references heap or `memcpy`/`memset` libcalls. It is not a bare-metal target; linking
  still uses `cc`.
* Reference types cannot be stored in collections.
* Lambdas are pure: cannot call regular functions or capture mutable globals.
* Operator overloading limited to const-ref parameters and value returns.
* Array sizes are limited to 64-bit via `strtoull` parsing.
* Struct layout control is binary only: `packed struct` removes all padding and sets ABI alignment 1, and
  there is no `align(N)`, bitfields, or per-field control.
* Callable values (`func[...]` / `lambda[...]`) are code pointers only: no closures or captured environments.
* No pointers to lambda types: `ptr[lambda[...]]` is rejected (pointers to `func[...]` are supported).
* A non-empty global list initializer is rejected for every element type, including callables.
* Callable values support only `==` / `!=` between identical callable types; no arithmetic/bitwise or ordering
  operators.
* `lambda[...]` → `func[...]` widening is explicit-only and requires an identical signature.
* Function-typed globals require an initializer that is a `func` or `lambda` declaration name (no indirect
  initialization).
* Handles (`handle H :: T drop f;`) are limited to locals, `H&` / `const H&` borrows and `-> H` returns. They are
  rejected as globals, struct fields, collection elements, lambda parameters/returns/locals, and cast targets.
  A PP-defined callable cannot take a handle by value (an `extern` declaration may: the C callee only borrows),
  the underlying must be a scalar primitive or a pointer, and the drop function cannot be called from source.

These limits are tracked in `PROGRESS.md`.
