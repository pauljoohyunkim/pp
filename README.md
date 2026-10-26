# PP

PP is a general purpose programming language that is built on three philosophies:
 * Dumb-sounding
 * Simple yet usable
 * Explicit and safe

You can think of this language as a amalgamation of the simplicity of the C language, strong type guarantee of rust language, functional programming type signature of Haskell language, and a bit of unicorn fart for good luck.

This repository is a compiler attempted in C with LLVM backend.

## Building Compiler

You need the following tools to build this compiler:

* C compiler
  * By default, configured to be `clang` but you can use `gcc`
* LLVM Library
* Pkg-Config
  * Technically not needed, I use it to load LLVM compilation and linker flags.
* Make
  * Technically not needed, but it makes the compilation process easier

### Quickstart
```
# Assuming you have all the prerequisites
make clean && make

# If you want to use gcc instead
make clean && make CC=gcc
```

You should be able to find the binary in `bin/pplang`.

You can optionally build unit test (but you will need gtest)
```
make unittest
```
You should find the unit test binary at `bin/unittest`.

