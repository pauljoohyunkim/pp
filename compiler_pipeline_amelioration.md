# Compiler Pipeline Amelioration Plan

Goal: Reduce parameter explosion in `pp_compiler_pipeline_compile_source` / `..._to_buffer` and centralise shared compilation context. Enable PPI pre-lexing once per session instead of per source file.

## Current pain points
* `pp_compiler_pipeline_compile_source` has 8 parameters
* `pp_compiler_pipeline_compile_source_to_buffer` has 6 parameters
* `pp_compiler_pipeline_compile_source_core` has 6 parameters
* PPI files are re-read and re-lexed for every source file
* Driver already owns `PPLang_InputList`, `PPLang_PPIList`, `PP_StringPool`, flags

## Desired outcome
* Single `CompilerPipelineOptions` struct owns shared state
* Public API:
  * `int pp_compiler_pipeline_compile_source_opts(const CompilerPipelineOptions* opts, const char* src_path, const char* obj_path);`
  * `int pp_compiler_pipeline_compile_source_to_buffer_opts(const CompilerPipelineOptions* opts, const char* src_path, LLVMMemoryBufferRef* out_buf);`
* PPI pre-lexed once per session, tokens prepended to each source

## Step by step plan

### 0. Prep & safety
* `git status`
* Stash / commit uncommitted `demo/* /Makefile` and `lib/pp_std`
* `make clean && make`
* `make unittest` → baseline green

### 1. Define options struct
File: `include/pp_compiler_pipeline.h`
Add:
```c
typedef struct {
    PP_StringPool*       string_pool;
    PPLang_PPIList*      ppi_list;
    LLVMTargetMachineRef target_machine;
    bool                 verbose;
    char                 optimization_mode;
    bool                 to_assembly;
    bool                 emit_ir;
} CompilerPipelineOptions;
```
Add new prototypes, keep old ones for backward compatibility.

### 2. Add PPI pre-lex helper
File: `src/pp_compiler_pipeline.c`
Add:
```c
typedef struct {
    PP_Token* tokens;
    size_t    count;
} PPIPrelexed;
```
Helper: `static int pp_compiler_pipeline_prelex_ppi(const CompilerPipelineOptions* opts, PPIPrelexed* out)`
* Iterate `opts->ppi_list->paths`
* Read file, `pp_lexer_create(code, path, opts->string_pool)`, `pp_lexer_analyze`
* Collect tokens, skip trailing EOF
* Accumulate into `out->tokens`
* Error reporting same as current

Add `static void pp_compiler_pipeline_prelex_free(PPIPrelexed* pre)`

### 3. Refactor core to accept options
Create internal:
```c
static int pp_compiler_pipeline_compile_source_core_opts(const char* src_path, CompilerPipelineOptions* opts, PPIPrelexed* prelexed, PP_CodeGenerator** out_cg);
```
* Lex source with `opts->string_pool`
* Prepend `prelexed->tokens` to source lexer token stream
* Parser / semantic / codegen unchanged
* Use `opts->verbose` for diagnostics

### 4. Implement opts wrappers
Implement `pp_compiler_pipeline_compile_source_opts` and `..._to_buffer_opts`
* Prelex PPI once → `PPIPrelexed pre`
* Call core_opts
* Optimise with `opts->optimization_mode` and `opts->target_machine`
* Emit based on `opts->to_assembly` / `opts->emit_ir`
* Free prelexed

Keep old APIs as thin shims building a temporary `CompilerPipelineOptions` from old params.

### 5. Migrate driver `pplang.c`
After target machine creation:
```c
CompilerPipelineOptions opts = {
    .string_pool = string_pool,
    .ppi_list = ppi_list,
    .target_machine = input_list->target_machine,
    .verbose = verbose,
    .optimization_mode = optimization_mode,
    .to_assembly = to_assembly,
    .emit_ir = emit_ir,
};
```
Replace calls to old APIs with `*_opts` variants.

### 6. Build & test
* `make clean && make`
* `make unittest`
* No regressions

### 7. Verify PPI behaviour
* `pplang --ppi lib/pp_std/extern/pp_io.ppi demo/01_fibo/main.pp -o /tmp/test.o -c`
* Multiple `--ppi` order preserved
* Duplicate identical extern → silent merge
* Conflict → error
* Diagnostics point to `.ppi` file
* Demo Makefiles build with `--ppi`

### 8. Cleanup old API
If stable, deprecate/remove old signatures, remove duplicate PPI lexing from core.

### 9. Documentation
Update `CHECKLIST_STEP1_PPI.md` and dev notes.

## Risks / notes
* Token location fidelity: keep `loc.filename` from PPI lexer
* Shared string pool already enforced
* Memory: prelexed tokens live for session, free after last source
* No threading yet

## Acceptance criteria
* Build succeeds
* Unit tests pass
* `pplang --ppi ...` builds demos without manual externs
* No parameter explosion at call sites
* PPI lexed once per session
