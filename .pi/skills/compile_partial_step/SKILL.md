---
name: compile-partial-step
 description: Deploy a temporary testing harness for lexer → parser → semantic analyzer → codegen by creating a small driver in /tmp/ and linking against PP libraries. Use when STOP_AT_* builds are no longer applicable.
---

# Compile Partial Step – Temporary Harness

The `STOP_AT_*` compile-time flags are deprecated. Use a lightweight manual harness instead.

## When to use
You need to run only a subset of the pipeline for debugging or unit testing without rebuilding `pplang`.

## Setup
Create a temporary driver in `/tmp/`:

```bash
mkdir -p /tmp/pp_tmp_harness
cat > /tmp/pp_tmp_harness/harness.c <<'EOF'
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"
#include "pp_code_generator.h"
#include "pp_string_pool.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const char* src = argv[1];
    PP_StringPool* sp = pp_string_pool_create();
    PP_Lexer* lx = pp_lexer_create(src, "<input>", sp);
    pp_lexer_analyze(lx);
    PP_Parser* p = pp_parser_create(lx, sp);
    pp_parser_parse(p);
    PP_SemanticAnalyzer* sa = pp_semantic_analyzer_create(p, sp);
    pp_semantic_analyzer_analyze(sa);
    // Optionally codegen
    // PP_CodeGenerator* cg = pp_code_generator_create(sa, sp);
    // pp_code_generator_generate(cg);
    // pp_code_generator_free(cg);
    pp_semantic_analyzer_free(sa);
    pp_parser_free(p);
    pp_lexer_free(lx);
    pp_string_pool_free(sp);
    return 0;
}
EOF
```

## Build
```bash
cd /tmp/pp_tmp_harness
gcc -g -I/home/pbjk/Documents/Lab/pp/include harness.c \
  /home/pbjk/Documents/Lab/pp/obj/*.o -lLLVM-23 -o harness
```
Only link objects you need up to the stage you want.

## Usage
```bash
/tmp/pp_tmp_harness/harness "your source code string or file contents"
```

For file input, read file into a string first or modify harness to use `read_file_to_string`.

### Verbose printing
The PP pipeline has built-in verbose dumps similar to `pplang -v`. In a harness you can enable them manually:

* Lexer errors are printed by `pp_lexer_analyze`
* Parser AST dump: `pp_ast_root_print(parser->program, 0);`
* Semantic verbose output is controlled via `CompilerPipelineOptions.verbose`. In a manual harness you can call the debug prints directly, e.g.:
```c
if (parser->error_list && parser->error_list->n_errors == 0) {
    printf("Parse succeeded. AST:\n");
    pp_ast_root_print(parser->program, 0);
}
```
* See `src/pplang.c` and `src/pp_compiler_pipeline.c` for the verbose flags used in the real driver.

## Cleanup
```bash
rm -rf /tmp/pp_tmp_harness
```

## Notes
* This avoids rebuilding `pplang` and lets you test lexer/parser/semantic/codegen in isolation.
* Always free in reverse order: codegen → semantic → parser → lexer → string pool.
* Do not commit temporary harnesses. Use only for ad-hoc debugging.

