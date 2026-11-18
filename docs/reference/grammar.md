# Grammar Reference

This is a high-level summary. See `notes/bnf.md` for full BNF.

## Programs

```ebnf
program ::= { declaration }
declaration ::= function | lambda | struct | variable | operator | extern
```

## Statements

```ebnf
statement ::= variable_declaration | assignment | return | if_stmt | while_stmt | for_stmt | block | expression_stmt
```

## Expressions

```ebnf
expression ::= literal | identifier | call | operator_call | cast | member_access | indexing | collection_initializer
```

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
