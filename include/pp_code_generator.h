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
    PPSymbolTable* global_symbol_table;
    PP_Environment_Backend* global_env_b;
    PP_GlobalTypeTable* gtt;
    PP_StringPool* sp;

    const PPType* current_function_type;

    const char* filename;
    const char* code;
    size_t code_length;

    PP_ErrorList* error_list;
} PP_CodeGenerator;

PP_Environment_Backend* pp_environment_backend_create(void);
void pp_environment_backend_free(PP_Environment_Backend* env_b);

PP_CodeGenerator* pp_code_generator_create(PP_SemanticAnalyzer* sa);
void pp_code_generator_free(PP_CodeGenerator* cg);

#ifdef __cplusplus
}
#endif

#endif /* PP_CODE_GENERATOR_H */
