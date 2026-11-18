# Handle Declaration Implementation Checklist

Feature: `handle HANDLE :: underlying drop free_func;`
Type-introducing declaration, same family as `newtype` and `typedef`.

## Design Decisions
- Grammar: `<handle_decl> ::= "handle" <identifier> "::" <type> "drop" <identifier> ";"`
- Reserved words: `handle` reserved; `drop` contextual keyword in this position (or reserved)
- Pass 1: register handle name in `named_types`, create PPType with new Handle kind carrying underlying type and drop function name string
- Pass 2: resolve `free_func` in value namespace, check signature `func[(underlying) -> void]`, non-lambda. Store resolved symbol on type
- Type guards: reject const/reference underlying, reject underlying function type
- Namespace: same as structs/newtypes/typedefs, reuse duplicate-name check
- Drop generation: `drop_T` for handle calls bound free_func then sentinel/null rule
- Copyability: `type_is_copyable` returns false for handle
- Lowering/debug: lower underlying type, emit typedef for DWARF
- `.ppi` support required
- Lambdas: ban handle types in lambda signatures/bodies via `type_contains_handle`
- Move-only semantics: copy banned (lvalue-source init, assignment, by-value arg). Rvalue sources and return local are moves
- Passing: functions take const Handle& for borrowing, or move for ownership transfer
- Construction: only via explicit cast/conversion from T (`fd as File`). Extract raw via reverse cast
- Null/sentinel: skip drop on null for pointer-backed handles; moved-from handles set to sentinel and skip drop. Reassignment drops old value then stores new
- Containers: lists of handles allowed, drop per element on pop; banned in lambdas; globals only if constexpr sentinel
- Struct fields: recursive type_needs_drop automatic, struct copy banned if contains handle
- Debug/C boundary: emit as typedef of underlying, pass raw T to C

## Checklist

### Grammar / Docs
- [x] Update `notes/bnf.md` `<global_declaration>` to include `<handle_decl> ";"`
- [x] Add `<handle_decl>` production
- [ ] Update `docs/` language guide for handle semantics
- [ ] Document `.ppi` requirements for handle + free_func extern

### Lexer / Tokens
- [x] Add `handle` keyword token
- [x] Add `drop` keyword token
- [x] Update keyword table in `src/pp_lexer.c`

### Parser / AST
- [ ] Add AST node enum for `AST_HANDLE_DECL` in `include/pp_ast.h`
- [ ] Add struct fields: name, underlying_type, drop_func_name
- [ ] Parser support in `src/pp_parser.c`: parse `handle_decl`
- [ ] Update `global_declaration` parsing
- [ ] Add parser tests

### Semantic Analyzer
- [ ] Pass 1: register handle in `named_types`, create PPType with kind Handle
- [ ] Store underlying type, drop_func_name string
- [ ] Duplicate name check vs structs/newtypes/typedefs
- [ ] Type guards: reject const/reference underlying, reject function underlying
- [ ] Pass 2: resolve `free_func` in value namespace
- [ ] Validate signature `func[(underlying) -> void]`, non-lambda
- [ ] Store resolved symbol on type
- [ ] Add `type_contains_handle` check
- [ ] Ban handle in lambda signatures/bodies
- [ ] Update `type_is_copyable` to false for handle
- [ ] Enforce move-only: ban copy from lvalue, by-value param, assignment without move
- [ ] Allow explicit cast from underlying to handle and reverse cast
- [ ] Handle null/sentinel rules for drop skipping
- [ ] Recursive copyability for structs containing handles
- [ ] Update `notes/general_coding_rule.md` / `ownership_model.md` if needed

### Code Generator
- [ ] Lower handle type to underlying for DWARF typedef
- [ ] Drop generation: emit call to bound free_func for handle values
- [ ] Ensure drop not skipped for handles, with null/sentinel check
- [ ] Emit move semantics: clear source to sentinel on move
- [ ] Ensure reassignment drops old value then stores new
- [ ] Debug info emission for handle type
- [ ] C boundary: pass raw underlying T to C calls

### Runtime / Stdlib
- [ ] No runtime helpers required, but document free_func contract

### Tests
- [ ] Unit tests for parser: valid/invalid handle decl
- [ ] Semantic tests: name collision, const underlying reject, func underlying reject, bad free_func signature reject
- [ ] Codegen tests: drop calls free_func exactly once per handle
- [ ] ASan-test early return, loop, struct field containing handle
- [ ] Reassign test: old value dropped, new stored
- [ ] Move test: source cleared to sentinel
- [ ] List pop test with counter in C drop function to confirm each handle closes exactly once
- [ ] Valgrind leak check for handle usage demo
- [ ] Add demo in `demo/` for handle usage with examples `File` and `Evp`

### Validation
- [ ] `make -j`
- [ ] `bin/unittest`
- [ ] Valgrind leak check passes
- [ ] Notebook updated with results

## Success Criteria
- Feature works as specified
- Tests pass
- No regressions in Valgrind
- Docs updated
