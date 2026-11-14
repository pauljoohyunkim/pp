# First Program

Create `hello.pp`:

```pp
func main :: () -> int32 {
    pp_std_print_string("Hello, PP!\n");
    return 0;
}
```

Compile and run with the standard library using PPI:

```bash
./bin/pplang --ppi lib/pp_std/extern/pp_io.ppi hello.pp lib/pp_std/bin/pp_std.a -o hello
./hello
```

> With `--ppi`, external PPI declarations are injected automatically. Manual `extern` declarations in source can be used if you do not want to import the entire PPI file..

If you use the demo Makefiles, the standard library is passed automatically:

```bash
./bin/pplang --ppi lib/pp_std/extern/pp_io.ppi main.pp fibo.pp lib/pp_std/bin/pp_std.a -o program
```

## Compiling to object / assembly / LLVM IR

```bash
./bin/pplang -c -o hello.o hello.pp
./bin/pplang -S -o hello.s hello.pp
./bin/pplang --emit-llvm -o hello.ll hello.pp
```

## Options

* `-h` – show help
* `-v` – verbose output
* `-c` – compile to object file
* `-S` – compile to assembly file
* `--emit-llvm` – emit LLVM IR
* `--ppi <file>` – pre-processor include file for external PPI declarations, can be repeated
* `-o <file>` – output filename
* `-O <level>` – optimization level 0-3

Next: [Toolchain](03-toolchain.md)
