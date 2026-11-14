# PP External Declarations & Module Interface Plan

## Current situation
* Demos in `demo/` manually repeat `extern func pp_std_print_*` declarations in every `main.pp`.
* Central declaration source exists: `lib/pp_std/extern/pp_io.ppi`
  * Valid PP syntax, contains `extern func` declarations for stdlib print API.
  * Duplicate entry present: `pp_std_print_uint64` appears twice.
* Compiler driver `src/pplang.c`:
  * `detect_filetype` only recognises `.pp` as source.
  * `.ppi` is not recognised, therefore cannot be compiled directly.
* No import / module syntax implemented.
* No preprocessor / textual include implemented.
* Parser supports `extern func`, `extern lambda`, `extern operator`.
* Semantic analyzer builds global symbol table from extern declarations in the current compilation unit.

## Design context
`notes/v2-todo.md` module interface decision:
* Preprocessor option: unified source buffer + source location mapping before lexer. Pros: familiar C-style includes. Cons: no type checking across files, requires line map for diagnostics, duplicates C header problems.
* Interface file option: compiler emits `.ppi` with exported declarations, imported via semantic analysis, with mangling as link-time backstop. Pros: compile-time type/purity checking, good diagnostics, aligns with PP safety goals. Cons: moderate implementation cost.
* Current leaning: prefer interface file approach for PP-to-PP, keep `extern "C"` unmangled escape hatch for foreign code. Textual `#include` deferred unless needed for non-code assets.

## Options considered

### 1. Driver accepts `.ppi` files and prepends tokens
* `detect_filetype` recognises `.ppi` as declaration file.
* Each `.ppi` is lexed separately, tokens prepended to every `.pp` unit before parsing.
* Tokens keep own filename, line, column → diagnostics stay correct.
* Usage: `pplang main.pp --ppi lib/pp_std/extern/pp_io.ppi ...` or `-I dir` with file name.
* Smallest change, no new language syntax. Flat header model.

### 2. Automatic prelude for standard library
* Driver finds stdlib `extern/*.ppi` by default via `--stdlib-path` or install location and prepends them to every unit.
* `--nostdlib` disables prelude and drops `pp_std.a` from link.
* Removes repeated `extern` lines from demos and explicit `pp_std.a` argument.

### 3. Token-level `include "file.ppi";` directive
* Lexer/driver splices included file's tokens at position.
* Needs visited set, include-once behavior, rule that included file may only contain declarations.
* Gives per-file control without a module system, still C-style.

### 4. `import` with semantic loading of interfaces
* Proper module route with type and purity checking, good diagnostics.
* Moderate cost. Keep as v2 item already noted.

### 5. Compiler-generated `.ppi` `--emit-ppi`
* Automatic production of option 1 inputs for PP-to-PP code.
* Should come after merge rule.

## Rules needed for any of the above
* **Identical redeclaration allowed.** Once prelude is automatic, existing code that still declares `extern func pp_std_print_string ...` would collide. Merge identical `extern` declarations, error only on signature or kind mismatch `func` vs `lambda`.
* `pp_io.ppi` already contains duplicate `pp_std_print_uint64`. Merge rule makes it harmless; delete duplicate anyway.
* **Extern followed by definition allowed** if signatures match. Gives free signature check for defining file.
* **Declarations only in `.ppi` files.** Reject function bodies and variable definitions there, otherwise every object file defines same symbol and linking fails.
* **Unused externs free.** LLVM declarations never referenced produce no undefined symbols, so large prelude costs only parse time.
* **Diagnostics:** errors inside `.ppi` should name that file; verbose output should list which `.ppi` files were prepended.

## Suggested order
1. `.ppi` recognition and token prepending, merge rule, duplicate cleanup.
2. Stdlib prelude + `--nostdlib`.
3. Update demos to drop manual `extern` lines.
4. Later: `import` semantic module system, `--emit-ppi`.

## Open questions
* Module search path resolution: how to map `import pp_std` → `lib/pp_std/extern/pp_io.ppi`?
* Should `.ppi` files be compiled to a cached binary symbol table for speed?
* Cycle detection policy for module imports vs token prepending.
* Interaction with `--emit-ppi` and forward declarations.
* Debug info `-g` mapping for prepended tokens.

---
Recorded for extern/module management planning.
