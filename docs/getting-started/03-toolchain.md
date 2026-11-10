# Toolchain

PP is built and run with a standard LLVM-based toolchain.

## Compiler

* C compiler – `clang` by default, `gcc` supported
  ```bash
  make CC=gcc
  ```
* LLVM – required for code generation and optimization
* `pkg-config` – used to discover LLVM compile and link flags

The compiler binary is `bin/pplang`.

## Standard Library

PP programs that use standard library functions must link `lib/pp_std/bin/pp_std.a` at build time:

```bash
./bin/pplang main.pp lib/pp_std/bin/pp_std.a -o program
```

Automatic stdlib linking is planned.

The standard library source lives in `lib/pp_std/` as a submodule. It can be built with:

```bash
make -C lib/pp_std
```

## Build System

The repository uses Make.

```bash
make clean && make
make unittest
```

Unit tests use Google Test and require `clang++`.

## Output Formats

* Executable: `./bin/pplang src.pp lib/pp_std/bin/pp_std.a -o out`
* Object file: `./bin/pplang -c -o out.o src.pp`
* Assembly: `./bin/pplang -S -o out.s src.pp`
* LLVM IR: `./bin/pplang --emit-llvm -o out.ll src.pp`

Next: [Language overview](../language/01-types.md)
