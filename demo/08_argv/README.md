# 08_argv – Command-line Arguments Demo

Demonstrates retrieving command-line arguments via `pp_get_argv`, which returns a `List(string)` owned by the runtime.

## Files
* `main.pp` – prints `argc` and each `argv[i]` as a string
* `Makefile` – builds `argv_demo`

## Build

```bash
make
```

## Run

```bash
./argv_demo
./argv_demo foo bar baz
```

Expected output without arguments:
```
argc = 1
argv[0] = "argv_demo"
```

With arguments:
```
argc = 4
argv[0] = "argv_demo"
argv[1] = "foo"
argv[2] = "bar"
argv[3] = "baz"
```

## Clean

```bash
make clean
```
