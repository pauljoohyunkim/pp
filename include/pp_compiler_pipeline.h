#ifndef PP_COMPILER_PIPELINE_H
#define PP_COMPILER_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <llvm-c/Target.h>
#include "pp_code_generator.h"
#include "pp_string_pool.h"

typedef struct {
    PP_StringPool*       string_pool;
    const char**         ppi_paths;
    int                  ppi_count;
    LLVMTargetMachineRef target_machine;
    bool                 verbose;
    char                 optimization_mode;
    bool                 to_assembly;
    bool                 emit_ir;
} CompilerPipelineOptions;

/**
 * Compile a source file to an object/assembly/IR file using legacy parameter list.
 * @param src_path Path to .pp source
 * @param obj_path Output path
 * @param verbose Enable verbose diagnostics
 * @param optimization_mode Optimization level '0'..'3' or '\0'
 * @param tm LLVM target machine
 * @param to_assembly Emit assembly instead of object
 * @param emit_ir Emit LLVM IR instead of object
 * @param sp Shared string pool
 * @param ppi_paths Array of .ppi files
 * @param ppi_count Number of PPI files
 * @return 1 on success
 */
int pp_compiler_pipeline_compile_source(const char* src_path, const char* obj_path, bool verbose, char optimization_mode,
                                       LLVMTargetMachineRef tm, bool to_assembly, bool emit_ir, PP_StringPool* sp, const char** ppi_paths, int ppi_count);

/**
 * Compile a source file to an in-memory object buffer using legacy parameter list.
 * @param src_path Path to .pp source
 * @param out_buf Output memory buffer, caller must dispose
 * @param verbose Enable verbose diagnostics
 * @param optimization_mode Optimization level '0'..'3' or '\0'
 * @param tm LLVM target machine
 * @param sp Shared string pool
 * @param ppi_paths Array of .ppi files
 * @param ppi_count Number of PPI files
 * @return 1 on success
 */
int pp_compiler_pipeline_compile_source_to_buffer(const char* src_path, LLVMMemoryBufferRef* out_buf, bool verbose,
                                                  char optimization_mode, LLVMTargetMachineRef tm, PP_StringPool* sp, const char** ppi_paths, int ppi_count);

/* New options-based API */

/**
 * Compile a source file using CompilerPipelineOptions.
 * @param opts Pointer to options struct containing pool, PPI list, target machine and flags
 * @param src_path Path to .pp source
 * @param obj_path Output path
 * @return 1 on success
 */
int pp_compiler_pipeline_compile_source_opts(const CompilerPipelineOptions* opts, const char* src_path, const char* obj_path);

/**
 * Compile a source file to an in-memory object buffer using CompilerPipelineOptions.
 * @param opts Pointer to options struct
 * @param src_path Path to .pp source
 * @param out_buf Output memory buffer, caller must dispose
 * @return 1 on success
 */
int pp_compiler_pipeline_compile_source_to_buffer_opts(const CompilerPipelineOptions* opts, const char* src_path, LLVMMemoryBufferRef* out_buf);

#ifdef __cplusplus
}
#endif

#endif /* PP_COMPILER_PIPELINE_H */
