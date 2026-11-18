# Known Limitations

* Standard library documentation is deferred for beta. See `pp_std` repository for stdlib.
* No modules yet; all code is in single files.
* No templates/generics.
* Global variables must be constexpr; no runtime globals.
* Reference types cannot be stored in collections.
* Lambdas are pure: cannot call regular functions or capture mutable globals.
* Operator overloading limited to const-ref parameters and value returns.
* Array sizes are limited to 64-bit via `strtoull` parsing.
* Callable values (`func[...]` / `lambda[...]`) are code pointers only: no closures or captured environments.
* No pointers to callable types: `ptr[func[...]]` and `nullptr[func[...]]` are rejected.
* Arrays and lists of callable values are rejected; use a struct with a callable field if you need a container.
* Callable values support no comparisons or arithmetic/bitwise operators.
* `lambda[...]` → `func[...]` widening is explicit-only and requires an identical signature.
* Function-typed globals require an initializer that is a `func` or `lambda` declaration name (no indirect
  initialization).

These limits are tracked in `PROGRESS.md`.
