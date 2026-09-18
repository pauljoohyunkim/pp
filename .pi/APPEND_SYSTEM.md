# PP Compiler

This is a C-based successor of cz. Build and usage follow the same pattern as ../cz/ but with the pp language.

Detailed design, language features, pipeline and build instructions are in `notes/project_overview.md`.

Do not commit or push unless instructed to explicitly.

If you need date, always use `date` instead of relying on your memory.

## Clang Format
Whenever you are at a C/C++ repo, you should use clang-format:

```bash
clang-format -style="{BasedOnStyle: Google, IndentWidth: 4, ContinuationIndentWidth: 4}" -i main.cpp
```

## Memory Leak check
When testing using `pplang` binary, to check for memory leak, you can use `valgrind`:

```bash
valgrind --leak-check=yes ./bin/pplang pp_test_code.pp
```
