#include "pp_code_generator.h"
static int pp_code_generator_emit_function(PP_CodeGenerator* cg, PPSymbolTable* env, PP_Environment_Backend* env_b,
                                           const PP_AST_Node* stmt);
static int pp_code_generator_emit_global_variable(PP_CodeGenerator* cg, PPSymbolTable* env,
                                                  PP_Environment_Backend* env_b, const PP_AST_Node* stmt);

#include <assert.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
    new_map =
        (PP_Type_To_LLVMTypeRef*)realloc(env_b->type_map, sizeof(PP_Type_To_LLVMTypeRef) * (env_b->type_count + 1));
    if (new_map == NULL) return 0;
    env_b->type_map = new_map;
    env_b->type_map[env_b->type_count].type = type;
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

static const LLVMTypeRef pp_environment_backend_lookup_type(const PP_CodeGenerator* cg, const PPType* type) {
    if (cg == NULL || type == NULL) return NULL;

    PP_Environment_Backend* env_b = cg->global_env_b;
    if (env_b == NULL) return NULL;

    for (size_t i = 0; i < env_b->type_count; ++i) {
        if (env_b->type_map[i].type == type) { return env_b->type_map[i].ref; }
    }

    return NULL;
}

static void pp_debug_print_type_inner(const PPType* t) {
    if (t == NULL) return;
    switch (t->kind) {
        case PP_TK_Void:
            printf("Void");
            break;
        case PP_TK_Int32:
            printf("Int32");
            break;
        case PP_TK_UInt32:
            printf("UInt32");
            break;
        case PP_TK_Float:
            printf("Float");
            break;
        case PP_TK_Bool:
            printf("Bool");
            break;
        case PP_TK_Struct:
            if (t->type.pp_type_struct.name) {
                printf("Struct: %s", t->type.pp_type_struct.name);
            } else {
                printf("Struct");
            }
            break;
        case PP_TK_Reference:
            printf("Reference(");
            pp_debug_print_type_inner(t->type.pp_type_reference.reference_to);
            printf(")");
            break;
        case PP_TK_Const:
            printf("Const(");
            pp_debug_print_type_inner(t->type.pp_type_const.const_of);
            printf(")");
            break;
        case PP_TK_Newtype:
            printf("Newtype: %s", t->type.pp_type_newtype.name);
            break;
        case PP_TK_Array:
            printf("Array");
            break;
        case PP_TK_List:
            printf("List");
            break;
        case PP_TK_Function:
            printf("Function");
            break;
        case PP_TK_Lambda:
            printf("Lambda");
            break;
        default:
            printf("Unknown");
            break;
    }
}

void pp_environment_backend_print_type_map(PP_Environment_Backend* env_b) {
    if (env_b == NULL) return;

    printf("Backend Type Map:\n");
    for (size_t i = 0; i < env_b->type_count; i++) {
        const PPType* t = env_b->type_map[i].type;
        LLVMTypeRef ref = env_b->type_map[i].ref;
        printf("  [%p] (", (void*)t);
        pp_debug_print_type_inner(t);
        printf(") -> LLVM Type: %p\n", (void*)ref);
    }
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
                const PPType* struct_type = pp_global_type_table_find_by_name(cg->gtt, name);
                assert(struct_type);
                const LLVMTypeRef struct_backend_type = pp_environment_backend_lookup_type(cg, struct_type);
                if (struct_backend_type) {
                    return struct_backend_type;
                } else {
                    // Assertion: Name collision should not happen!
                    assert(!LLVMGetTypeByName2(cg->ctx, name));

                    return LLVMStructCreateNamed(cg->ctx, name);
                }
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

    cg->gst = sa->gst;
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
        pp_symbol_table_free(cg->gst);
        pp_error_list_free(cg->error_list);
        pp_global_type_table_free(cg->gtt);
        free((char*)cg->code);
        pp_string_pool_free(cg->sp);
    }
    free(cg);
    LLVMShutdown();
}

static int pp_code_generator_fill_type_map_primitive_opaque_struct(PP_CodeGenerator* cg) {
    assert(cg);
    assert(cg->gtt);
    assert(cg->global_env_b);

    for (size_t i = 0; i < cg->gtt->type_count; i++) {
        const PPType* type = cg->gtt->types[i];
        assert(type);

        // Skip function and lambda here.
        if (type->kind == PP_TK_Function || type->kind == PP_TK_Lambda) { continue; }

        // Look up environment backend.
        if (pp_environment_backend_lookup_type(cg, type)) continue;

        // Lower and insert
        LLVMTypeRef llvm_type = pp_backend_lower_type(cg, type);
        assert(llvm_type);

        if (!pp_environment_backend_push_type_map(cg->global_env_b, type, llvm_type)) {
            assert(0 && "Type push error");
        }
    }

    return 1;
}

static int pp_code_generator_fill_type_map_complex(PP_CodeGenerator* cg) {
    LLVMTypeRef* llvm_types = NULL;
    assert(cg);
    assert(cg->gtt);

    for (size_t i = 0; i < cg->gtt->type_count; i++) {
        const PPType* type = cg->gtt->types[i];
        assert(type);

        switch (type->kind) {
            case PP_TK_Struct: {
                LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, type);
                assert(llvm_struct_type);

                size_t field_count = type->type.pp_type_struct.layout->field_count;
                if (field_count > 0) {
                    llvm_types = (LLVMTypeRef*)calloc(field_count, sizeof(LLVMTypeRef));
                    NULL_POINTER_ERROR_HANDLE(llvm_types);

                    for (size_t j = 0; j < field_count; j++) {
                        const PPType* member_type = type->type.pp_type_struct.layout->fields[j].field_type;
                        assert(member_type);

                        LLVMTypeRef llvm_member_type = pp_environment_backend_lookup_type(cg, member_type);
                        assert(llvm_member_type);
                        llvm_types[j] = llvm_member_type;
                    }
                }

                // 0 is for "packed" or not
                LLVMStructSetBody(llvm_struct_type, llvm_types, field_count, 0);
                free(llvm_types);
                llvm_types = NULL;
            } break;
            case PP_TK_Function:
            case PP_TK_Lambda: {
                bool is_lambda = type->kind == PP_TK_Lambda;
                const PPType* return_type =
                    is_lambda ? type->type.pp_type_lambda.return_type : type->type.pp_type_function.return_type;
                assert(return_type);
                LLVMTypeRef llvm_return_type = pp_environment_backend_lookup_type(cg, return_type);

                size_t param_count =
                    is_lambda ? type->type.pp_type_lambda.param_count : type->type.pp_type_function.param_count;
                if (param_count > 0) {
                    llvm_types = (LLVMTypeRef*)calloc(param_count, sizeof(LLVMTypeRef));
                    NULL_POINTER_ERROR_HANDLE(llvm_types);

                    for (size_t j = 0; j < param_count; j++) {
                        const PPType* param_type = is_lambda ? type->type.pp_type_lambda.param_types[j]
                                                             : type->type.pp_type_function.param_types[j];
                        assert(param_type);

                        LLVMTypeRef llvm_param_type = pp_environment_backend_lookup_type(cg, param_type);
                        NULL_POINTER_ERROR_HANDLE(llvm_param_type);

                        llvm_types[j] = llvm_param_type;
                    }
                }

                LLVMTypeRef llvm_func_type = LLVMFunctionType(llvm_return_type, llvm_types, param_count, 0);
                assert(llvm_func_type);

                if (!pp_environment_backend_push_type_map(cg->global_env_b, type, llvm_func_type)) {
                    goto error_cleanup;
                }

                free(llvm_types);
                llvm_types = NULL;
            } break;
            default:
                break;
        }
    }

    return 1;

error_cleanup:
    free(llvm_types);
    return 0;
}

int pp_code_generator_generate(PP_CodeGenerator* cg) {
    assert(cg);
    assert(cg->program);
    assert(cg->program->node_type == PP_AST_ProgramNode);

    // Pass 1 to build type map. (Primitive & Opaque Struct & Reference)
    if (pp_code_generator_fill_type_map_primitive_opaque_struct(cg) != 1) { goto error_cleanup; }

    // Pass 2 to build type map. (Struct Body & Function)
    if (pp_code_generator_fill_type_map_complex(cg) != 1) { goto error_cleanup; }

    for (unsigned int i = 0; i < cg->program->program.decl_count; i++) {
        const PP_AST_Node* statement = cg->program->program.decls[i];
        NULL_POINTER_ERROR_HANDLE(statement);

        switch (statement->node_type) {
            case PP_AST_FunctionDeclarationNode:
            case PP_AST_LambdaDeclarationNode:
                pp_code_generator_emit_function(cg, cg->gst, cg->global_env_b, statement);
                break;
            case PP_AST_StructDeclarationNode:
                break;
            case PP_AST_VariableDeclarationNode:
                pp_code_generator_emit_global_variable(cg, cg->gst, cg->global_env_b, statement);
                break;
            case PP_AST_TypedefDeclarationNode:
            case PP_AST_NewtypeDeclarationNode:
                break;
            default:
                break;
        }
    }

    /*
    for (unsigned int i = 0; i < cg->program->program.decl_count; i++) {
        const PP_AST_Node* statement = cg->program->program.decls[i];
        NULL_POINTER_ERROR_HANDLE(statement);

        if (statement->node_type != PP_AST_FunctionDeclarationNode) {
            continue;
        }

        const char* func_name = statement->function_declaration.name;

        if (pp_code_generator_generate_function_body(cg, cg->global_symbol_table, cg->global_env_b, statement, false) !=
    1) { pp_error_list_push_error(cg->error_list, cg->filename, statement->line, statement->col, "Function \"%s\" has a
    problem in body generation.", func_name);
        }
    }
    */

    return 1;
error_cleanup:
    return 0;
}

int pp_code_generator_emit_object_file(LLVMModuleRef module, const char* output_filename) {
#warning "Object file generation stubbed"
    assert(module);
    assert(output_filename);
    return 0;
}

static int pp_code_generator_emit_function(PP_CodeGenerator* cg, PPSymbolTable* env, PP_Environment_Backend* env_b,
                                           const PP_AST_Node* stmt) {
    assert(cg);
    assert(env);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_FunctionDeclarationNode || stmt->node_type == PP_AST_LambdaDeclarationNode);

    // Retrieve function/lambda
    const char* func_name = stmt->function_declaration.name;
    assert(func_name);
    const PPSymbol* func_symbol = pp_symbol_table_lookup(cg->gst, func_name, false);
    assert(func_symbol);
    const PPType* func_type = func_symbol->type;
    assert(func_type);

    // Look up backend type
    LLVMTypeRef llvm_func_type = pp_environment_backend_lookup_type(cg, func_type);
    assert(llvm_func_type);

    LLVMValueRef llvm_func_val = LLVMAddFunction(cg->mod, func_name, llvm_func_type);
    NULL_POINTER_ERROR_HANDLE(llvm_func_val);

    if (!pp_environment_backend_push_val_map(env_b, func_symbol, llvm_func_val)) goto error_cleanup;

    return 1;

error_cleanup:
    return 0;
}

static int pp_code_generator_emit_global_variable(PP_CodeGenerator* cg, PPSymbolTable* env,
                                                  PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(env);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_VariableDeclarationNode);
    // Stubbed for now.
    return 1;
}
