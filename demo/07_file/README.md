# 07_file – PP_File Handle Demo

Demonstrates `PP_File` handle wrapping stdio for writing and reading files.

## Files
* `main.pp` – opens a file for write, writes `AABBCCDD`, then reopens for read and prints contents
* `Makefile` – builds `file_demo`

## Build

```bash
make
```

## Run

```bash
./file_demo
```

Expected output:
```
Wrote 8 bytes.
Read 8 bytes: AABBCCDD
```

A file `file_test.txt` is created in the demo directory during the run. The Makefile `clean` target removes it.

## Clean

```bash
make clean
```
