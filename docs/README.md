# PP Language Documentation

PP is a general purpose programming language built on three philosophies: dumb-sounding, simple yet usable, explicit and safe.

This documentation is for users writing PP programs.

## Quick Start

```bash
# Build the compiler
make clean && make

# Compile a PP source file
./bin/pplang -o hello hello.pp
./hello

# Compile to object
./bin/pplang -c -o hello.o hello.pp
```

See [Getting Started](getting-started/01-install-build.md) for prerequisites.

## Standard Library

PP's standard library `pp_std` is maintained separately. It provides libc bindings and common utilities.
The submodule is initialized with:
```bash
git submodule update --init --recursive
```
See the [pp_std repository](https://github.com/pauljoohyunkim/pp_std) for details.

## Structure

- [Getting Started](getting-started/)
  - [Install & Build](getting-started/01-install-build.md)
  - [First Program](getting-started/02-first-program.md)
  - [Toolchain](getting-started/03-toolchain.md)
- [Language](language/)
  - [Types](language/01-types.md)
  - [Variables & Const](language/02-variables-and-const.md)
  - [Declarations](language/03-declarations.md)
  - [Expressions](language/04-expressions.md)
  - [Control Flow](language/05-control-flow.md)
  - [Initialization](language/06-initialization.md)
  - [Functions & Lambdas](language/07-functions-and-lambdas.md)
  - [Operators](language/08-operators.md)
- [Examples](examples/index.md)
- [Reference](reference/)
  - [Grammar](reference/grammar.md)
  - [Known Limitations](reference/known-limitations.md)

## Notes

Design notes and internal semantics are in `dev-docs/` and `notes/`.
