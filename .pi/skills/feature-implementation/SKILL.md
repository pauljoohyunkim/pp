---
name: feature-implementation
description: Reproducible workflow for implementing language/compiler features in PP.
---

# Feature Implementation Skill

This skill provides a reproducible workflow for implementing language/compiler features in PP.

## Scope
* Analyze existing repo to understand current design, grammar, types, codegen.
* Capture analysis in notebook.
* Create a checklist for the feature.
* Implement step-by-step, ticking boxes.

## Workflow

### 1. Analyze
* Read `notes/bnf.md`, relevant docs in `docs/`, headers in `include/`, and source in `src/`.
* Understand how similar features are implemented.
* Capture findings in a notebook page.

### 2. Design
* Decide on AST nodes, semantic checks, codegen strategy.
* Choose reserved identifiers vs keywords.
* Document design decisions.

### 3. Checklist
Create a markdown checklist file, e.g. `FEATURE_CHECKLIST.md`:
* Grammar / Docs
* Lexer / Tokens
* Parser / AST
* Semantic Analyzer
* Code Generator
* Runtime / Stdlib
* Tests
* Validation

### 4. Implement
Tick boxes one at a time:
* Update `notes/bnf.md`
* Add AST enums/structs in `include/pp_ast.h`
* Parser changes in `src/pp_parser.c`
* Semantic checks in `src/pp_semantic_analyzer.c`
* Codegen in `src/pp_code_generator.c`
* Runtime helpers in `lib/pp_std`
* Tests in `tests/`

After each step, build and run tests.

### 5. Validate
* `make -j`
* `bin/unittest`
* Valgrind leak check
* Update notebook with results.

## Success criteria
* Feature works as specified
* Tests pass
* No regressions in Valgrind
* Docs updated
