# PP String Support – Option A + Heap String Progress

## Goal
Implement compile-time static string literals via `@"text"` that initialize `[uint8 : N]` arrays with NUL terminator included, and support heap-allocated `string` type with `"text"` literals. No `str` type; static strings are just `[uint8 : N]`. Lexer only tokenizes, semantic analyzer distinguishes static vs heap.

## Checklist

### Design & Docs
- [x] Document design decision: static literals via `@"text"` → `[uint8 : N]` with implicit NUL, heap `string` type with `"text"` literals
- [x] Update `docs/02-types.md` with array string literal initialization rule and `string` type
- [x] Add note in `notes/string_design.md` or `notes/v2-todo.md` about chosen approach
- [x] Clarify no `str` type; static strings are arrays only

### Lexer / Tokens
- [x] Add `PP_TT_STATIC_STRING_LITERAL` to `include/pp_tokens.h`
- [x] Map name in `src/pp_tokens.c`
- [x] Detect `@` prefix before `"` with **no whitespace** in `src/pp_lexer.c` → emit single `PP_TT_STATIC_STRING_LITERAL` token, do not split into `@` + `STRING_LITERAL`
- [x] Parse static string literal with same escape handling as normal string
- [x] Emit `PP_TT_STATIC_STRING_LITERAL` with correct lexeme; lexer does not interpret values
- [x] Unit test lexer for `@"hello"` and `@"he\"llo\n"`
- [x] Ensure plain `"..."` remains `PP_TT_STRING_LITERAL` for heap `string` type

### Parser
- [x] Verify `PP_TT_STATIC_STRING_LITERAL` is routed to `pp_parser_create_literal`
- [x] No AST changes required

### Semantic Analyzer
- [x] Extend `pp_semantic_analyzer_check_literal_expression` for `PP_TT_STATIC_STRING_LITERAL`
  - [x] Unescape lexeme, compute length = content bytes + 1 NUL
  - [x] Set `is_constexpr = true`
- [x] Extend `pp_semantic_analyzer_check_literal_expression` for `PP_TT_STRING_LITERAL`
  - [x] Handle heap `string` type, no NUL, runtime allocation
- [ ] Add array literal initialization check in variable/field initializer path
  - [x] Detect target type = `[uint8 : N]`
  - [x] Detect initializer = static string literal
  - [x] Compare `N` vs literal length, error if mismatch
  - [x] Error for too small: `let b :: [uint8 :5u] = @"hello";` must error
  - [x] Error for too large: `let c :: [uint8 :4u] = @"hello";` must error
- [ ] Ensure const correctness for global/static arrays
- [ ] Support heap `string` type
  - [x] Register `PP_TK_String` type kind in `include/pp_type.h`
  - [x] Register builtin type `string` in semantic init
  - [x] Update `pp_type_needs_drop`, `pp_type_is_trivially_copyable` for `String` → needs drop, not trivially copyable
  - [ ] Handle `PP_TT_STRING_LITERAL` for heap `string` with escaped length, no NUL
  - [x] Global variables of `string` only allowed empty, local allowed
  - [ ] Codegen for heap string literals: allocate + copy
- [x] Binary `+` and compound `+=` for `string`
  - [x] Extend `pp_semantic_analyzer_check_binary_expression` for `PP_TT_PLUS` with `PP_TK_String` operands
  - [x] Ensure `+=` works via assignment check


### Code Generator
- [ ] `pp_code_generator_generate_rvalue_literal` handle static string literal
  - [ ] Emit LLVM constant array `[N x i8]` with bytes + NUL
- [ ] Or transform literal to synthetic collection initializer to reuse existing codegen
- [ ] Verify global vs local constant handling

### Type System
- [x] Add `PP_TK_String` type kind for heap-allocated strings
- [x] Register builtin type `string` in semantic init
- [x] Update `pp_type_needs_drop`, `pp_type_is_trivially_copyable` for `String` → needs drop, not trivially copyable
- [x] Static strings use existing `PP_TK_Array` with `uint8` element

### Tests
- [ ] Lexer tests for `@` prefix
- [ ] Parser tests for literal node
- [ ] Semantic tests for valid initialization `let a :: [uint8 :6u] = @"hello";`
- [ ] Semantic tests for invalid size `let b :: [uint8 :5u] = @"hello";`
- [ ] Codegen test for LLVM constant `[6 x i8] c"hello\00"`
- [ ] C interop test: `ptr` from array is usable as C string

### Ownership / Interop
- [ ] Document C interop rule: NUL guaranteed only for literals initialized via `@` to `[uint8 : N]`
- [ ] No changes to ownership model required

## Current Status
- Design decided: static literals via `@"…"` → `[uint8 : N]` with implicit NUL, heap `string` type with `"…"` literals
- Lexer token decision: single `PP_TT_STATIC_STRING_LITERAL` token for `@"…"`
- Read-only analysis complete
- No code changes made yet

## Notes
- Static strings are just `[uint8 : N]` arrays, no `str` type
- `@` syntax mandatory for compile-time literals, no whitespace between `@` and `"`
- Lexer emits single `PP_TT_STATIC_STRING_LITERAL` token for `@"…"`, does not split
- NUL terminator included in `N`, exact match required
- Plain `"..."` literals are for heap `string` type, length is escaped bytes, no NUL
- Lexer only tokenizes, values interpreted in semantic analyzer
- Escape sequences supported: `\n \t \r \" \\ \xHH \0`
- `string` type requires copy/drop semantics; concatenation via `+` is future work
- No type inference in PP
