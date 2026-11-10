# First Program

Create `hello.pp`:

```pp
extern func pp_std_print_string :: (str :: string) -> void;

func main :: () -> int32 {
    pp_std_print_string("Hello, PP!\n");
    return 0;
}
```

Compile and run with the standard library:

```bash
./bin/pplang hello.pp lib/pp_std/bin/pp_std.a -o hello
./hello
```

> Note: Linking the standard library manually is required for now. Automatic stdlib linking is planned for a future release.

If you use the demo Makefiles, the standard library is passed automatically:

```bash
./bin/pplang main.pp fibo.pp lib/pp_std/bin/pp_std.a -o program
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
* `-o <file>` – output filename
* `-O <level>` – optimization level 0-3

Next: [Toolchain](03-toolchain.md)
