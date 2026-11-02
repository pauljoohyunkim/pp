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
                              | <struct_decl>
                              | <variable_decl> ";"
                              | <typedef_decl> ";"
                              | <newtype_decl> ";"

<function_signature>        ::= <identifier> "::" "(" <param_list> ")" "->" <type>
<lambda_signature>          ::= <identifier> "::" "(" <param_list> ")" "->" <type>

<function_decl>             ::= "func" <function_signature> <block>
<extern_function_decl>      ::= "extern" "func" <function_signature> ";"
<lambda_decl>               ::= "lambda" <lambda_signature> <block>
<extern_lambda_decl>        ::= "extern" "lambda" <lambda_signature> ";"
<struct_decl>               ::= "struct" <identifier> "{" <struct_member_list> "}"
<struct_member_list>        ::= <struct_member> ";" <struct_member_list> | ""
<struct_member>             ::= <param_decl> [ "=" <expression> ]
<typedef_decl>              ::= "typedef" <base_type> <identifier>
<newtype_decl>              ::= "newtype" <base_type> <identifier>

<block>                     ::= "{" <statement_list> "}"
<statement_list>            ::= <statement> <statement_list> | ""
<statement>                 ::= <variable_decl> ";"
                              | <assignment_stmt> ";"
                              | <return_stmt> ";"
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

<for_stmt>                  ::= "for" "(" [ <for_init> ] ";" [ <expression> ] ";" [ <for_update> ] ")" <block>
<for_init>                  ::= <variable_decl> | <assignment_stmt> | <expression>
<for_update>                ::= <assignment_stmt> | <expression>

<param_list>                ::= <param_decl> | <param_decl> "," <param_list> | ""
<param_decl>                ::= <identifier> "::" <type>

;; --- Type ---
<type>                      ::= <mutability> <collection_type> [ "&" ]
<mutability>                ::= "const" | ""
<collection_type>           ::= "[" <type> [ ":" <expression> ] "]" | <base_type>
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

<unary>                     ::= "-" <postfix> 
                              | "!" <postfix> 
                              | <postfix>

<postfix>                   ::= <postfix> "(" <argument_list> ")"       ;; Function call
                              | <postfix> "." <identifier>              ;; Struct Member Access
                              | <postfix> "[" <expression> "]"          ;; Array/List Access
                              | <primary>

;; --- Primary Values ---
<primary>                   ::= <identifier>
                              | "NUMERICAL_LITERAL"
                              | "STRING_LITERAL"        ;; Not yet implemented.
                              | "TRUE"
                              | "FALSE"
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
* TODO: Pointer
