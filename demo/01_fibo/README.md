# 01_fibo – Fibonacci Demo

Recursive Fibonacci using PP language.

## Files
* `main.pp` – program entry, prints first 10 Fibonacci numbers
* `fibo.pp` – `fibo` lambda definition
* `Makefile` – builds `fibo_demo`

## Build

```bash
make
# ensures bin/pplang exists, builds lib/pp_std, then runs pplang
```

## Run

```bash
./fibo_demo
```

Expected output: 1 1 2 3 5 8 13 21 34 55 each on its own line.

## Clean

```bash
make clean
```
