# Known Limitations

* Standard library documentation is deferred for beta. See `pp_std` repository for stdlib.
* No modules yet; all code is in single files.
* No templates/generics.
* Global variables must be constexpr; no runtime globals.
* Reference types cannot be stored in collections.
* Lambdas are pure: cannot call regular functions or capture mutable globals.
* Operator overloading limited to const-ref parameters and value returns.
* Array sizes are limited to 64-bit via `strtoull` parsing.

These limits are tracked in `PROGRESS.md`.
