# Default Integer Types & Suffix Literal Plan

Status: Draft – suffix mapping and suffix order confirmed.

## Scope
Add int8/uint8, int16/uint16, int64/uint64, int128/uint128 to PP compiler.
Literal suffixes: `s`, `ss`, `l`, `ll` with optional `u` prefix.
`u` always precedes size suffix. Case-insensitive.

Mapping confirmed:
- `s`  → int16
- `ss` → int8
- `l`  → int64
- `ll` → int128
- `lll` → error
Signed variants default; unsigned variants: `us`, `uss`, `ul`, `ull`.

## Checklist

### Design & Spec
- [x] Finalize suffix → type mapping table
- [x] Decide on error policy for `lll` and unknown suffixes
- [x] Define numeric promotion rules for mixed widths
  - No implicit promotion; all conversions explicit via `as`
- [x] Document suffix syntax in `docs/`

### Lexer
- [x] Extend digit parsing in `src/pp_lexer.c` to consume optional `u` prefix + size suffix
- [x] Implement longest-match for `s`/`ss` and `l`/`ll`
- [x] **Unified integer literal token**: lexer emits single `PP_TT_INTEGER_LITERAL` for signed/unsigned
- [ ] Preserve full lexeme for semantic analysis — done, but verify token enum removal for `PP_TT_UNSIGNED_INTEGER_LITERAL` is no longer needed
- [x] Unit tests for lexer suffix variants
- [x] Add token enum entries for new integer types in `include/pp_tokens.h`
- [x] Update lexer keyword matching for new integer type names
- [x] Update `pp_token_to_human_name` for new tokens
- [x] Add unit tests for new integer type keywords

### Parser
- [x] Add AST type kinds for new integer types in `include/pp_ast.h`
- [x] Update parser type mapping to new token types
- [x] Update semantic mapping from AST type kind to PPTypeKind
- [x] Add `PP_TK_Int8`, `PP_TK_UInt8`, `PP_TK_Int16`, `PP_TK_UInt16`, `PP_TK_Int64`, `PP_TK_UInt64`, `PP_TK_Int128`, `PP_TK_UInt128` to `include/pp_type.h`
- [x] Update `pp_type_is_numerical` in `src/pp_type.c`
- [x] Update `pp_type_is_trivially_copyable_impl` in `src/pp_type.c`
- [x] Update `pp_type_equal` primitive list in `src/pp_type_table.c`
- [x] Update `pp_type_decay` and `pp_type_decay_cg` switches

### Semantic Analyzer
- [x] Register aliases `int8`, `uint8`, `int16`, `uint16`, `int64`, `uint64`, `int128`, `uint128` in `pp_semantic_analyzer_create`
- [x] Extend `pp_semantic_analyzer_check_literal_expression` to parse lexeme suffix + `u` flag → concrete type
- [x] Centralize suffix parsing in `pp_type_parse_int_literal_suffix` in `src/pp_type.c`
- [x] Expose `pp_type_kind_is_unsigned` helper for type kind checks
- [x] Reject `lll` suffix with error
- [x] Update `pp_type_from_type_node` for new type keywords if needed
- [x] Ensure array size expression handling works with new unsigned widths
- [x] Update binary expression type checking to require same integer type for arithmetic ops, e.g., int32+int32, int8+int8
- [x] Update unary expression handling for new integer types
- [x] Update cast expression validation for new integer types
- [x] Ensure simple constant expression evaluation respects 64-bit limit for array sizes

### Code Generator
- [x] Extend `pp_backend_lower_type` to map new kinds to `LLVMInt8/16/64/128`
- [x] Refactor `pp_code_generator_generate_rvalue_literal` to use decoration type for LLVM constant width/signedness
- [x] Use `pp_type_parse_int_literal_suffix` for numeric stripping instead of manual parsing
- [ ] Implement 128-bit decimal literal parsing decision
  - [x] Array size expressions restricted to ≤64-bit
  - [ ] Option A: Full __int128 parser for literals
  - [x] Option B: Restrict 128-bit literals to fit in 64-bit for now with `#warning` stub
- [x] Update binary/unary signedness checks for new unsigned kinds
- [x] Update compound assignment codegen for signedness
- [ ] Verify LLVM i128 target support

### Testing & Validation
- [x] Add lexer tests for suffix combos: `123s`, `123ss`, `123l`, `123ll`, `u123s`, `u123ss`, `u123l`, `u123ll`
- [ ] Add semantic tests for literal type resolution
- [ ] Add codegen tests for each width
- [ ] Update existing int32/uint32 tests for regression

### Documentation
- [x] Update `docs/02-types.md` with new integer types and suffix literal syntax
- [x] Add examples for suffix literals

## Open Questions
- Decimal parsing for 128-bit literals: Full __int128 parsing TBD. Array size expressions restricted to ≤64-bit per decision.
- Numeric promotion rules: No implicit promotion; explicit `as` casts only.
- Should `lll` be error or map to int128? Confirmed error.
- 128-bit integer codegen: WIP stub with `#warning`; full `__int128` literal parsing and constant emission TBD.

## Notes
Created at 2026-11-06. Awaiting confirmation on decimal parsing scope.
