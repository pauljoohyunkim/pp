# PP Compiler Debug Info -g Option

## Overview

` -g` is a compiler flag for `pplang` which enables DWARF debug metadata emission via LLVM's DIBuilder C API. Debug info maps generated LLVM IR back to PP source: filenames, functions, variables, types, and per-instruction line/column. This enables gdb/lldb to set breakpoints by line, step through source, and inspect variables.

The Makefile already builds `pplang` itself with `-g -Wall -O0`. That is debug symbols for the compiler binary. The `-g` described here is for the output of `pplang` when compiling PP source to object/assembly/LLVM IR.

## Current State

* Build system: Makefile
  * `CFLAGS=-g -Wall -O0 -I$(INCLUDE) -MMD -MP $(shell llvm-config --cflags) $(CFLAGS_EXTRA)`
  * `CXXFLAGS=$(CFLAGS)`
* `pplang` CLI options currently: `-h -v -c -S --emit-llvm -o <file> -O <0123>`
* No `-g` flag for PP source compilation
* `PP_CodeGenerator` has no DIBuilder, DISubprogram, DILocation, or DIType mapping
* `PP_AST_Node` already carries `unsigned int line; unsigned int col;`
* `PP_CodeGenerator` stores `filename`, `code`, `code_length`
* LLVM version: 23.1.4

## Design Goals

* `-g` toggles all debug info generation. Without it, zero cost.
* `-g` with `-O0` gives best experience. With optimization, LLVM handles `dbg.value` as long as declares exist.
* Keep debug info accurate for PP ownership semantics and generated helpers.

## Implementation Plan

### 1. CLI and flag plumbing

* `src/pplang.c`
  * Add `g` to `getopt_long` spec: `"hcSvo:O:g"`
  * Parse `-g` into `bool debug`
  * Pass `debug` through `compile_pp_source_core -> pp_code_generator_create -> pp_code_generator_generate`
* `include/pp_code_generator.h`
  * Add `bool debug` to `PP_CodeGenerator`
  * Add `LLVMDIBuilderRef di_builder`
  * Add `DICompileUnitRef cu`, `DIFileRef file`, `DIScopeRef current_scope`

### 2. Module setup, once per compilation unit

* In `pp_code_generator_create` when `debug`:
  * `LLVMDIBuilderCreate` for module
  * Module flags:
    * `LLVMAddModuleFlag(mod, LLVMModuleFlagBehaviorWarning, "Dwarf Version", 4)`
    * `LLVMAddModuleFlag(mod, LLVMModuleFlagBehaviorWarning, "Debug Info Version", 3)`
  * `LLVMDIBuilderCreateFile(filename, ...)` → file scope
  * `LLVMDIBuilderCreateCompileUnit(LLVMDWARFSourceLanguageC99, file, producer="pplang", isOptimized=false, ...)` → CU
* In `pp_code_generator_free` when `debug`:
  * `LLVMDIBuilderFinalize`
  * Dispose DIBuilder

### 3. Functions

* `pp_code_generator_emit_function`
  * Get function AST node line/col
  * Build subroutine type via DIBuilder from PP function type
  * `LLVMDIBuilderCreateFunction(cu, name, linkageName, file, line, subrType, 0, 0, 0, 0, 1, 0, 0)` → DISubprogram
  * `LLVMSetSubprogram`
  * Store DISubprogram in env for nested scope tracking
* Lambdas/operators are functions. Use mangled name as linkage name, source name as display name.
* Extern declarations need no subprogram.

### 4. Source locations on instructions

* Helper:
  ```c
  static void pp_code_generator_set_location(PP_CodeGenerator* cg, const PP_AST_Node* node)
  ```
  * Build `DILocation` from `current_scope`, `node->line`, `node->col`
  * `LLVMSetCurrentDebugLocation2`
* Call at entry of every statement/expression generation.
* Clear location for compiler-generated code: `pp.copy.T`, `pp.drop.T`, synthetic temporaries, drops on early return.

### 5. Variables and parameters

* Extend `pp_code_generator_entry_alloca` which is single alloca point.
* When `debug`:
  * Create `DILocalVariable` / `DIParameterVariable` via DIBuilder using DIType from type map
  * Emit `LLVMDIBuilderInsertDeclareAtEnd` for the alloca
* Lexical blocks:
  * On `pp_code_generator_generate_block_statement`, create `LLVMDIBuilderCreateLexicalBlock` and update `current_scope`
  * Restore scope on exit

### 6. Types

* Add PPType → DIType cache alongside existing PPType → LLVMType cache
* Primitives: `LLVMDIBuilderCreateBasicType(name, sizeInBits, encoding)`
* Const: `LLVMDIBuilderCreateQualifiedType(DW_TAG_const_type, baseDIType)`
* Reference: `LLVMDIBuilderCreateReferenceType`
* Pointer: `LLVMDIBuilderCreatePointerType(baseDIType, size)`
* Newtype: `LLVMDIBuilderCreateTypedef`
* Struct: `LLVMDIBuilderCreateStructType` with forward declaration for recursive structs, then fill members with offsets from `LLVMOffsetOfElement`
* Array: `LLVMDIBuilderCreateArrayType`
* List / String: describe as struct `pp.list` / `pp.string` with data/len/cap fields

### 7. Optimization interplay

* `-g` toggles debug info. No debug info if flag off.
* `-g -O0` best experience.
* With `-O1+`, LLVM will generate `dbg.value` for mem2reg. Declares must exist.

## Gotchas

* Verifier requires instruction location scope == function's subprogram. Mixing scopes fails verification.
* Cleanup code on early returns needs sensible location or stepping jumps oddly.
* Generated helper functions should have no subprogram or be marked artificial to avoid stepping into them.
* Keep `LLVMVerifyModule` on during development.
* Keep filename per compilation unit. Prepended `.ppi` files carry own filename, just work.

## Suggested Milestones

1. Compile unit + function subprograms + line locations only. Breakpoints by line, step.
2. Parameters and locals with primitive types, `print x` works.
3. Structs, arrays, pointers, references, newtypes.
4. Lexical blocks, lists/strings polish, hide helpers, cleanup locations.

## Test Plan

* Compile small program with `-g`
* `gdb ./a.out`
  * `break main`
  * `next`
  * `info locals`
  * `ptype <var>`
* Verify DWARF sections present: `readelf --debug-dump=info a.out`

## Files to Touch

* `src/pplang.c` - CLI flag parsing
* `include/pp_code_generator.h` - struct changes
* `src/pp_code_generator.c` - DIBuilder lifecycle, location, subprograms, declares, type mapping
* `notes/debug-g-option.md` - this file

## Open Questions

* Which DWARF source language to advertise? `LLVMDWARFSourceLanguageC99` as placeholder. May want custom `DW_LANG_Pascal`? GDB has no PP language.
* Should `-g` also pass `-g` to clang/ld when linking PP output? Probably not; PP compiler emits object, user links with their own flags.
* Separate `make debug` / `make release` targets for compiler itself? Not required for this feature.

## References

* LLVM C API DebugInfo: `llvm-c/DebugInfo.h`
* LLVM DIBuilder docs
* Current code generator entry points: `pp_code_generator_entry_alloca`, `pp_code_generator_emit_function`, `pp_code_generator_generate_statement`
