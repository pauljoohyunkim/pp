# Developer Documentation

This directory is for contributors who want to understand and extend the PP compiler.

## Getting Started
* Build: `make pplang`
* Test: `make unittest`
* Run compiler: `./bin/pplang path/to/file.pp`

## Architecture Overview
The compiler is a classic multi-pass pipeline:
* **Lexer** → token stream
* **Parser** → AST
* **Semantic Analyzer** → type checking, symbol tables, const-correctness
* **Code Generator** → LLVM IR

Core concepts:
* String pool owns all source text. Never free pool pointers.
* Global Type Table owns all types; types are interned, not duplicated.
* Global Symbol Table owns globals; local tables are owned by AST nodes.
* Ownership is transferred explicitly between stages; free functions tolerate NULL.

## How to Contribute
1. Read the high-level notes in `notes/` for design rationale.
2. Pick a component: lexer, parser, semantic analysis, codegen.
3. Run unit tests before and after changes.
4. Keep user docs in `docs/` in sync with semantic changes.
5. Document new constraints in `docs/` semantics pages, not only in code.

## Key Development Topics
* Grammar and parsing – evolving BNF, parser implementation notes
* Semantic analysis – two-pass analysis, type construction, constraints
* Ownership model – who owns what, transfer rules, common patterns
* Build system and testing

See:
* `parser-ast-mapping.md` – Parser → AST mapping and extending syntax
* `semantic-analysis.md` – Pass 1 / Pass 2 checks
* `extending.md` – Workflow for adding language features

These guides avoid listing individual source files to stay resilient to refactorings.

## Notes
Design notes are in `notes/`. They capture rationale, decisions, and open questions. User-facing docs are in `docs/`.
