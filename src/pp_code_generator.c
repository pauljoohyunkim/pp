#include "pp_code_generator.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

PP_Environment_Backend* pp_environment_backend_create(void) {
    PP_Environment_Backend* env_b = (PP_Environment_Backend*)calloc(1, sizeof(PP_Environment_Backend));
    return env_b;
}

void pp_environment_backend_free(PP_Environment_Backend* env_b) {
    if (env_b != NULL) {
        free(env_b->value_map);
        free(env_b->type_map);
    }
    free(env_b);
}

static int pp_environment_backend_push_val_map(PP_Environment_Backend* env_b, const PPSymbol* symbol,
                                               LLVMValueRef llvmval) {
    PP_Symbol_To_LLVMValueRef* new_map = NULL;
    if (env_b == NULL || symbol == NULL) return 0;
    new_map = (PP_Symbol_To_LLVMValueRef*)realloc(env_b->value_map,
                                                  sizeof(PP_Symbol_To_LLVMValueRef) * (env_b->value_count + 1));
    if (new_map == NULL) return 0;
    env_b->value_map = new_map;
    env_b->value_map[env_b->value_count].symbol = symbol;
    env_b->value_map[env_b->value_count].ref = llvmval;
    env_b->value_count++;
    return 1;
}

static int pp_environment_backend_push_type_map(PP_Environment_Backend* env_b, const PPType* type,
                                                LLVMTypeRef llvmtype) {
    PP_Type_To_LLVMTypeRef* new_map = NULL;
    if (env_b == NULL || type == NULL) return 0;
    const PPType* t = type;
    while (t && t->kind == PP_TK_Const) { t = t->type.pp_type_const.const_of; }
    new_map =
        (PP_Type_To_LLVMTypeRef*)realloc(env_b->type_map, sizeof(PP_Type_To_LLVMTypeRef) * (env_b->type_count + 1));
    if (new_map == NULL) return 0;
    env_b->type_map = new_map;
    env_b->type_map[env_b->type_count].type = t;
    env_b->type_map[env_b->type_count].ref = llvmtype;
    env_b->type_count++;
    return 1;
}

static const LLVMValueRef pp_environment_backend_lookup_val(const PP_Environment_Backend* env_b, const PPSymbol* symbol,
                                                            bool cascade) {
    if (env_b == NULL || symbol == NULL) return NULL;
    for (size_t i = 0; i < env_b->value_count; ++i) {
        if (env_b->value_map[i].symbol == symbol) { return env_b->value_map[i].ref; }
    }
    if (env_b->parent != NULL && cascade) { return pp_environment_backend_lookup_val(env_b->parent, symbol, cascade); }
    return NULL;
}

static LLVMTypeRef pp_backend_lower_type(PP_CodeGenerator* cg, const PPType* type) {
    NULL_POINTER_ERROR_HANDLE(cg);
    NULL_POINTER_ERROR_HANDLE(type);

    switch (type->kind) {
        case PP_TK_Void:
            return LLVMVoidTypeInContext(cg->ctx);
        case PP_TK_Bool:
            return LLVMInt1TypeInContext(cg->ctx);
        case PP_TK_Int32:
            return LLVMInt32TypeInContext(cg->ctx);
        case PP_TK_UInt32:
            return LLVMInt32TypeInContext(cg->ctx);
        case PP_TK_Float:
            return LLVMFloatTypeInContext(cg->ctx);

        case PP_TK_Newtype:
            return pp_backend_lower_type(cg, type->type.pp_type_newtype.underlying);

        case PP_TK_Const:
            return pp_backend_lower_type(cg, type->type.pp_type_const.const_of);

        case PP_TK_Reference:
            return LLVMPointerTypeInContext(cg->ctx, 0);

        case PP_TK_Struct:
            /* Create opaque named struct. Actual body will be filled in a second pass. */
            {
                const char* name = type->type.pp_type_struct.name;
                if (name) { return LLVMStructCreateNamed(cg->ctx, name); }
            }
            break;

        case PP_TK_Array:
        case PP_TK_List:
        case PP_TK_Function:
        case PP_TK_Lambda:
            /* Stubbed for now – full lowering not implemented yet */
            assert(0 && "TODO: pp_backend_lower_type does not yet support lowering, array, list, function, or lambda");
            break;

        default:
            break;
    }

    return NULL;
error_cleanup:
    return NULL;
}

PP_CodeGenerator* pp_code_generator_create(PP_SemanticAnalyzer* sa) {
    PP_CodeGenerator* cg = NULL;
    LLVMContextRef ctx = NULL;
    LLVMModuleRef mod = NULL;
    LLVMBuilderRef builder = NULL;
    PP_Environment_Backend* env_b = NULL;
    PP_ErrorList* error_list = NULL;

    if (sa == NULL) goto error_cleanup;

    cg = (PP_CodeGenerator*)calloc(1, sizeof(PP_CodeGenerator));
    if (cg == NULL) goto error_cleanup;

    ctx = LLVMContextCreate();
    if (ctx == NULL) goto error_cleanup;
    cg->ctx = ctx;
    ctx = NULL;

    mod = LLVMModuleCreateWithNameInContext(sa->filename ? sa->filename : "input", cg->ctx);
    if (mod == NULL) goto error_cleanup;
    cg->mod = mod;
    mod = NULL;

    builder = LLVMCreateBuilderInContext(cg->ctx);
    if (builder == NULL) goto error_cleanup;
    cg->builder = builder;
    builder = NULL;

    env_b = pp_environment_backend_create();
    if (env_b == NULL) goto error_cleanup;
    cg->global_env_b = env_b;
    env_b = NULL;

    error_list = pp_error_list_create();
    if (error_list == NULL) goto error_cleanup;
    cg->error_list = error_list;
    error_list = NULL;

    cg->filename = sa->filename;
    cg->code_length = sa->code_length;

    cg->program = sa->program;
    sa->program = NULL;

    cg->global_symbol_table = sa->gst;
    sa->gst = NULL;

    cg->gtt = sa->gtt;
    sa->gtt = NULL;

    cg->code = sa->code;
    sa->code = NULL;

    cg->sp = sa->sp;
    sa->sp = NULL;

    cg->current_function_type = sa->current_function_type;

    return cg;

error_cleanup:
    LLVMContextDispose(ctx);
    LLVMDisposeModule(mod);
    LLVMDisposeBuilder(builder);
    pp_environment_backend_free(env_b);
    pp_error_list_free(error_list);
    pp_code_generator_free(cg);
    return NULL;
}

void pp_code_generator_free(PP_CodeGenerator* cg) {
    if (cg != NULL) {
        LLVMDisposeBuilder(cg->builder);
        LLVMDisposeModule(cg->mod);
        LLVMContextDispose(cg->ctx);
        pp_ast_root_free(cg->program);
        pp_environment_backend_free(cg->global_env_b);
        pp_symbol_table_free(cg->global_symbol_table);
        pp_error_list_free(cg->error_list);
        pp_global_type_table_free(cg->gtt);
        free((char*)cg->code);
        pp_string_pool_free(cg->sp);
    }
    free(cg);
    LLVMShutdown();
}

int pp_code_generator_generate(PP_CodeGenerator* cg) {
    /* Stub: full code generation not yet implemented */
    (void)cg;
    return 1;
}

int pp_code_generator_emit_object_file(LLVMModuleRef module, const char* output_filename) {
    /* Stub: object emission not yet implemented */
    (void)module;
    (void)output_filename;
    return 0;
}
