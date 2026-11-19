# Hex & Character Literal Implementation Checklist

## Grammar / Docs
- [x] Update `notes/bnf.md` to include hex literal syntax and char literal syntax
- [ ] Update `docs/` if needed

## Lexer / Tokens
- [x] Add `PP_TT_CHAR_LITERAL` to `include/pp_tokens.h`
- [x] Update `pp_token_to_human_name` in `src/pp_tokens.c`
- [x] Implement hex literal detection in `src/pp_lexer.c` `isdigit` branch
- [x] Implement character literal detection in `src/pp_lexer.c` switch default (single quote)
- [x] Handle escapes in char literal: `\n \t \r \\ \' \xHH \0`
- [x] Ensure suffix handling works for hex literals (`u/U`, `s/l`)
- [x] Add lexer unit tests in `tests/lexertest.cpp`

## Parser / AST
- [x] Add `PP_TT_CHAR_LITERAL` case to primary literal handling in `src/pp_parser.c`
- [x] Ensure AST node creation for char literal (reuse integer literal or new node)

## Semantic Analyzer
- [x] Handle `PP_TT_CHAR_LITERAL` and infer type `uint8`
- [x] Handle hex integer literals type inference
- [x] Update type checking for suffixes

## Code Generator
- [x] Emit correct LLVM constant for char literal as i8
- [x] Emit correct constant for hex integer literal

## Tests
- [ ] `make unittest`
- [x] Add specific tests for hex and char literals
- [ ] Valgrind leak check

## Validation
- [ ] `make -j`
- [ ] `bin/unittest`
- [ ] Update notebook with results
