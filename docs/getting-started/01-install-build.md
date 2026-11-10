# Install & Build

## Prerequisites

* C compiler – `clang` by default, `gcc` works too
* LLVM library – required for backend code generation
* `pkg-config` – used to load LLVM compile/link flags
* `make`

## Clone

```bash
git clone <repo url>
cd pp
git submodule update --init --recursive
```

## Build

```bash
make clean && make
```

The compiler binary will be at `bin/pplang`.

Use gcc instead of clang:

```bash
make clean && make CC=gcc
```

Unit tests require Google Test:

```bash
make unittest
bin/unittest
```

## Verify

```bash
./bin/pplang -h
```

You should see the help output.
