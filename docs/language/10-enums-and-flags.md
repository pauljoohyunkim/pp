# Enums and Flags

`enum` and `flag` introduce closed, nominal sets of named constants backed by an integer type.
An `enum` describes a value chosen from a fixed list; a `flag` describes a set of independent
bits that can be combined with bitwise operators.

```pp
enum Mode :: uint8 { Read, Write, Append = 5uss }
flag Perms :: uint32 { Create, Truncate, Exclusive, Sync = 1u << 10u }
```

Both declarations introduce a **new nominal type**: it is not interchangeable with its underlying
integer type, nor with any other enum or flag.

## The declaration

The underlying type is mandatory and must be an integer primitive.

* `enum` may use a **signed or an unsigned** integer type up to 64 bits. When the enum mirrors a C enum, pick the
  type that matches the C declaration (`int8` vs `uint8`, and so on).
* `flag` must use an **unsigned** integer type up to 64 bits. A signed underlying type is rejected, because flag
  values are bit positions and every bit of the underlying width must be usable.

```pp
enum Status :: int8 { Idle, Running, Done }
flag Bits :: uint8 { A, B, C }
```

* Members are separated by commas; a trailing comma is allowed and the body may be empty.
* The body closes with `}` — there is no trailing semicolon.

### Values

Each member has a constant value of the **underlying type**. An explicit value is a typed constant, so
its literal carries the suffix of the underlying type:

| underlying type | suffix | examples |
|---|---|---|
| `int8` / `uint8` | `ss` / `uss` | `-1ss`, `255uss` |
| `int16` / `uint16` | `s` / `us` | `-1s`, `65535us` |
| `int32` / `uint32` | *(none)* / `u` | `-1`, `65535u` |
| `int64` / `uint64` | `l` / `ul` | `-1l`, `25ul` |

* An explicit value must be an expression of exactly the underlying type. A mismatch — an unsuffixed
  `1` in a `uint32` flag, or `1u` in a `uint8` enum — is an error, so a value is never silently
  reinterpreted.
* An explicit value may refer to members declared **earlier** in the same body. `+ - * / % << >> | & ^`
  and unary `-` / `~` are supported and keep the underlying type; arithmetic wraps at that type's width.
* Character literals such as `'+'` are `uint8` constants, so they are only accepted when the
  underlying type is `uint8`.
* An automatic `enum` value is `0` for the first member, then the previous value plus one.
* An automatic `flag` value is `1` for the first member, then the **next power of two strictly
  greater** than the previous member's value.

Values must be unique and **strictly increasing**, whether written explicitly or computed, compared in
the underlying type's signed or unsigned domain.

```pp
enum Mode :: uint8 { Read, Write, Append = 5uss }
flag Perms :: uint32 { Create, Truncate, Exclusive }
// Mode::Append is 5; Perms::Exclusive is 4
```

For flags, an explicit value must be a single bit (`1u << n`) or the OR of earlier flag members,
and must still be greater than the previous member's value:

```pp
flag Access :: uint32 { Read = 1u, Write = 2u, ReadWrite = Read | Write, Execute = 1u << 4u }
```

Because character literals are `uint8` constants, a `uint8`-backed enum can name the characters a
parser dispatches on — remember that values must still increase:

```pp
enum BFCommand :: uint8 { Plus = '+', Comma = ',', Minus = '-', Period = '.' }
```

A signed underlying type makes negative members expressible directly:

```pp
enum Delta :: int8 { Down = -2ss, Level = 0ss, Up = 1ss }
```

`Delta::Down` widens to `-2`. Values are stored in the bits of the underlying type, so an unsigned
enum still holds only non-negative numbers. Prefer an unsigned underlying type unless you are matching
an external signed declaration.

### `none`

`none` is reserved as a member name inside `flag` bodies. It is not a keyword, so it remains a legal
identifier everywhere else. Every flag type has a synthetic zero constant:

```pp
let p :: Perms = Perms::none;
```

## Using constants

Constants are constexpr and are written `Type::Member`. They may be used anywhere an integer
constant is expected, including array sizes and global initializers:

```pp
let buffer :: [uint8 : Mode::Append] = @"hell";

let default_mode :: Mode = Mode::Append;
```

## Operators

Enums support only equality comparison:

```pp
if (m == Mode::Append) { /* ... */ }
```

Flags support equality and the bitwise set operations:

```pp
let p :: Perms = Perms::Create | Perms::Sync;
p |= Perms::Truncate;
p &= ~Perms::Truncate;
p ^= Perms::Create;

if ((p & Perms::Sync) != Perms::none) { /* Sync is set */ }
if ((p & Perms::Sync) == Perms::none) { /* Sync is clear */ }
```

* `|` union, `&` intersection, `^` symmetric difference, `~` complement.
* Compound forms `|=`, `&=`, `^=` are available.
* Results stay within the same flag type; mixing two different enum or flag types is an error.
* Enum values support no arithmetic and no ordering.

Bit tests are written with the arithmetic operators above — `!= Perms::none` is the "is set"
idiom. There is no `has` operator.

## Casts

The nominal type and its underlying integer have the same layout, so casts exist for the boundary.

```pp
let raw :: uint32 = p as uint32;   // flag -> integer
let q :: Perms = raw as Perms;     // integer -> flag
```

* `enum -> integer` and `flag -> integer` are allowed.
* `integer -> flag` is allowed: every bit pattern of the underlying type is a meaningful set.
* `integer -> enum` is **not** allowed: an enum is closed, and an arbitrary integer is not one of
  its members. C functions that produce enum values must declare them with the enum type in their
  signature; the layout is the underlying integer.
* Casting between two different enum or flag types is not allowed.

## Uninitialized values

Like any integer, an enum or flag variable does not need an initializer:

```pp
let m :: Mode;
let p :: Perms;
```

Reading such a value before assigning it is as meaningless as for an uninitialized integer.

## A complete example

```pp
enum Mode :: uint8 { Read, Write, Append = 5uss }
flag Perms :: uint32 { Create, Truncate, Exclusive, Sync = 1u << 10u }

func describe :: (m :: Mode, p :: Perms) -> uint32 {
    let folded :: uint32 = (m as uint32) + (p as uint32);
    if ((p & Perms::Sync) != Perms::none) {
        folded |= 0x8000u;
    }
    return folded;
}

func main :: () -> int32 {
    let p :: Perms = Perms::Create | Perms::Sync;
    let buffer :: [uint8 : Mode::Append] = @"hell";
    return (describe(Mode::Append, p) + #buffer) as int32;
}
```

## In the standard library

`lib/pp_std/extern/pp_io.ppi` declares the POSIX open flags as a flag over `uint32`, so bit combinations are
passed to `pp_fd_open` without magic numbers:

```pp
flag PP_OpenFlags :: uint32 {
    Write = 1u, ReadWrite = 2u, Create = 64u, Exclusive = 128u, Truncate = 512u, Append = 1024u,
}

let flags :: PP_OpenFlags = PP_OpenFlags::Write | PP_OpenFlags::Create | PP_OpenFlags::Truncate;
let fd :: PP_FD = pp_fd_open("out.bin", flags, 420);   // 0644
```

Read-only access is `PP_OpenFlags::none`, because `O_RDONLY` is zero.

Enums serve the same purpose for selector values; `pp_fd_seek` takes one instead of a bare integer:

```pp
enum PP_Whence :: int32 { Set, Current, End }   // 0, 1, 2

pp_fd_seek(fd, 0ul, PP_Whence::End);
```

Enum and flag types are accepted directly as `extern` parameter types, so no cast is needed at the call site;
they use the ABI of their underlying integer type.
