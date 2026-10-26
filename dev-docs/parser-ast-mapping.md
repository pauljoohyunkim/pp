# 06 – Parser → AST Mapping

This guide explains how the recursive-descent parser turns source text into AST nodes. It complements `notes/bnf.md` which documents the language grammar.

## Parser entry points
* `pp_parser_parse_program` builds the root `PP_AST_ProgramNode` from the token stream.
* Top-level declarations are parsed into:
  * `PP_AST_FunctionDeclarationNode`
  * `PP_AST_LambdaDeclarationNode`
  * `PP_AST_StructDeclarationNode`
  * `PP_AST_VariableDeclarationNode`
  * `PP_AST_TypedefDeclarationNode`
  * `PP_AST_NewtypeDeclarationNode`

## Key mappings
* `type` → `PP_AST_TypeExpressionNode` containing a `TypeBase`, `TypeArray` or `TypeList` node, with `is_const`/`is_reference` flags.
* `param_decl` → `PP_AST_ParamDeclarationNode` with identifier and type expression.
* `struct_member` → field stored in `struct_declaration.fields[]` with optional initializer AST node.
* `block` → `PP_AST_StatementListNode` with owned child statements.
* `expression` → precedence climbing builds binary/unary nodes, postfix nodes for calls/member/array access, `PP_AST_StructInitializerNode` and `PP_AST_CollectionInitNode`.

## Ownership during parsing
* Parser allocates AST nodes via `calloc`.
* Parent owns children; freeing the root frees the tree.
* Identifier strings and literals are interned in the string pool; AST holds non-owning pointers.
* After parsing, ownership of the AST root transfers to the semantic analyzer; the parser discards token array.

## Extending
* Add new AST node types in `include/pp_ast.h` and corresponding constructors/free.
* Update parser functions to produce the new nodes.
* Ensure semantic analyzer and codegen handle the new node kind.
* Add parser tests in `tests/parsertest.cpp`.

See `notes/bnf.md` for full grammar and `src/pp_parser.c` for implementation details.
