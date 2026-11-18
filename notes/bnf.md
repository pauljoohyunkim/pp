# BNF Grammar for PP Language

**Note**: This can frequently change as development progresses.

## BNF Grammar

```
<program>                   ::= <global_declaration_list>
<global_declaration_list>   ::= <global_declaration> <global_declaration_list> | ""
<global_declaration>        ::= <function_decl>
                              | <extern_function_decl>
                              | <lambda_decl>
                              | <extern_lambda_decl>
                              | <operator_decl>
                              | <extern_operator_decl>
                              | <struct_decl>
                              | <variable_decl> ";"
                              | <typedef_decl> ";"
                              | <newtype_decl> ";"
                              | <handle_decl> ";"

<function_signature>        ::= <identifier> "::" "(" <param_list> ")" "->" <type>
<lambda_signature>          ::= <identifier> "::" "(" <param_list> ")" "->" <type>

<function_decl>             ::= "func" <function_signature> <block>
<extern_function_decl>      ::= "extern" "func" <function_signature> ";"
<lambda_decl>               ::= "lambda" <lambda_signature> <block>
<operator_decl>             ::= "operator" <operator_symbol> "::" "(" <param_list> ")" "->" <type> <block>
<extern_operator_decl>      ::= "extern" "operator" <operator_symbol> "::" "(" <param_list> ")" "->" <type> ";"
<struct_decl>               ::= "struct" <identifier> "{" <struct_member_list> "}"
<struct_member_list>        ::= <struct_member> ";" <struct_member_list> | ""
<struct_member>             ::= <param_decl> [ "=" <expression> ]
<typedef_decl>              ::= "typedef" <base_type> <identifier>
<newtype_decl>              ::= "newtype" <base_type> <identifier>
<handle_decl>               ::= "handle" <identifier> "::" <type> "drop" <identifier>

<block>                     ::= "{" <statement_list> "}"
<statement_list>            ::= <statement> <statement_list> | ""
<statement>                 ::= <variable_decl> ";"
                              | <assignment_stmt> ";"
                              | <return_stmt> ";"
                              | <break_stmt> ";"
                              | <continue_stmt> ";"
                              | <if_stmt>
                              | <for_stmt>
                              | <while_stmt>
                              | <block>
                              | <expression> ";"

<variable_decl>             ::= "let" <param_decl> [ "=" <expression> ]

<assignment_stmt>           ::= <expression> <assignment_op> <expression>
<assignment_op>             ::= "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="

<return_stmt>               ::= "return" [ <expression> ]

;; --- Control Flow ---
<if_stmt>                   ::= "if" "(" <expression> ")" <block> [ "else" <else_branch> ]
<else_branch>               ::= <if_stmt> | <block>
<while_stmt>                ::= "while" "(" <expression> ")" <block>
<break_stmt>                ::= "break"
<continue_stmt>             ::= "continue"

<for_stmt>                  ::= "for" "(" [ <for_init> ] ";" [ <expression> ] ";" [ <for_update> ] ")" <block>
<for_init>                  ::= <variable_decl> | <assignment_stmt> | <expression>
<for_update>                ::= <assignment_stmt> | <expression>

<param_list>                ::= <param_decl> | <param_decl> "," <param_list> | ""
<param_decl>                ::= <identifier> "::" <type>

;; --- Type ---
<type>                      ::= <mutability> <collection_type> [ "&" ]
<mutability>                ::= "const" | ""
<collection_type>           ::= "[" <type> [ ":" <expression> ] "]"
                              | <pointer_type>
                              | <callable_type>
                              | <base_type>
<pointer_type>              ::= "ptr" "[" <type> "]"
<callable_type>             ::= "func"   "[" "(" <type_list> ")" "->" <type> "]"
                              | "lambda" "[" "(" <type_list> ")" "->" <type> "]"
<type_list>                 ::= <type> | <type> "," <type_list> | ""
<base_type>                 ::= "void"
                              | "int32"
                              | "uint32"
                              | "float"
                              | "bool"
                              | <identifier>

;; --- Expression ---
<expression>                ::= <logical_or>

<logical_or>                ::= <logical_or> "||" <logical_and>
                              | <equality>

<logical_and>               ::= <logical_and> "&&" <equality>
                              | <equality>

<equality>                  ::= <equality> "==" <relational> 
                              | <equality> "!=" <relational> 
                              | <relational>

<relational>                ::= <relational> "<"  <bitwise_or> 
                              | <relational> ">"  <bitwise_or> 
                              | <relational> "<=" <bitwise_or> 
                              | <relational> ">=" <bitwise_or> 
                              | <bitwise_or>

<bitwise_or>                ::= <bitwise_or> "|" <bitwise_xor> 
                              | <bitwise_xor>

<bitwise_xor>               ::= <bitwise_xor> "^" <bitwise_and> 
                              | <bitwise_and>

<bitwise_and>               ::= <bitwise_and> "&" <shift> 
                              | <shift>

<bitshift>                  ::= <bitshift> "<<" <additive>
                              | <bitshift> ">>" <additive>
                              | <additive>

<additive>                  ::= <additive> "+" <multiplicative> 
                              | <additive> "-" <multiplicative> 
                              | <multiplicative>

<multiplicative>            ::= <multiplicative> "*" <cast_expr> 
                              | <multiplicative> "/" <cast_expr> 
                              | <multiplicative> "%" <cast_expr> 
                              | <cast_expr>

<cast_expr>                 ::= <unary> "as" <type> 
                              | <unary>

<unary>                     ::= "#" <postfix> 
                              | "@" <postfix> 
                              | "$" <postfix> 
                              | "-" <postfix> 
                              | "!" <postfix> 
                              | <postfix>

<postfix>                   ::= <postfix> "(" <argument_list> ")"       ;; Function call or reserved builtin call
                              | <postfix> "." <identifier>              ;; Struct Member Access
                              | <postfix> "[" <expression> "]"          ;; Array/List Access
                              | <primary>

;; Reserved builtin calls
;; list_push(list, value)  – push value onto list
;; list_pop(list)          – pop and return element from list
;; list_clear(list)        – clear list


;; --- Primary Values ---
<primary>                   ::= <identifier>
                              | "NUMERICAL_LITERAL"
                              | "STRING_LITERAL"        ;; Not yet implemented.
                              | "TRUE"
                              | "FALSE"
                              | "nullptr" [ "[" <type> "]" ]
                              | "(" <expression> ")"
                              | <struct_init>
                              | <collection_init>

<struct_init>               ::= <identifier> "{" <struct_init_list> "}"
<struct_init_list>          ::= [ <struct_init_member> ( "," <struct_init_member> )* [ "," ] ]
<struct_init_member>        ::= "." <identifier> "=" <expression>

<elem_list>                 ::= <expression> | <expression> "," <elem_list> | ""
<collection_init>           ::= "[" <type> [ ":" <expression> ] ";" <elem_list> "]"

```

## Note
* Pointer support added: `ptr[T]`, `@` address-of, `$` deref, `nullptr` literal.
* Callable types (`func[...]`, `lambda[...]`): a callable signature is also a *value* type. A value of such a
  type is a plain pointer-sized code pointer.
  * Parameter **names** are not part of a callable type; only parameter types are. `<type_list>` above is used
    only inside `func[...]` / `lambda[...]`.
  * An empty parameter list `()` is allowed.
  * Higher-order signatures are allowed, e.g. `func[(func[(int32) -> float]) -> int32]`.
  * `const` and `&` apply to the binding exactly as for any other object: `const func[(...) -> T]`,
    `func[(...) -> T]&`.
  * `ptr[func[...]]` is allowed: a callable value is itself a code pointer, so a pointer to it is a pointer to
    the slot holding that code pointer. `ptr[lambda[...]]` is rejected (a lambda is a value, not addressable code).
    References to function-typed variables (`func[...]&`) remain allowed.
  * There is no implicit widening between `lambda[...]` and `func[...]`; an explicit
    `as func[(...) -> T]` cast with an identical signature (lambda -> func only) is the sole conversion.
  * Callable values support identity comparison `==` / `!=` between identical callable types, and nothing else;
    arithmetic/bitwise operators and the remaining comparisons are rejected.
  * Arrays and lists of callable values are allowed. Global non-empty list initializers remain banned by the
    general rule (not specific to callables).
