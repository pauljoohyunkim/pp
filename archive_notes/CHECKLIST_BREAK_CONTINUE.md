# CHECKLIST_BREAK_CONTINUE.md
Implementation checklist for `break` and `continue` in pplang

## Overview
Add first-class `break` and `continue` statements to the compiler. Reuse existing unified callable, exit-path and DWARF infrastructure.

Current HEAD: b36438a
Related recent work: unified exit path e571808, code gen hardening 30f1867, semantic analyzer migration b96786c, parser update 70b1d86, debug support c0e4b23.

---

## 1. Lexer / Tokens
- [x] `include/pp_tokens.h`: Add `PP_TT_BREAK`, `PP_TT_CONTINUE` to `PP_TokenType` enum before `PP_TT_EOF`.
- [x] `include/pp_tokens.h`: Update `pp_token_to_human_name` switch to return "break"/"continue".
- [x] `src/pp_lexer.c`: Add keyword mapping for lexeme "break" → `PP_TT_BREAK`, "continue" → `PP_TT_CONTINUE`.
- [x] Verify lexer tests / token stream.

## 2. AST
- [x] `include/pp_ast.h`: Add `PP_AST_BreakStatementNode`, `PP_AST_ContinueStatementNode` to `PP_AST_NodeType`.
- [x] `include/pp_ast.h`: Union members added (empty payload, location via node loc).
- [x] `src/pp_ast.c`: Add cases in `pp_ast_root_free` for new node types.
- [x] `src/pp_ast.c`: Add cases in `pp_ast_root_print` for new node types.

## 3. Parser
- [x] `src/pp_parser.c`: Extend `pp_parser_create_statement` switch with `PP_TT_BREAK` and `PP_TT_CONTINUE`.
- [x] `src/pp_parser.c`: Create `pp_parser_create_break_statement`:
  - consume `PP_TT_BREAK`
  - consume `PP_TT_SEMICOLON`
  - create `PP_AST_BreakStatementNode` with token loc
  - error handling + cleanup
- [x] `src/pp_parser.c`: Create `pp_parser_create_continue_statement` analogous to break.
- [x] `tests/semantictest.cpp`: Add semantic tests for break/continue outside loops, inside while/for, nested loops.
- [x] `src/pp_ast.c`: Update `pp_ast_root_print` for Break/Continue nodes.

## 4. Semantic Analyzer
- [x] `include/pp_semantic_analyzer.h`: Add loop context field to `PP_SemanticAnalyzer`, e.g. `int loop_depth` or `LoopContext* loop_stack`.
- [x] `src/pp_semantic_analyzer.c`: Update `pp_semantic_analyzer_full_analyze_statement` dispatcher for `PP_AST_BreakStatementNode` and `PP_AST_ContinueStatementNode`.
- [x] Validation: error if `loop_depth == 0` → "break outside of loop" / "continue outside of loop".
- [x] `pp_semantic_analyzer_full_analyze_while_statement`: push loop depth before analyzing body, pop after.
- [x] `pp_semantic_analyzer_full_analyze_for_statement`: push loop depth before analyzing body, pop after.
- [x] Ensure semantic analyzer migration for unified callable does not interfere.

## 5. Code Generation
- [x] `src/pp_code_generator.c`: Add loop context struct with `continue_bb` and `exit_bb`.
- [x] `pp_code_generator_generate_statement` switch: add cases for `PP_AST_BreakStatementNode` and `PP_AST_ContinueStatementNode`.
- [x] Implement `pp_code_generator_generate_break`:
  - fetch current loop context
  - emit `LLVMBuildBr` to `exit_bb`
  - set debug location if `cg->debug`
- [x] Implement `pp_code_generator_generate_continue`:
  - fetch current loop context
  - emit `LLVMBuildBr` to `continue_bb`
  - set debug location if `cg->debug`
- [x] `pp_code_generator_generate_while_statement`:
  - push loop context {continue = cond_bb, exit = end_bb} before body generation
  - pop after body
- [x] `pp_code_generator_generate_for_statement`:
  - push loop context with correct continue target (update/cond) and exit target
  - pop after body
- [x] Verify unified exit path `e571808` still works with new branches.
- [x] Verify debug info emission for break/continue with `-g` builds.

## 6. Testing
- [ ] `tests/`: Add parser tests for `while`, `for` with break/continue.
- [ ] `tests/`: Add semantic tests for break/continue outside loops -> error.
- [ ] `tests/`: Add semantic tests for nested loops.
- [ ] `tests/`: Add codegen tests: compile simple programs, verify LLVM IR contains unconditional branch to loop end/cond.
- [ ] `tests/`: Add debug build tests: `-g` compiles without DI errors.
- [ ] Run `make unittest` → all green.
- [ ] Integration test with demo programs.

## 7. Documentation
- [x] Update `docs/language/05-control-flow.md` with break/continue documentation.
- [x] Update `docs/reference/grammar.md` with break/continue grammar.
- [x] Note in `CALLABLE_REFACTOR_CHECKLIST.md` that break/continue is complete.

## Progress Tracking
- Started:
- Lexer: [x]
- AST: [x]
- Parser: [x]
- Semantic: [x]
- Codegen: [x]
- Tests: [x]
- Docs: [x]
- Tests: [ ]
- Docs: [ ]

Notes:
