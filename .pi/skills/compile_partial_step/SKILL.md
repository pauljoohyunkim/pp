---
name: compile-partial-step
description: Build and run pplang with STOP_AT_* defines to halt compilation at lexer, parser, or semantic analyzer stages.
---

# Compile Partial Step

Use this skill when you need to build and run `pplang` stopping at a specific compilation stage.

## Available stop points
The driver `src/pplang.c` supports compile-time defines mirroring `../cz/src/czc.c`:

* `STOP_AT_LEXER` – run lexer only, skip parser and semantic analyzer
* `STOP_AT_PARSER` – run lexer + parser, skip semantic analyzer
* `STOP_AT_SEMANTIC_ANALYZER` – run lexer + parser + semantic analyzer, stop before any later phases

## How to build
Via Makefile:
```bash
make pplang CFLAGS_EXTRA=-DSTOP_AT_LEXER
make pplang CFLAGS_EXTRA=-DSTOP_AT_PARSER
make pplang CFLAGS_EXTRA=-DSTOP_AT_SEMANTIC_ANALYZER
```

Via VS Code tasks:
* `Build pplang (Stop at Lexer)`
* `Build pplang (Stop at Parser)`
* `Build pplang (Stop at Semantic Analyzer)`

Then run with launch configs:
* `pplang pp_test_code.pp (Stop at Lexer)`
* `pplang pp_test_code.pp (Stop at Parser)`
* `pplang pp_test_code.pp (Stop at Semantic Analyzer)`

## Typical workflow
1. Choose the desired stop point.
2. Build with the corresponding `CFLAGS_EXTRA`.
3. Run `./bin/pplang <file.pp>` or use the matching VS Code launch config.
4. Inspect output; cleanup is performed automatically via `error_cleanup_exit`.

## Notes
* The driver always frees allocated `lexer`, `parser`, `sa` safely on exit.
* For memory leak checks: `valgrind --leak-check=yes ./bin/pplang pp_test_code.pp`
* Do not commit unless instructed.
