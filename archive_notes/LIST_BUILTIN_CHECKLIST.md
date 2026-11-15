# LIST_BUILTIN_CHECKLIST.md

Goal: Add built-in list mutation operations `list_push`, `list_pop`, `list_clear` as reserved builtins.

These are treated as **separate from normal function calls** – dedicated AST nodes and semantic checks for clearer validation.

## Phase 0 – Design decisions
- [ ] Confirm AST node types for builtins: `PP_AST_ListPushNode`, `PP_AST_ListPopNode`, `PP_AST_ListClearNode`
- [ ] Decide if `list_*` are reserved identifiers or keywords. Chosen: reserved identifiers, special-cased.
- [ ] Confirm list runtime layout `{ data_ptr, len, cap }` exists in codegen.

## Phase 1 – Grammar / Docs
- [x] Update `notes/bnf.md`
  - Add note under Expression/Postfix that `list_push`, `list_pop`, `list_clear` are reserved builtin calls.
  - Document signature in language reference.
- [ ] Update `docs/language/01-types.md` and getting-started with builtin usage examples.

## Phase 2 – Lexer / Tokens
- [ ] No new token needed – identifiers `list_push`, `list_pop`, `list_clear` are lexed as `PP_TT_IDENTIFIER`.
- [ ] Add reservation list in lexer/parser: identifiers that are builtin reserved.

## Phase 3 – Parser & AST
- [x] `include/pp_ast.h`
  - Add AST node enums `PP_AST_ListPushNode`, `PP_AST_ListPopNode`, `PP_AST_ListClearNode`.
  - Define structs:
    - `list_push { PP_AST_Node* list; PP_AST_Node* value; }`
    - `list_pop  { PP_AST_Node* list; }`
    - `list_clear{ PP_AST_Node* list; }`
- [x] `src/pp_ast.c`
  - Add free and print handling for the three new nodes.
- [x] `src/pp_parser.c`
  - Detect reserved builtin identifiers in postfix call parsing and build dedicated AST nodes with arity checks.
  - In `pp_parser_create_postfix`, after identifier parsing for call, detect reserved builtin names.
  - If name matches `list_push` → parse 2 args, build `PP_AST_ListPushNode`.
  - If name matches `list_pop` → parse 1 arg, build `PP_AST_ListPopNode`.
  - If name matches `list_clear` → parse 1 arg, build `PP_AST_ListClearNode`.
  - Ensure they are not emitted as normal `PP_AST_CallNode`.

## Phase 4 – Semantic Analyzer
- [x] `include/pp_semantic_analyzer.h`
  - Add reserved builtin name constants.
- [x] `src/pp_semantic_analyzer.c`
  - Add dispatch cases for `PP_AST_ListPushNode`, `PP_AST_ListPopNode`, `PP_AST_ListClearNode`.
  - Implement checks: mutable lvalue list, element type assignability, return type for pop.
  - Reserved name enforcement can be added later.

## Phase 5 – Code Generator
- [x] `src/pp_code_generator.c`
  - Add handling for `PP_AST_ListPushNode`, `PP_AST_ListPopNode`, `PP_AST_ListClearNode` in `pp_code_generator_generate_rvalue`.
  - Stubs implemented returning undef for now.
  - TODO: Emit calls to runtime helpers:

## Phase 6 – Runtime / Stdlib
- [ ] Add runtime helpers to `lib/pp_std` or inline in codegen:
  - `pp_list_push`, `pp_list_pop`, `pp_list_clear` with grow logic.
- [ ] Ensure proper drop of elements on pop/clear.

## Phase 7 – Tests
- [x] `tests/parsertest.cpp`
  - Tests for parsing `list_push`, `list_pop`, `list_clear` with correct arity and error on wrong arity.
- [x] `tests/semantictest.cpp`
  - Tests for valid push/pop/clear, wrong type, const list rejection.
- [ ] `tests/codegentest.cpp`

## Phase 8 – Validation
- [ ] Build `make -j`
- [ ] Run `bin/unittest`
- [ ] Valgrind leak check on unit tests and demos.
- [ ] Update notebook page `valgrind-validation`.

## Notes
- Builtins are separate AST nodes, not generic function calls, to allow dedicated semantic checks and clearer error messages.
- Reserved names prevent user shadowing.
- Future extension to `list_len`, `list_insert`, etc. follows same pattern.
