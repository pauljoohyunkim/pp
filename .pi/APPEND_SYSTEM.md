# PP Compiler

This is a C-based successor of cz. Build and usage follow the same pattern as ../cz/ but with the pp language.
The logic up to semantic analyzer pass 1 is to be similar to ../pp-rust (which is the prototype of PP compiler)

Detailed design, language features, pipeline and build instructions are in `notes/project_overview.md`.

Check the general coding rules at `notes/general_coding_rule.md`

Do not commit or push unless instructed to explicitly.

If you need date, always use `date` instead of relying on your memory.

Be concise in your output.

## Clang Format
Whenever you are at a C/C++ repo, you should use clang-format:

Repository uses a project `.clang-format` file. Run:

```bash
clang-format -i src/*.c include/*.h
```

Or for all C/H files:
```bash
find src include -type f \( -name "*.c" -o -name "*.h" \) -exec clang-format -i {} +
```

The style is Google-based with 4-space indent, braces attached, `ColumnLimit: 120`, arguments packed, and short if statements kept on one line:
BasedOnStyle: Google, IndentWidth: 4, ContinuationIndentWidth: 4, UseTab: Never, BreakBeforeBraces: Attach, SpaceBeforeParens: ControlStatements, ColumnLimit: 120, BinPackArguments: true, BinPackParameters: true, PenaltyBreakBeforeFirstCallParameter: 10000, AllowShortIfStatementsOnASingleLine: WithoutElse, AllowShortLoopsOnASingleLine: true, AllowShortBlocksOnASingleLine: true

## Memory Leak check
When testing using `pplang` binary, to check for memory leak, you can use `valgrind`:

```bash
valgrind --leak-check=yes ./bin/pplang pp_test_code.pp
```
