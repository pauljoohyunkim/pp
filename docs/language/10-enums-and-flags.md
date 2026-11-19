# Enums and Flags

`enum` and `flag` introduce closed, nominal sets of named constants backed by an integer type.
An `enum` describes a value chosen from a fixed list; a `flag` describes a set of independent
bits that can be combined with bitwise operators.

```pp
enum Mode  :: uint8  { Read, Write, Append = 5 }
flag Perms :: uint32 { Create, Truncate, Exclusive, Sync = 1 << 10 }
```

Both declarations introduce a **new nominal type**: it is not interchangeable with its underlying
integer type, nor with any other enum or flag.

## The declaration

The underlying type is mandatory and must be an integer primitive. `enum` may use a signed or an
unsigned type; `flag` must use an unsigned one, because its values are bit positions.

```pp
enum Status :: int32  { Idle, Running, Done }
flag Bits   :: uint8  { A, B, C }
```

* Members are separated by commas; a trailing comma is allowed and the body may be empty.
* The body closes with `}` — there is no trailing semicolon.

### Values

Each member has an unsigned constant value that must fit the underlying type.

* An explicit value is any constant integer expression, and may refer to members declared
  **earlier** in the same body. `+ - * / % << >> | & ^` and unary `-` / `~` are supported, and
  character literals such as `'+'` stand for their code point (a `uint8` constant).
* An automatic `enum` value is `0` for the first member, then the previous value plus one.
* An automatic `flag` value is `1` for the first member, then the **next power of two strictly
  greater** than the previous member's value.

Values must be unique and **strictly increasing**, whether written explicitly or computed. This
makes an enum's numbering predictable and keeps flags ordered by bit position.

```pp
enum Mode  :: uint8  { Read, Write, Append = 5 }
flag Perms :: uint32 { Create, Truncate, Exclusive }
// Mode::Append is 5; Perms::Exclusive is 4
```

For flags, an explicit value must be a single bit (`1 << n`) or the OR of earlier flag members,
and must still be greater than the previous member's value:

```pp
flag Access :: uint32 { Read = 1, Write = 2, ReadWrite = Read | Write, Execute = 1 << 4 }
```

Because character literals are ordinary constants, an enum can name the characters a parser
dispatches on — remember that values must still increase:

```pp
enum BFCommand :: uint8 { Plus = '+', Comma = ',', Minus = '-', Period = '.' }
```

Values are unsigned even when the underlying type is signed; write `-1` only as an expression such
as `~0u`, and expect it to be rejected if it does not fit an unsigned interpretation of the width.

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
let buffer :: [uint8 : Mode::Append] = @"hello";

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
enum Mode  :: uint8  { Read, Write, Append = 5 }
flag Perms :: uint32 { Create, Truncate, Exclusive, Sync = 1 << 10 }

func describe :: (m :: Mode, p :: Perms) -> uint32 {
    let folded :: uint32 = (m as uint32) + (p as uint32);
    if ((p & Perms::Sync) != Perms::none) {
        folded |= 0x8000u;
    }
    return folded;
}

func main :: () -> int32 {
    let p :: Perms = Perms::Create | Perms::Sync;
    let buffer :: [uint8 : Mode::Append] = @"hello";
    return (describe(Mode::Append, p) + #buffer) as int32;
}
```
