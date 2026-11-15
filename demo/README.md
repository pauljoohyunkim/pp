# PP Language Demos

This directory contains small example programs for the PP language (`pplang`).

## Submodule note

The standard library is provided under `lib/pp_std/` as a Git submodule:

```
lib/pp_std
```

After a fresh clone you may need to initialise and update it:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

The demos depend on `lib/pp_std/bin/pp_std.a` which is built automatically by the demo Makefiles. If you change the submodule, run `make` inside `lib/pp_std` or re-run the demo make targets.

## Demos

* `01_fibo` – Fibonacci numbers using recursion
* `02_pi` – Pi approximation
* `03_eigenvector` – Eigenvector demo
* `04_pointers` – Pointer / reference example
* `05_bf` – Brainfuck via PPI

Each demo folder contains:
* `main.pp` – entry point
* `*.pp` / `*.ppi` – additional sources / pre-lexer inputs
* `Makefile` – builds the demo with `pplang`
* `README.md` – build & run instructions specific to the demo

### Common build steps

From a demo directory:

```bash
make          # builds the binary, e.g. fibo_demo
./fibo_demo   # run the binary
make clean    # remove the binary
```

The root Makefile builds `bin/pplang`. Demo Makefiles will invoke `make -C ../..` to ensure `pplang` is available before compiling.

## Adding a new demo

A minimal demo directory needs `main.pp` and a Makefile. See below for a wildcard-friendly Makefile pattern that avoids per-demo edits.
