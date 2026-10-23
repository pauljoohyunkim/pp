#ifndef PP_CODE_GENERATOR_H
#define PP_CODE_GENERATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>

#include "pp_ast.h"
#include "pp_error.h"
#include "pp_semantic_analyzer.h"
#include "pp_symbol.h"
#include "pp_symbol_table.h"
#include "pp_type.h"
#include "pp_type_table.h"

typedef struct PP_Environment_Backend PP_Environment_Backend;

typedef struct {
    const PPSymbol* symbol;
    LLVMValueRef ref;
} PP_Symbol_To_LLVMValueRef;

typedef struct {
    const PPType* type;
    LLVMTypeRef ref;
} PP_Type_To_LLVMTypeRef;

/**
 * @brief Backend environment for mapping semantic entities to LLVM IR values/types.
 */
struct PP_Environment_Backend {
    PP_Environment_Backend* parent;
    PP_Symbol_To_LLVMValueRef* value_map;
    size_t value_count;
    PP_Type_To_LLVMTypeRef* type_map;
    size_t type_count;
};

typedef struct {
    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef builder;

    PP_AST_Node* program;
    PPSymbolTable* gst;
    PP_Environment_Backend* global_env_b;
    PP_GlobalTypeTable* gtt;
    PP_StringPool* sp;

    const PPType* current_function_type;

    const char* filename;
    const char* code;
    size_t code_length;

    PP_ErrorList* error_list;
} PP_CodeGenerator;

/**
 * @brief Create a new PP_Environment_Backend.
 * @return Pointer to allocated backend or NULL on failure.
 */
PP_Environment_Backend* pp_environment_backend_create(void);

/**
 * @brief Free a PP_Environment_Backend.
 * @param env_b Backend to free.
 */
void pp_environment_backend_free(PP_Environment_Backend* env_b);

/**
 * @brief Print the type mapping in the backend environment for debugging.
 * @param env_b Backend to print.
 */
void pp_environment_backend_print_type_map(PP_Environment_Backend* env_b);

/**
 * @brief Print the value mapping in the backend environment for debugging.
 * @param env_b Backend to print.
 */
void pp_environment_backend_print_value_map(PP_Environment_Backend* env_b);

/**
 * @brief Create a PP_CodeGenerator from a semantic analyzer.
 * @param sa Semantic analyzer whose ownership of program/gtt/etc. is transferred.
 * @return New code generator or NULL on failure.
 */
PP_CodeGenerator* pp_code_generator_create(PP_SemanticAnalyzer* sa);

/**
 * @brief Free a PP_CodeGenerator and all owned resources.
 * @param cg Code generator to free.
 */
void pp_code_generator_free(PP_CodeGenerator* cg);

/**
 * @brief Generate LLVM IR from the code generator.
 * @param cg Code generator.
 * @return 1 on success, 0 on failure.
 */
int pp_code_generator_generate(PP_CodeGenerator* cg);

/**
 * @brief Emit LLVM module to an object file.
 * @param module LLVM module.
 * @param output_filename Output file name.
 * @return 1 on success, 0 otherwise.
 */
int pp_code_generator_emit_object_file(LLVMModuleRef module, const char* output_filename);

#ifdef __cplusplus
}
#endif

#endif /* PP_CODE_GENERATOR_H */
