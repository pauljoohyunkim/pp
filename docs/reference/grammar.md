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
type ::= base | array | list | struct | function | lambda | operator
array ::= "[" type ":" size "]"
list ::= "[" type "]"
```

Full BNF: `notes/bnf.md`
