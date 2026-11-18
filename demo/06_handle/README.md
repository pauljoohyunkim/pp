# 06 — handle

Demonstrates a `handle` declaration: a nominal type that owns a resource and is released by a bound drop
function when its scope ends.

```pp
handle File :: int32 drop pp_demo_close;

extern func pp_demo_open_write :: () -> File;
```

## The rule

A handle is a **scope-bound, non-transferable resource**:

* it is born in a `let`, from a call that returns the handle type;
* it is shared only by reference (`File&` / `const File&`) — never by value;
* it is dropped automatically at the end of the enclosing scope, on every path.

There is no explicit `drop h;` and no move. To release early, put the handle in an inner block.

## What the demo does

`main.pp` writes the eight bytes `12345678` to `pp_handle_demo.txt`, then opens the same file for reading,
reads the bytes back into a `[uint8 : 8u]` array and prints them.

```pp
func write_file :: () -> int32 {
    let f :: File = pp_demo_open_write();
    return pp_demo_write(f as int32, "12345678");
}

func read_file :: () -> int32 {
    let f :: File = pp_demo_open_read();
    let buf :: [uint8 : 8u] = [uint8 : 8u; 0uss, 0uss, 0uss, 0uss, 0uss, 0uss, 0uss, 0uss];

    let n :: int32 = pp_demo_read(f as int32, @buf as ptr[uint8], 8u as uint64);
    if (n != 8) {
        return 1;   // the handle is dropped on this path too
    }

    pp_demo_print(@buf as ptr[const uint8], 8u as uint64);
    return 0;       // and on this one
}
```

Two things are worth spotting:

* `f as int32` — a handle can be cast *down* to its underlying type to call C code. The reverse cast (int32 to
  `File`) does not exist: a handle can only be born from a function that returns one.
* `@buf as ptr[uint8]` — the address of the array, cast to a pointer to its first element.

`main` asserts that `pp_demo_close_count() == 2`, i.e. both handles were dropped exactly once, and that the
round trip succeeded.

## The C side

`handle_demo.c` mirrors the shape of `lib/pp_std/src/pp_print.c`: `pp_demo_write` receives a `const string&`,
which arrives as a `pp_string_t*` exactly like `pp_std_print_string_ref`. Closes are counted so the PP side can
assert the drop count. Nothing here is added to `pp_std`.

## Build and run

```bash
make && ./handle_demo        # prints 12345678, exit 0
valgrind ./handle_demo       # 0 errors, 0 leaks
make clean
```
