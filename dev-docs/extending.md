# 08 – Extending the Compiler

This guide covers adding new language features without breaking ownership and analysis invariants. Ownership rules are detailed in `notes/ownership_model.md`.

## Typical workflow
1. Update grammar in `notes/bnf.md` and add parser support.
2. Add AST node definitions in `include/pp_ast.h` and construction/free in `src/pp_ast.c`.
3. Extend semantic analysis in `src/pp_semantic_analyzer.c`:
   * Pass 1 if the feature introduces new global symbols/types.
   * Pass 2 for expression/statement checking and decoration.
4. Update code generation in `src/pp_code_generator.c` for new AST kinds.
5. Add tests in `tests/parsertest.cpp`, `tests/semantictest.cpp`, `tests/codegentest.cpp`.
6. Update user docs in `docs/` for new syntax and semantics.

## Things to keep in mind
* Types are immutable after creation and interned in the Global Type Table.
* String pool owns all identifiers/literals; never free them.
* Parent AST nodes own children; free the root to free the tree.
* Semantic analyzer decorates AST nodes in place; do not reallocate nodes after decoration.
* Const-correctness and reference rules are enforced centrally; respect them when adding new constructs.
* Temporary restrictions, e.g., move-only List, are documented in `docs/14-known-limitations.md`.

## Testing
Run `make unittest` after changes. Ensure both parser and semantic tests pass and that no new `#warning` directives are introduced unintentionally.
