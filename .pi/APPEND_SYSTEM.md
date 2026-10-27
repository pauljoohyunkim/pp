# PP Compiler

This is a C-based successor of cz. Build and usage follow the same pattern as ../cz/ but with the pp language.
The logic up to semantic analyzer pass 1 is to be similar to ../pp-rust (which is the prototype of PP compiler)

Detailed design, language features, pipeline and build instructions are in `notes/project_overview.md`.

Check the general coding rules at `notes/general_coding_rule.md`

Do not commit or push unless instructed to explicitly.

If you need date, always use `date` instead of relying on your memory.

Be concise in your output.

## Clang Format
You should use `clang-format` for formatting:

Repository uses a project `.clang-format` file. Run:

```bash
clang-format -i src/*.c include/*.h
```

Or for all C/H files:
```bash
find src include -type f \( -name "*.c" -o -name "*.h" \) -exec clang-format -i {} +
```

For example, if you remove conditional, do not attempt to fix the indentation by edit tool, but rely on `clang-format`.

## Memory Leak check
When testing using `pplang` binary, to check for memory leak, you can use `valgrind`:

```bash
valgrind --leak-check=yes ./bin/pplang pp_test_code.pp
```
