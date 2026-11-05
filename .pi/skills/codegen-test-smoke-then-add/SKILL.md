---
name: codegen-test-smoke-then-add
description: Smoke test PP code with bin/pplang before adding unit tests to tests/codegentest.cpp, and collect failure artifacts if codegen fails.
---

# CodeGen Testing: Smoke → Add

Use this skill when adding branching/control-flow or new codegen features to the PP compiler.

## Process

1. **Smoke test first**
   - Create a temporary `.pp` file under `/tmp` with the feature under test.
   - Run `bin/pplang -c -v /tmp/<name>.pp -o /tmp/<name>.o`.
   - Verify:
     - No lexer errors
     - No parser errors
     - No semantic errors
     - Code generation succeeds and prints LLVM IR
   - Capture and inspect the IR output for verification and correctness.

   Optional deep inspection:
   - Use `gdb` to pause after codegen and dump internal state.
   - Call `pp_global_type_table_print(sa->gtt)` for type table.
   - Dump symbol / environment info via `pp_environment_backend_print_type_map` and `pp_environment_backend_print_value_map`.
   - **Caution**: Do not run code that can hang, e.g. infinite loops in test programs, during gdb inspection.

2. **Decide**
   - If pplang succeeds with no errors → proceed to add a unit test.
   - If pplang fails at lexer/parser/semantic → fix the language frontend first.
   - If pplang succeeds in frontend but fails in codegen → collect failure artifacts.

## Adding a passing test

When smoke test passes:
- Add a test to `tests/codegentest.cpp` using existing helpers:
  - `run_codegen(source, lexer_errors, parser_errors, semantic_errors, codegen_result)`
  - `assert_ir_contains(cg, needle)`
  - `assert_ir_not_contains(cg, needle)`
- Verify `LLVMVerifyModule` passes.
- Check for expected LLVM constructs, e.g.:
  - if: `if.then`, `if.else`, `if.end`
  - while: `while.cond`, `while.body`, `while.end`
  - for: `for.cond`, `for.body`, `for.step`, `for.end`
- Build and run:
  ```bash
  make pplang unittest
  bin/unittest --gtest_filter=CodeGenTest.*
  ```

### Optional emission verification

After successful codegen, verify the emission API works:

```c
int ok = pp_code_generator_emit_object_file(cg->mod, "/tmp/pp_test.o");
```

The API is ported from `cz/src/cz_code_generator.c` and emits an LLVM object file via `LLVMTargetMachineEmitToFile`. Test emission for each new CodeGenTest case to ensure the module is well-formed. Emission succeeds for all current tests, even when unreachable blocks are present. Emission failures will be reported via stderr from `LLVMTargetMachineEmitToFile`.

## Failure handling – codegen fault

If codegen fails:
1. Create `codegen_failure.md` with:
   - Source code
   - Full LLVM IR output if generated
   - Verification error message from `LLVMVerifyModule`
   - Type table dump via `pp_global_type_table_print`
   - Symbol table / environment info if relevant, obtained via the print APIs
   - Optional gdb backtrace of codegen failure point
2. Do **not** add a unit test for the failing case.
3. Document workaround notes, e.g. for-loop init with `let` inside parentheses currently parser errors – use pre-declared variable workaround.

## Notes
- For-loop init with `let` inside parentheses is a known parser limitation. Use pre-declared variable workaround for tests.
- Always smoke test before committing tests.
- Notebook page `control-flow-codegen-tests` is the cache for this workflow.
- Avoid hanging the process with infinite loops when inspecting with gdb; use small bounded test cases.
- Emission API is ported from `../cz/src/cz_code_generator.c`. In CZ, semantic analysis enforces `cz_ast_node_returns_on_all_paths` for non-void functions, ensuring all control paths return. PP currently has this as TODO in `pp_semantic_analyzer.c`. Codegen in both projects still emits `unreachable` for non-terminated blocks as a fallback; emission itself does not require the semantic guard.
