# PP String Support Design Spec

## Overview

PP does not have a `str` type. Static strings are represented as fixed-size byte arrays `[uint8 : N]`. Heap-allocated strings use the `string` type.

Two literal forms exist:
* `@ "text"` — compile-time static string literal for `[uint8 : N]` arrays
* `"text"` — heap-allocated `string` literal

The lexer is value-agnostic; it only tokenizes. Semantic analysis distinguishes static vs heap literals and enforces size rules.

## Syntax

### Static string literal
```
@ "unescaped_content"
```
* `@` prefix directly adjacent to `"` with no whitespace.
* Produces a compile-time constant byte array.
* Implicit NUL terminator is added.
* Exact size match required: `N == unescaped_content_bytes + 1`

### Heap string literal
```
"unescaped_content"
```
* No prefix.
* Produces a `string` value.
* Length = escaped byte count, no NUL terminator.
* Global variables of type `string` may only be initialized with empty literal `""`. Non-empty heap literals are permitted only for locals.

## Lexer Rules

* Token types:
  * `STATIC_STRING_LITERAL`
  * `STRING_LITERAL`
* Lexer recognizes `@` followed immediately by `"` as start of `STATIC_STRING_LITERAL`.
  * No whitespace allowed between `@` and `"`; otherwise `@` is separate token and `"` starts `STRING_LITERAL`.
* Lexer does not interpret escape sequences; it captures raw source between quotes including escapes.
* Both literals support standard C-like escapes: `\n \t \r \" \\ \xHH \0` etc.
* Unterminated quotes are lexical error.
* Lexer does not compute length or unescape.

## Semantic Rules

### Type distinction
* `STATIC_STRING_LITERAL` is only valid where a `[uint8 : N]` array is expected, typically for initialization of a static byte array.
* `STRING_LITERAL` is only valid where a `string` type is expected.

### Static literal size check
* For `@ "text"` initializing `[uint8 : N]`:
  * Compute `unescaped_bytes` = number of bytes after interpreting escape sequences.
  * Required `N == unescaped_bytes + 1` for implicit NUL.
  * If mismatch, error.
* Static literals cannot be used for `string` type.

### Heap literal rules
* For `"text"` initializing `string`:
  * Length = `unescaped_bytes` (no NUL).
* Global `string` variables:
  * Only allowed initializer is `""` (empty heap literal).
  * Non-empty heap literal at global scope is error.
* Local `string` variables may be initialized with any heap literal.

### Escape handling
* Semantic analyzer unescapes both literal forms identically for byte count:
  * `"a\nb"` → bytes: `a` `0x0A` `b` → 3 bytes for heap, 4 bytes for static incl. NUL.
  * `"a\0"` → bytes: `a` `0x00` → 1 byte for heap, static requires array size 3 because content has explicit NUL plus implicit NUL: `'a'`, `0x00`, implicit `0x00`.
* Supported escapes: `\n \t \r \" \\ \xHH \0`.
* Escape sequences are interpreted at semantic analysis, not lexing.

## Codegen Rules

### Static string
* `@ "text"` → emit constant byte array of size `N` with NUL terminator.
* At compile time, layout is contiguous `[uint8 : N]` in static data segment.
* No runtime allocation.

### Heap string
* `"text"` → allocate on heap at runtime with length = `unescaped_bytes`.
* No NUL terminator appended.
* Empty literal may be optimized to null/empty singleton.

## Error Messages

* `static_string_size_mismatch`: `@ "text"` requires array size N == unescaped_bytes + 1, got N.
* `static_string_literal_used_for_string`: Static literal `@ "..."` cannot be used for type `string`.
* `heap_string_literal_used_for_static_array`: Heap literal `"..."` cannot initialize `[uint8 : N]`.
* `global_string_non_empty_init`: Global variable of type `string` can only be initialized with empty literal `""`.
* `invalid_static_string_prefix_spacing`: `@` must be immediately followed by `"` with no whitespace.
* `unterminated_string_literal`: String literal not closed.

## Examples

### Valid static
```pp
var msg : [uint8 : 5] = @ "hi\n";   // 2 + 1 + 1 NUL = 4? Wait compute: h i \n =3 bytes + NUL =4 -> error
var msg : [uint8 : 4] = @ "hi\n";   // ok
var buf : [uint8 : 5] = @ "abc";    // a b c + NUL =4 -> size mismatch error
var buf : [uint8 : 4] = @ "abc";    // ok
```

### Valid heap
```pp
var s : string = "hello\n";        // length 6, no NUL
var t : string = "";               // ok
var g : string = "";               // global ok
// var g2 : string = "x";          // error: global non-empty
```

### Escape handling
```pp
@ "a\nb"   // unescaped bytes = 3 → requires [uint8 : 4]
"a\nb"     // heap length = 3
@ "a\\b"   // unescaped bytes = 2 → requires [uint8 : 3]
@ "a\0"    // unescaped bytes = 2 → requires [uint8 : 3] (explicit \0 + implicit NUL)
```

### Lexer prefix rule
```pp
@ "ok"     // STATIC_STRING_LITERAL
@  "bad"   // `@` token + STRING_LITERAL → error later
```

No code changes; spec only.
