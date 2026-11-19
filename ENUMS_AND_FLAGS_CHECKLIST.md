# Enums & Flags Implementation Checklist

## Grammar / Docs
- [x] Update `notes/bnf.md` with `enum_decl` / `flag_decl` and `Type::Member` access note
- [ ] Add `docs/language/10-enums-and-flags.md`
- [ ] Update VS Code syntax highlighting for `enum` / `flag` / `~`

## Lexer / Tokens
- [x] Add `PP_TT_ENUM`, `PP_TT_FLAG`, `PP_TT_TILDE` to `include/pp_tokens.h`
- [x] Add human-readable names in `src/pp_tokens.c`
- [x] Lex `enum`, `flag` keywords and `~` in `src/pp_lexer.c`
- [x] Lexer tests

## Types
- [x] Add `PP_EnumKind`, `PP_EnumMember`, newtype fields, helpers in `include/pp_type.h` + `src/pp_type.c`
- [x] Free member table in `pp_type_free`

## Parser / AST
- [x] Add `PP_AST_EnumDeclarationNode` and `PP_AST_FlagDeclarationNode` (fully separate, inline union structs, parallel member arrays) in `include/pp_ast.h`
- [x] Decoration fields `is_enum_constant` / `enum_constant_value`
- [x] Parse declaration and add to global dispatch + sync tokens
- [x] Parse unary `~`
- [x] Parser tests

## Semantic Analyzer
- [x] Pass 1: register enum/flag type, validate underlying (integer; unsigned for flag)
- [x] Resolve member values; auto numbering; duplicate name/value rules; fit checks
- [x] `pp_array_size_from_simple_expr`: accept `Type::Member`
- [x] Qualified constant: `Type::Member` lookup (incl. `Flag::none`)
- [x] Binary operators: enum `==/!=`, flag `== != | & ^`
- [x] Compound assignment: flag `|= &= ^=`
- [x] Unary `~` for integer primitives and flag
- [x] Cast: reject enum/flag → different enum/flag
- [x] Semantic tests

## Code Generator
- [ ] Declaration emits nothing
- [ ] Enum constant member access → `LLVMConstInt` of lowered underlying type
- [ ] `~` unary lowering
- [ ] Debug info: `DW_TAG_enumeration_type` with enumerators
- [ ] Codegen tests

## Validation
- [x] `make -j`
- [x] `bin/unittest`
- [ ] Demo program
- [ ] Valgrind leak check
- [ ] Update notebook with results
