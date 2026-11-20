# 06 — handle

Demonstrates a `handle` declaration: a nominal type that owns a resource and is released by a bound drop
function when its scope ends. This demo uses the standard library's file descriptor handle, whose drop
function closes the descriptor:

```pp
handle PP_FD :: int32 drop pp_fd_close;
```

## The rule

A handle is a **scope-bound, non-transferable resource**:

* it is born in a `let`, from a call that returns the handle type;
* it is shared only by reference (`PP_FD&` / `const PP_FD&`) — never by value;
* it is dropped automatically at the end of the enclosing scope, on every path.

There is no explicit `drop h;` and no move. To release early, put the handle in an inner block.

## What the demo does

`main.pp` writes the eight bytes `AABBCCDD` to `test.txt`, then prints how many bytes were written. The
descriptor is opened with a flag value built from the standard library's `PP_OpenFlags`:

```pp
{
    let flags :: PP_OpenFlags = PP_OpenFlags::Write | PP_OpenFlags::Create | PP_OpenFlags::Truncate;
    let fd :: PP_FD = pp_fd_open("test.txt", flags, permission);
    let write_val :: int64 = pp_fd_write(fd, @data as ptr[const uint8], 8ul);
    ...
}   // fd is dropped (closed) here, on every path out of the block
```

`PP_OpenFlags` is declared in `lib/pp_std/extern/pp_io.ppi` as a `flag` over `uint32` and mirrors the POSIX
`O_*` bits. Read-only access is `PP_OpenFlags::none`, since `O_RDONLY` is zero:

```pp
flag PP_OpenFlags :: uint32 {
    Write = 1u,
    ReadWrite = 2u,
    Create = 64u,
    Exclusive = 128u,
    Truncate = 512u,
    Append = 1024u,
}
```

## The C side

`pp_fd_open` lives in `lib/pp_std/src/pp_io.c` and takes the same type name from `lib/pp_std/include/pp_io.h`:

```c
typedef uint32_t PP_OpenFlags;
PP_FD pp_fd_open(const pp_string_t* str, PP_OpenFlags flags, int32_t mode);
```

The header also defines `PP_O_WRITE`, `PP_O_CREAT`, `PP_O_TRUNC`, … for C callers; they must stay in sync with
the flag member values in the `.ppi`.

## Build and run

```bash
make && ./handle_demo        # prints "Wrote 8 bytes.", exit 0
valgrind ./handle_demo       # 0 errors, 0 leaks
make clean
```
