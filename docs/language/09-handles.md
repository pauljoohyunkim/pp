# Handles

A `handle` is a type that owns an external resource — a file descriptor, an allocated block, a library handle —
and releases it automatically when the value goes out of scope.

```pp
handle File   :: int32     drop pp_file_close;
handle Buffer :: ptr[void] drop EVP_free;
```

The declaration introduces a new nominal type (like `newtype`) and binds a drop function to it. The drop
function is called by the compiler; you never call it yourself.

## The rule

A handle is a **scope-bound, non-transferable resource**:

* it is **born** in a `let`, from a call that returns the handle type;
* it is **shared** only by reference (`File&`, `const File&`) — never by value;
* it **dies** at the end of the enclosing scope, on every path.

```pp
handle File :: int32 drop pp_demo_close;

extern func pp_demo_open  :: () -> File;
extern func pp_demo_write :: (fd :: int32, text :: const string&) -> int32;
extern func pp_demo_close :: (fd :: int32) -> void;

func write_line :: () -> int32 {
    let f :: File = pp_demo_open();     // born here
    return pp_demo_write(f as int32, "hello\n");
}                                       // dropped here, even on the return path
```

There is no `drop` statement and no move. To release a handle early, put it in an inner block:

```pp
func scoped :: () -> void {
    let a :: File = pp_demo_open();     // lives to the end of the function
    {
        let b :: File = pp_demo_open(); // released at the end of this block
    }
    return;
}
```

## Using the underlying value

A handle can be cast **down** to its underlying type, which is how you hand it to C:

```pp
let f :: File = pp_demo_open();
let raw :: int32 = f as int32;          // read-only view, ownership unchanged
```

Casting *to* a handle is not allowed: a handle can only be created by a function that returns one.

## Passing handles to functions

Parameters take a borrow, never the handle itself:

```pp
func inspect :: (f :: const File&) -> int32 {   // borrow: caller keeps ownership
    return 0;
}

func consume :: (f :: File) -> int32 {          // error: handle passed by value
    return 0;
}
```

A callee that borrows never drops. Borrows can be passed onwards to other borrowing functions.

## Writing the drop function

The drop function receives the underlying value and returns nothing. It must be declared either as a PP
function or as an `extern` function:

```pp
// Implemented in C, resolved by the linker.
extern func pp_file_close :: (fd :: int32) -> void;

handle File :: int32 drop pp_file_close;
```

```pp
// Or implemented in PP.
extern func pp_os_close :: (fd :: int32) -> void;

func my_close :: (fd :: int32) -> void {
    pp_os_close(fd);
}

handle File :: int32 drop my_close;
```

Rules:

* the signature must be `(underlying) -> void` with exactly one parameter;
* the parameter is the underlying value, never a handle;
* the function must be a `func` definition or an `extern func` declaration — not a lambda, an operator, or a
  variable of callable type;
* the drop function cannot be called from source code, nor stored in a `func[...]` variable. The compiler is its
  only caller, which is what guarantees exactly one release per handle.

If the drop function is `extern`, the compiler checks that it is declared; a missing implementation is reported
by the linker.

## Reassignment

A handle variable may be reassigned. The old resource is released before the new value is stored:

```pp
let f :: File = pp_demo_open();
f = pp_demo_open();     // previous descriptor closed, then the new one stored
```

## Pointer-backed handles

When the underlying type is a pointer, a null value is not passed to the drop function. This makes "no resource"
representable without a special case in user code:

```pp
handle Buffer :: ptr[void] drop EVP_free;

extern func alloc_or_null :: () -> Buffer;

func maybe :: () -> void {
    let b :: Buffer = alloc_or_null();  // if this returns null, drop is skipped
    return;
}
```

For non-pointer underlyings the drop function is always called; there is no reserved "empty" value.

## Restrictions

| Position | Allowed |
|---|---|
| Local variable with initializer | yes |
| Parameter as `H&` / `const H&` | yes |
| Return type `-> H`, returned from a call | yes |
| Bare `H` parameter or argument | no |
| Global variable | no |
| Struct field | no |
| Collection element (`[H]`, `[H : n]`) | no |
| Lambda parameter, return type or local | no |
| Cast target (creating a handle) | no |

A handle needs an initializer: `let f :: File;` is an error. Returning a handle *local* is also an error —
`return pp_demo_open();` is the supported form, because ownership then passes to the caller.

The underlying type may be a scalar, an array, a struct or a pointer, but it may not be a reference, a callable
type, or a type that transitively contains a reference or another handle.

## Interoperating with C APIs that take ownership

A C function that takes ownership of a resource cannot be expressed as a drop function, because that would mean
the resource is released twice. Wrap it in PP so the ownership transfer happens inside the wrapper:

```pp
extern func EVP_PKEY_CTX_set0_rsa_oaep_label :: (ctx :: ptr[void], label :: ptr[uint8], len :: int32) -> int32;

func set_label :: (ctx :: ptr[void], label :: ptr[uint8], len :: int32) -> int32 {
    // the wrapper takes responsibility for `label` before handing it over
    return EVP_PKEY_CTX_set0_rsa_oaep_label(ctx, label, len);
}
```

## See also

* [Declarations](03-declarations.md) — where `handle` fits beside `typedef` and `newtype`
* [Types](01-types.md) — nominal types, references, pointers
* Design rationale and rejected alternatives: `notes/handle_design.md`

Next: [Expressions](04-expressions.md)
