# Freestanding Mode

`--ffreestanding` compiles a PP program for an environment without the C heap. It removes every
language feature that would allocate, and guarantees that the generated object references no heap or
memory runtime symbols:

* no `malloc`, `calloc`, `realloc`, `free`, `memcmp`,
* no `memcpy`, `memset`, `memmove` (copies are emitted as explicit inline byte loops).

```bash
./bin/pplang --ffreestanding program.pp -o program
```

This is a **no-heap hosted mode**: the output is an ordinary object/executable linked through `cc`. It
is not a bare-metal target (no linker script or custom entry point). Use `-c`/`-S`/`--emit-llvm` as
usual.

## What stays available

* all primitives (`int8` … `uint128`, `float`, `bool`, `void`)
* structs, fixed-size arrays `[T : N]`, pointers `ptr[T]`, references `&`
* `typedef`, `newtype`, `enum`, `flag`
* functions, lambdas, operator overloads and `extern` declarations
* **static** string literals: `@"..."` producing `[uint8 : N]` (heap-free, NUL-terminated)
* `handle` with its user-bound drop function

## What is rejected

* the heap `string` type and heap string literals `"..."`
* list types `[T]`, list initializers `[T; ...]`, and `list_push` / `list_pop` / `list_clear`

```pp
let s :: string = "hello";          // error: string type not available
let t :: [uint8 : 3u] = "hi";       // error: heap string literal not available
let v :: [int32] = [int32; 1, 2];   // error: list type not available
```

Use a static literal for text data:

```pp
let s :: [uint8 : 6u] = @"hello";   // ok, fixed byte array
let v :: [int32 : 2u] = [int32 : 2u; 1, 2];
```

## Handles

Handles are unaffected: they own an external resource, not heap memory. The compiler still generates
`drop_T` helpers that call the handle's user drop function, and never clones a handle (`copy_T` treats
handles as move-only). See [Handles](09-handles.md).

```pp
handle File :: int32 drop pp_close;
extern func pp_open  :: () -> File;
extern func pp_close :: (fd :: int32) -> void;

func f :: () -> int32 {
    let file :: File = pp_open();   // dropped at the end of this scope
    return 0;
}
```

## I/O without `string`

Pass a pointer and a length instead of a `const string&`:

```pp
extern func write :: (fd :: int32, buf :: ptr[const uint8], n :: uint64) -> int64;
```

## Standard library

The `pp_std` interface files cannot be used in this mode:

* `pp_heap.ppi` is the C heap allocator,
* `pp_runtime.ppi` exposes `pp_get_argv -> [string]` and `pp_system :: (const string&)`,
* `pp_io.ppi` uses `const string&` in its entry points.

Their C implementations also call `malloc`/`realloc`/`free`. Declare the pointer + length externs you
need directly, as shown above.

## Optimization

`--ffreestanding` forces unoptimized IR and warns if `-O1`/`-O2`/`-O3` was requested. Higher
optimization levels can synthesize `llvm.memcpy`/`llvm.memset`, which may lower to libcalls that a
heap-free environment does not provide.

## Checking the result

```bash
./bin/pplang --ffreestanding -c program.pp -o program.o
nm -u program.o | grep -E 'mem(cpy|set|move)|malloc|calloc|realloc|free' || echo "clean"
```

Next: [Known Limitations](../reference/known-limitations.md)
