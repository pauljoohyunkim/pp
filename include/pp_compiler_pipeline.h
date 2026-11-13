#ifndef PP_COMPILER_PIPELINE_H
#define PP_COMPILER_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <llvm-c/Target.h>
#include "pp_code_generator.h"
#include "pp_string_pool.h"

int pp_compiler_pipeline_compile_source(const char* src_path, const char* obj_path, bool verbose, char optimization_mode,
                                       LLVMTargetMachineRef tm, bool to_assembly, bool emit_ir, PP_StringPool* sp, const char** ppi_paths, int ppi_count);
int pp_compiler_pipeline_compile_source_to_buffer(const char* src_path, LLVMMemoryBufferRef* out_buf, bool verbose,
                                                  char optimization_mode, LLVMTargetMachineRef tm, PP_StringPool* sp, const char** ppi_paths, int ppi_count);
int pp_compiler_pipeline_compile_source_core(const char* src_path, bool verbose, PP_CodeGenerator** out_cg, PP_StringPool* sp, const char** ppi_paths, int ppi_count);

#ifdef __cplusplus
}
#endif

#endif /* PP_COMPILER_PIPELINE_H */
