# PP Ownership Model

This note describes who owns what memory in the PP compiler so that double-free / use-after-free can be avoided.

## General rules
* **String Pool is the single owner of all source text.** Identifier names, literal lexemes and type names are interned via `pp_string_pool_push`. The returned `const char*` must never be freed by callers.
* **Free functions tolerate NULL.** Custom free helpers `pp_*_free` safely handle NULL. Do not guard the free call itself; guard only loops that dereference members.
* **Ownership transfer on error paths.** Pointers moved into a parent struct/array are nulled in the source variable to avoid double-free on `error_cleanup`.

## Components

### String Pool — `pp_string_pool.c`
* Owns: all interned `char*` strings.
* Lifetime: for the duration of a compilation unit, owned by the driver / semantic analyzer.
* Callers receive non-owning `const char*` references.

### Tokens
* Lexer allocates token structs and owns the token array.
* Parser consumes tokens; after successful parse the token array is freed by the driver.
* No AST node keeps a pointer to a `PP_Token` – only line/col numbers are copied.

### AST — `pp_ast.c`
* The parser owns the root `PP_AST_Node *program`.
* Parent nodes own children. `pp_ast_root_free` recursively frees the tree.
* Decorations `PP_AST_Decoration` are owned by the expression node that created them and freed with the node.
* Identifier strings inside AST nodes are pool-owned, never freed directly.
* Symbol tables attached to function / statement list nodes are owned by those nodes.

### Types
* **Global Type Table — `pp_type_table.c`** owns all `PPType*` instances.
  * Types created in Pass 1 are interned and become GTT-owned.
  * Callers hold non-owning pointers into the table.
* Types are immutable after creation. Const/Reference wrappers are also GTT-owned.

### Symbols
* **Global Symbol Table — `pp_symbol_table.c`** owns global symbols for variables, functions, lambdas, typedefs, newtypes, structs.
* Local symbol tables for function parameters and bodies are owned by the AST node that created them and freed with the AST.
* `PP_Symbol` does not own its type pointer – the type is GTT-owned.

### Semantic Analyzer — `pp_semantic_analyzer.c`
* Owns the error list for the compilation unit.
* Temporary structures such as `PP_StructLayout` are owned by the corresponding `PPType` after Pass 2.
* The analyzer does not own the AST; it annotates it in place.

### Parser / Driver
* `pplang.c` owns the top-level objects for a run: string pool, lexer state, parser AST root, semantic analyzer context.
* On successful compilation the AST and related tables are freed by the driver before exit.

## Common transfer patterns
* Lexer → Parser: token array ownership moves to parser, then discarded.
* Parser → Semantic Analyzer: AST root ownership stays with driver; analyzer annotates in place.
* Type creation → GTT: newly created `PPType` is interned; caller nulls its local pointer.
* String interning → everywhere: pool retains the only freeable copy.

## What NOT to do
* Do not `free()` a pool-owned string.
* Do not free a `PPType*` that came from the GTT.
* Do not free a symbol retrieved from a symbol table – the table owns it.
* Do not manually null-check before calling a custom `*_free`; the free function handles NULL. Only guard loops over arrays of pointers.
