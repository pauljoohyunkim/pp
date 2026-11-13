# Checklist – Step 1: Driver accepts --ppi and prepends declaration tokens

Goal: Make `pplang` recognise `.ppi` interface files and prepend their tokens to each `.pp` compilation unit, without new language syntax.

## 1. Repo prep
- [x] Clean up duplicate in `lib/pp_std/extern/pp_io.ppi`
  - Remove duplicate `extern func pp_std_print_uint64 :: (val :: uint64) -> void;`
  - Renamed `pp_print.ppi` → `pp_io.ppi` and de-duplicated
- [ ] Create test `.ppi` file for unit tests, e.g. `tests/fixtures/test_decl.ppi`
  - Deferred: will rely on `lib/pp_std/extern/pp_io.ppi` for testing

## 2. Driver CLI
- [ ] Add long option `--ppi` to `src/pplang.c` getopt_long
  - Allow multiple `--ppi` occurrences → accumulate list
- [ ] Add short/long option `-I` for ppi include path? Decided later, keep simple for step 1
- [ ] Update `print_help()` to document `--ppi <file>`
- [ ] Parse args: build `PPI_InputList` parallel to `PPLang_InputList`

## 3. Filetype detection
- [ ] Extend `detect_filetype` in `src/pplang.c` to recognise `.ppi` as `PP_DECL` not source
- [ ] Keep `.pp` as `PP_SOURCE`, `.o/.a` as `PP_OBJECT`

## 4. Lexing & token prepending
- [ ] For each `--ppi` file:
  - [ ] Open file, get size
  - [ ] Create lexer `pp_lexer_create(code, path)`
  - [ ] Run `pp_lexer_analyze`
  - [ ] Collect token stream; verify no parse errors
- [ ] For each `.pp` source unit before parsing:
  - [ ] Lex `.ppi` files first, capture tokens
  - [ ] Lex `.pp` file
  - [ ] Concatenate token streams with original filename/line/col preserved
  - [ ] Option A: prepend tokens in memory before `pp_parser_create`
  - [ ] Option B: modify `pp_lexer` to accept multiple input buffers with source map
- [ ] Ensure diagnostics reference correct file name for each token
- [ ] **String pool ownership**
  - [ ] Move `PP_StringPool` ownership to orchestrator `src/pplang.c`
  - [ ] Pass shared pool to lexer for both `.ppi` and `.pp` files to avoid duplicate pools
  - [ ] Ensure lexemes from both files live in same arena after prepending

## 5. Parser / AST handling
- [ ] Parser should accept extern declarations from prepended tokens seamlessly
- [ ] No change to grammar needed
- [ ] **Merging strategy**
  - [ ] Merge at parser level via token prepending, not AST merge in semantic analyzer
  - [ ] Single parse pass over combined token stream → single AST
  - [ ] Avoid duplicate string pools and AST merge complexity

## 6. Semantic merge rules
- [x] Allow identical redeclaration of extern symbols from prelude + source → already implemented in `pp_semantic_analyzer_build_global_table_extern_function_decl` / `..._extern_lambda_decl`
- [x] Error on signature mismatch or kind mismatch `func` vs `lambda` → already implemented
- [x] Allow extern followed by definition in same unit if signatures match → already implemented for function forward declarations
- [ ] Enforce declarations-only in `.ppi` files
  - [ ] After parsing `.ppi`, walk AST and reject nodes that are definitions with bodies
  - [ ] Allowed in `.ppi`: `extern func/lambda/operator`, `struct`, `newtype`, `typedef`, `enum` etc. – i.e., type and declaration nodes without bodies
  - [ ] Emit error: `.ppi` files may contain only declarations, no function/lambda bodies or variable definitions

Note: `.ppi` can contain non-extern declarations such as `struct`, `newtype`, `typedef`. The restriction is no definitions with bodies.

## 7. Linking
- [ ] No change to linking; `.ppi` files produce no object code

## 8. Testing
- [ ] Unit test: single `.ppi` with one extern, compile `.pp` that uses it
- [ ] Unit test: duplicate identical extern from `.ppi` + source → succeeds
- [ ] Unit test: conflicting signature from `.ppi` + source → error
- [ ] Unit test: `.ppi` containing a definition → error
- [ ] Unit test: diagnostic line/col points to `.ppi` file
- [ ] Integration test: demo `01_fibo` compiled with `--ppi lib/pp_std/extern/pp_io.ppi` and manual externs removed

## 9. Demo Makefiles
- [ ] Update one demo Makefile to use `--ppi`
  - Example: `$(PPLANG) --ppi $(PP_STD)/extern/pp_io.ppi main.pp $(PP_STD)/bin/pp_std.a -o $@`
- [ ] Verify build succeeds and output matches previous build

## 10. Documentation
- [ ] Update `docs/getting-started/02-first-program.md` with `--ppi` usage example
- [ ] Update `README.md` Quickstart options list
- [ ] Add note to `notes/extern_module_plan.md` about implementation status

## 11. Cleanup
- [ ] Remove temporary debug prints
- [ ] Run `make clean && make`
- [ ] Run `make unittest`
- [ ] Check no regressions

## Acceptance criteria
- [ ] `pplang --ppi lib/pp_std/extern/pp_io.ppi main.pp lib/pp_std/bin/pp_std.a -o prog` builds demos without manual `extern` lines in source
- [ ] Errors from `.ppi` files report correct filename
- [ ] Duplicate identical externs are merged silently
- [ ] Conflicting externs produce clear error
- [ ] No new language syntax introduced

---
Owner: extern/module management
Status: pending
