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
  - Install & Build
  - First Program
  - Toolchain
- [Language](language/)
  - Types
  - Variables & Const
  - Declarations
  - Expressions
  - Control Flow
  - Initialization
  - Functions & Lambdas
  - Operators
- [Examples](examples/)
  - Demos in `demo/`
- [Reference](reference/)
  - Grammar summary
  - Known Limitations

## Notes

Design notes and internal semantics are in `dev-docs/` and `notes/`.
