# -g Debug Info Support Checklist

## Overview
Track implementation of `-g` DWARF debug info emission for `pplang` via LLVM DIBuilder.

## CLI & Plumbing
- [x] Add `'g'` to getopt_long spec in `src/pplang.c`
- [x] Parse `-g` into `bool debug`
- [x] Add `bool debug` to `CompilerPipelineOptions` in `include/pp_compiler_pipeline.h`
- [x] Propagate `debug` from `main` through `pp_compiler_pipeline_compile_source_opts` / `..._to_buffer_opts`
- [x] Update `print_help()` to document `-g`

## Code Generator Struct
- [x] Extend `PP_CodeGenerator` in `include/pp_code_generator.h` with:
  - `bool debug`
  - `LLVMDIBuilderRef di_builder`
  - `LLVMMetadataRef di_compile_unit`
  - `LLVMMetadataRef di_file`
  - `LLVMMetadataRef di_current_scope`
- [x] Update `pp_code_generator_create` signature to accept `bool debug` and store it → `PP_CodeGenerator* pp_code_generator_create(PP_SemanticAnalyzer* sa, PP_StringPool* sp, bool debug)`
- [x] Update all call sites: `pp_compiler_pipeline_compile_source_core_opts` and legacy core

## DIBuilder Lifecycle
- [x] Create DIBuilder in `pp_code_generator_create` when `debug`
- [x] Set module flags: Dwarf Version 5, Debug Info Version LLVMDebugMetadataVersion()
- [x] Create DIFile from source filename (fallback "input")
- [x] Create DICompileUnit
- [x] Finalize DIBuilder and cleanup in `pp_code_generator_free`

## Functions & Scopes
- [x] Emit DISubprogram in `pp_code_generator_emit_function`
- [x] Set subprogram on LLVM function via `LLVMSetSubprogram`
- [x] Track current DIScope per function
- [x] Create lexical blocks on `pp_code_generator_generate_block_statement`
- [x] Restore scope on block exit

## Source Locations
- [x] Implement `pp_code_generator_debug_set_location(cg, loc)`
- [x] Call at entry of statement generation
- [ ] Call at entry of expression generation
- [ ] Clear location for synthetic helpers: `pp.copy.T`, `pp.drop.T`, temporaries
- [ ] Ensure verifier: instruction scope == function's subprogram

## Variables & Parameters
- [x] Emit `DIParameterVariable` for params in `pp_code_generator_generate_function_body`
- [x] Emit `DIAutoVariable` for `let` declarations in `pp_code_generator_generate_variable_declaration_statement`
- [x] Emit `LLVMDIBuilderInsertDeclareRecordAtEnd` for those allocas when `debug`
- [x] Map PP types to DI types for variables (primitives)

## Type Mapping
- [ ] Add PPType → DIType cache
- [x] Primitives: `LLVMDIBuilderCreateBasicType` (with `DW_ATE` encodings)
- [x] Real `DISubroutineType` from signature (return type + param types)
- [ ] Const: `CreateQualifiedType(DW_TAG_const_type, ...)`
- [x] Reference / Pointer: `CreateReferenceType` / `CreatePointerType`
- [x] Newtype: unwrapped to underlying in `pp_code_generator_get_di_type`
- [x] Struct: `CreateStructType` with members derived recursively
- [x] Array / List / String mapping

## Optimization Interplay
- [x] Ensure debug info is emitted only when `-g` is set
- [ ] Test with `-g -O0` and `-g -O1+`

## Testing
- [x] Compile small program with `-g -c`
- [x] Verify DWARF sections: `readelf --debug-dump=info a.o`
- [x] `gdb ./a.out` → `break main`, `next`, `info locals`, `ptype`
- [x] Verify breakpoints by line work
- [x] Verify no debug info emitted without `-g`

## Gotchas
- [ ] Hide generated helpers or mark artificial to avoid stepping
- [ ] Handle early-return cleanup locations sensibly
- [ ] Keep `LLVMVerifyModule` on during development
- [ ] Per compilation unit filename handling for PPI files

## Milestones
- [x] Milestone 1: CU + module flags + DIFile + DICompileUnit scaffold
- [x] Milestone 1 cont.: function subprograms + line locations
- [x] Milestone 2: Parameters and locals with primitive types
- [x] Milestone 3: Structs, arrays, pointers, references, newtypes
- [x] Milestone 4: Lexical blocks, lists/strings polish
- [ ] Milestone 5: Expression-level locations, helper marking artificial

## Files Touched
- `src/pplang.c`
- `include/pp_compiler_pipeline.h`
- `include/pp_code_generator.h`
- `src/pp_code_generator.c`
- `CHECKLIST_G_OPTION.md`
