# Grammar Reference

This is a high-level summary. See `notes/bnf.md` for full BNF.

## Programs

```ebnf
program ::= { declaration }
declaration ::= function | lambda | struct | typedef | newtype | enum | flag | variable | operator | extern | handle
```

```ebnf
handle ::= "handle" identifier "::" type "drop" identifier ";"
```

`handle` and `drop` are reserved keywords. See [Handles](../language/09-handles.md).

## Statements

```ebnf
statement ::= variable_declaration | assignment | return | if_stmt | while_stmt | for_stmt | block | expression_stmt
         | break_stmt | continue_stmt
```

```ebnf
break_stmt ::= "break" ";"
continue_stmt ::= "continue" ";"
```

## Expressions

```ebnf
expression ::= literal | identifier | call | operator_call | cast | member_access | indexing | collection_initializer
```

### Literals

```ebnf
literal ::= integer_literal | float_literal | char_literal | string_literal | static_string_literal | bool_literal
integer_literal ::= decimal_integer | hex_integer
decimal_integer ::= ["-"?]? digits [suffix]
hex_integer ::= "0x" hex_digits [suffix]
suffix ::= ["u"|"U"] ["s"|"l" {"s"|"l"}]
char_literal ::= "'" char_or_escape "'"
char_or_escape ::= escape_sequence | non_quote_char
escape_sequence ::= "\\" ["n"|"t"|"r"|"0"|"'"|"\\"|"x" hex_digit hex_digit]
```

* Hex integer literals start with `0x`/`0X` and may use the same suffixes as decimal integers. Example: `0x41u`, `0xDEADBEEF`.
* Character literals are single-quoted and automatically typed as `uint8`. Escapes supported: `\n`, `\t`, `\r`, `\0`, `\'`, `\\`, `\xHH`.


## Types

```ebnf
type ::= base | array | list | ptr | func_type | lambda_type | struct | operator
array ::= "[" type ":" size "]"
list ::= "[" type "]"
ptr ::= "ptr" "[" type "]"
func_type ::= "func" "[" "(" [ type { "," type } ] ")" "->" type "]"
lambda_type ::= "lambda" "[" "(" [ type { "," type } ] ")" "->" type "]"
```

Callable types describe values (pointer-sized code pointers). Parameter names are not allowed inside them, an empty
parameter list is allowed, and higher-order signatures such as `func[(func[(int32) -> float]) -> int32]` are
valid.

Full BNF: `notes/bnf.md`
