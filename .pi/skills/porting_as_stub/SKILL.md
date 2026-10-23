---
name: porting-as-stub
description: Strategically port function declarations and stub implementations to maintain buildability while iteratively migrating logic from cz to PP.
---

# Porting As Stub

Use this skill when porting large amounts of logic from the `cz` project to the `pp` project. Instead of porting complete function bodies immediately, you create "stubs" to preserve the project's buildability and establish the overall pipeline flow first.

## Workflow

1. **Identify Target**: Locate the function or set of functions in `cz/src/cz_code_generator.c` (or other sources) that need to be ported.
2. **Forward Declare**: Add the function prototype at the top of the corresponding `.c` file in `pp`. 
   - Use `static` for internal helpers.
   - Ensure the prototype matches the new `pp` types (e.g., `PP_CodeGenerator*` instead of `CZ_CodeGenerator*`).
3. **Implement Stub**: Add the function definition at the bottom of the `.c` file.
   - **Assertions**: Every input pointer should be guarded with `assert(ptr);`.
   - **Default Return**: Return a value that allows the pipeline to continue (e.g., `1` for success in `int` functions, `NULL` for `LLVMValueRef` or other pointers).
   - **Marker**: Include a comment `// Stubbed for now.` to make it easy to find later.
4. **Verify**: Run `make` to ensure the change didn't break the build.

## Key Rules

* **Build-First**: The primary goal is that the project remains compilable. Do not introduce half-finished logic that causes compiler errors.
* **Contract-Oriented**: Focus on the function signature and the intended return value.
* **Iterative Migration**: Once the skeleton of the pipeline is in place (e.g., the main loop calls the stubs in the correct order), start replacing stubs with actual ported logic one by one.

## Example Stub Template

```c
static int pp_code_generator_example_helper(PP_CodeGenerator* cg, const PP_AST_Node* node) {
    assert(cg);
    assert(node);
    // Stubbed for now.
    return 1; 
}
```
