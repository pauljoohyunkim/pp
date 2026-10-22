#include "pp_code_generator.h"

#include <assert.h>
#include <errno.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PP_Environment_Backend* pp_environment_backend_enter_scope(const PP_Environment_Backend* current_env_b);
static LLVMValueRef pp_code_generator_generate_lvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_lvalue_identifier(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_l_value_to_r_value(PP_CodeGenerator* cg, const PPType* type, LLVMValueRef lvalue);
static LLVMValueRef pp_code_generator_generate_rvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_binary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                             PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                             bool is_compile_time);
static LLVMValueRef pp_code_generator_generate_rvalue_unary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                            PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                            bool is_compile_time);
static LLVMValueRef pp_code_generator_generate_rvalue_cast(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                           bool is_compile_time);
static LLVMValueRef pp_code_generator_generate_rvalue_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                              PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                              bool is_compile_time);
static LLVMValueRef pp_code_generator_generate_rvalue_struct_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                  PP_Environment_Backend* env_b,
                                                                  const PP_AST_Node* node, bool is_compile_time);
static LLVMValueRef pp_code_generator_generate_rvalue_collection_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                      PP_Environment_Backend* env_b,
                                                                      const PP_AST_Node* node, bool is_compile_time);
static LLVMValueRef pp_code_generator_generate_rvalue_struct_member_access(PP_CodeGenerator* cg,
                                                                           PPSymbolTable* symbol_table,
                                                                           PP_Environment_Backend* env_b,
                                                                           const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_function_call(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                    PP_Environment_Backend* env_b,
                                                                    const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_identifier(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node);

static int pp_code_generator_generate_function_body(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_generate_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                PP_Environment_Backend* env_b, const PP_AST_Node* node);
static int pp_code_generator_generate_variable_declaration_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                     PP_Environment_Backend* env_b,
                                                                     const PP_AST_Node* stmt);
static int pp_code_generator_generate_assignment_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_generate_return_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                       PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_generate_if_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                   PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_generate_for_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_generate_while_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_generate_block_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_emit_function(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                           PP_Environment_Backend* env_b, const PP_AST_Node* stmt);
static int pp_code_generator_emit_global_variable(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                  PP_Environment_Backend* env_b, const PP_AST_Node* stmt);

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

static PP_Environment_Backend* pp_environment_backend_enter_scope(const PP_Environment_Backend* current_env_b) {
    PP_Environment_Backend* child_scope = NULL;
    NULL_POINTER_ERROR_HANDLE(current_env_b);

    child_scope = pp_environment_backend_create();
    NULL_POINTER_ERROR_HANDLE(child_scope);

    child_scope->parent = current_env_b;

    return child_scope;

error_cleanup:
    pp_environment_backend_free(child_scope);
    return NULL;
}

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

void pp_environment_backend_print_value_map(PP_Environment_Backend* env_b) {
    if (env_b == NULL) return;

    printf("Backend Value Map:\n");
    for (size_t i = 0; i < env_b->value_count; i++) {
        const PPSymbol* sym = env_b->value_map[i].symbol;
        LLVMValueRef ref = env_b->value_map[i].ref;

        printf("  [%p] (Symbol: %p, Type: ", (void*)sym, (void*)sym);
        if (sym && sym->type) pp_debug_print_type_inner(sym->type);
        printf(") -> LLVM Value: %p\n", (void*)ref);
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

    for (size_t i = 0; i < cg->program->program.decl_count; i++) {
        const PP_AST_Node* stmt = cg->program->program.decls[i];
        assert(stmt);

        if (stmt->node_type != PP_AST_FunctionCallNode && stmt->node_type != PP_AST_LambdaDeclarationNode) continue;

        const char* func_name = stmt->function_declaration.name;
        assert(func_name);

        if (!pp_code_generator_generate_function_body(cg, cg->gst, cg->global_env_b, stmt)) {
            assert(0 && "Function body generation failure.");
        }
    }

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

static int pp_code_generator_generate_function_body(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    PP_Environment_Backend* body_env_b = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_FunctionDeclarationNode || stmt->node_type == PP_AST_LambdaDeclarationNode);

    /* Stubbed for now.
       Implementation will follow CZ:
       1. Create entry basic block.
       2. Populate local environment with parameters (alloca + store).
       3. Generate statements in the body.
       4. Ensure terminator exists for all blocks. */

    const char* func_name = stmt->function_declaration.name;
    assert(func_name);
    const PPSymbol* func_symbol = pp_symbol_table_lookup(symbol_table, func_name, false);
    assert(func_symbol);

    assert(func_symbol->type);
    cg->current_function_type = func_symbol->type;
    bool is_lambda = func_symbol->type->kind == PP_TK_Lambda;

    // LLVM lookup
    LLVMValueRef llvm_func = pp_environment_backend_lookup_val(env_b, func_symbol, false);
    assert(llvm_func);

    // Create block
    LLVMBasicBlockRef llvm_entry_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_func, "entry");
    assert(llvm_entry_block);
    LLVMPositionBuilderAtEnd(cg->builder, llvm_entry_block);

    // Enter backend scope
    body_env_b = pp_environment_backend_enter_scope(env_b);
    NULL_POINTER_ERROR_HANDLE(body_env_b);

    // Fill environment with parameters by getting parameter's value ref and registering to backend.
    size_t param_count = is_lambda ? func_symbol->type->type.pp_type_lambda.param_count
                                   : func_symbol->type->type.pp_type_function.param_count;
    for (size_t i = 0; i < param_count; i++) {
        LLVMValueRef llvm_param_val = LLVMGetParam(llvm_func, i);
        assert(llvm_param_val);
        const char* param_name = stmt->function_declaration.params[i]->param_declaration.identifier;
        assert(param_name);
        const PPSymbol* param_symbol =
            pp_symbol_table_lookup(stmt->function_declaration.param_symbol_table, param_name, false);
        assert(param_symbol);
        const PPType* param_type = param_symbol->type;
        assert(param_type);
        LLVMTypeRef llvm_param_type = pp_environment_backend_lookup_type(cg, param_type);
        assert(llvm_param_type);

        // TODO: Deal with references
        LLVMValueRef llvm_alloca_slot = LLVMBuildAlloca(cg->builder, llvm_param_type, param_name);
        LLVMBuildStore(cg->builder, llvm_param_val, llvm_alloca_slot);

        if (!pp_environment_backend_push_val_map(body_env_b, param_symbol, llvm_alloca_slot)) { goto error_cleanup; }
    }

    size_t stmt_count = stmt->function_declaration.body->statement_list.stmt_count;
    const PP_AST_Node** stmt_list = stmt->function_declaration.body->statement_list.stmts;
    for (size_t i = 0; i < stmt_count; i++) {
        if (!pp_code_generator_generate_statement(cg, stmt->function_declaration.body->statement_list.symbol_table,
                                                  body_env_b, stmt_list[i])) {
            goto error_cleanup;
        }
    }

// Skip all the basic block (if block, while block, for block) in the function until the end.
// Then return nothing if it is a void function,
// Or write "unreachable" for nonvoid flow fall.
#warning "Semantic analysis may need to check all the exit points"
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(llvm_func); bb != NULL; bb = LLVMGetNextBasicBlock(bb)) {
        // Check if this basic block lacks a terminator instruction
        if (LLVMGetBasicBlockTerminator(bb) == NULL) {
            LLVMPositionBuilderAtEnd(cg->builder, bb);

            LLVMTypeRef ret_type = LLVMGetReturnType(LLVMGlobalGetValueType(llvm_func));

            if (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind) {
                LLVMBuildRetVoid(cg->builder);
            } else {
                // Emits LLVM unreachable for non-void control flow fallthrough
                LLVMBuildUnreachable(cg->builder);
            }
        }
    }

    pp_environment_backend_free(body_env_b);
    return 1;

error_cleanup:
    pp_environment_backend_free(body_env_b);
    return 0;
}

static int pp_code_generator_generate_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);

    switch (node->node_type) {
        case PP_AST_VariableDeclarationNode:
            if (!pp_code_generator_generate_variable_declaration_statement(cg, symbol_table, env_b, node)) return 0;
            break;
        case PP_AST_AssignmentNode:
            if (!pp_code_generator_generate_assignment_statement(cg, symbol_table, env_b, node)) return 0;
            break;
        case PP_AST_ReturnStatementNode:
            if (!pp_code_generator_generate_return_statement(cg, symbol_table, env_b, node)) return 0;
            break;
        case PP_AST_IfStatementNode:
            if (!pp_code_generator_generate_if_statement(cg, symbol_table, env_b, node)) return 0;
            break;
        case PP_AST_ForStatementNode:
            if (!pp_code_generator_generate_for_statement(cg, symbol_table, env_b, node)) return 0;
            break;
        case PP_AST_WhileStatementNode:
            if (!pp_code_generator_generate_while_statement(cg, symbol_table, env_b, node)) return 0;
            break;
        case PP_AST_StatementListNode:
            if (!pp_code_generator_generate_block_statement(cg, symbol_table, env_b, node)) return 0;
            break;
#warning "Expression must be checked"
        default:
            abort();
    }

    return 1;
}

static int pp_code_generator_generate_variable_declaration_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                     PP_Environment_Backend* env_b,
                                                                     const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    /* Stubbed for now. */
    (void)cg;
    (void)symbol_table;
    (void)env_b;
    (void)stmt;
    return 1;
}

static int pp_code_generator_generate_assignment_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    /* Stubbed for now. */
    (void)cg;
    (void)symbol_table;
    (void)env_b;
    (void)stmt;
    return 1;
}

static int pp_code_generator_generate_return_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                       PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_ReturnStatementNode);
    assert(cg->current_function_type);
    assert(cg->current_function_type->kind == PP_TK_Function || cg->current_function_type->kind == PP_TK_Lambda);

    bool is_lambda = cg->current_function_type->kind == PP_TK_Lambda;
    const PPType* current_function_return_type = is_lambda
                                                     ? cg->current_function_type->type.pp_type_lambda.return_type
                                                     : cg->current_function_type->type.pp_type_function.return_type;
    assert(current_function_return_type);

#warning "Deal with void returns"
    if (current_function_return_type->kind == PP_TK_Void) {
        if (stmt->return_statement.expr == NULL) {
            LLVMBuildRetVoid(cg->builder);
        } else {
            // Not possible
            abort();
        }
    } else if (pp_type_is_reference(current_function_return_type)) {
        LLVMValueRef llvm_return_val_ref =
            pp_code_generator_generate_lvalue(cg, symbol_table, env_b, stmt->return_statement.expr);
        LLVMBuildRet(cg->builder, llvm_return_val_ref);
    } else {
        LLVMValueRef llvm_return_val_ref =
            pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->return_statement.expr);
        LLVMBuildRet(cg->builder, llvm_return_val_ref);
    }

    return 1;
}

static int pp_code_generator_generate_if_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                   PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    /* Stubbed for now. */
    (void)cg;
    (void)symbol_table;
    (void)env_b;
    (void)stmt;
    return 1;
}

static int pp_code_generator_generate_for_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    /* Stubbed for now. */
    (void)cg;
    (void)symbol_table;
    (void)env_b;
    (void)stmt;
    return 1;
}

static int pp_code_generator_generate_while_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    /* Stubbed for now. */
    (void)cg;
    (void)symbol_table;
    (void)env_b;
    (void)stmt;
    return 1;
}

static int pp_code_generator_generate_block_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    /* Stubbed for now. */
    (void)cg;
    (void)symbol_table;
    (void)env_b;
    (void)stmt;
    return 1;
}

static int pp_code_generator_emit_function(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                           PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
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

    /* Generate function body (stubbed) */
    if (!pp_code_generator_generate_function_body(cg, symbol_table, env_b, stmt)) { goto error_cleanup; }

    return 1;

error_cleanup:
    return 0;
}

static int pp_code_generator_emit_global_variable(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                  PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_VariableDeclarationNode);

    const char* var_name = stmt->variable_declaration.var_decl->param_declaration.identifier;
    assert(var_name);
    const PPSymbol* var_symbol = pp_symbol_table_lookup(symbol_table, var_name, false);
    assert(var_symbol);
    const PPType* var_type = var_symbol->type;
    assert(var_type);

    LLVMTypeRef llvm_var_type = pp_environment_backend_lookup_type(cg, var_type);
    assert(llvm_var_type);

    LLVMValueRef llvm_global_var = LLVMAddGlobal(cg->mod, llvm_var_type, var_name);

    if (pp_environment_backend_push_val_map(env_b, var_symbol, llvm_global_var) != 1) { return 0; }

    if (stmt->variable_declaration.initializer) {
        // Note that variable cannot be reference.
        LLVMValueRef llvm_initializer =
            pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->variable_declaration.initializer);
        assert(llvm_initializer);
        LLVMSetInitializer(llvm_global_var, llvm_initializer);
    } else {
        LLVMSetInitializer(llvm_global_var, LLVMConstNull(llvm_var_type));
    }

    return 1;
}

static LLVMValueRef pp_code_generator_generate_lvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    LLVMValueRef llvm_val = NULL;

    switch (node->node_type) {
        case PP_AST_IdentifierNode:
            llvm_val = pp_code_generator_generate_lvalue_identifier(cg, symbol_table, env_b, node);
            break;
            /*
                case PP_AST_StructMemberAccessNode:
                    llvm_val = pp_code_generator_generate_lvalue_struct_member_access(cg, symbol_table, env_b, node);
                    break;
                case PP_AST_FunctionCallNode:
                    llvm_val = pp_code_generator_generate_lvalue_function_call(cg, symbol_table, env_b, node);
                    break;
            */
        default:
            assert(0 && "Cannot get l-value");
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_l_value_to_r_value(PP_CodeGenerator* cg, const PPType* type,
                                                         LLVMValueRef lvalue) {
    LLVMValueRef llvm_val = NULL;
    assert(cg);
    assert(type);
    assert(lvalue);

    const PPType* underlying_type = type;
    if (pp_type_is_reference(type)) {  // Reference is always the outermost wrapper (per type_system.md).
        underlying_type = type->type.pp_type_reference.reference_to;
    }
    assert(underlying_type);

    LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, underlying_type);
    assert(llvm_type);

    llvm_val = LLVMBuildLoad2(cg->builder, llvm_type, lvalue, "lval_to_rval");
    assert(llvm_val);

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_lvalue_identifier(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_IdentifierNode);

    const char* name = node->identifier.identifier;
    assert(name);

    const PPSymbol* symbol = pp_symbol_table_lookup(symbol_table, name, true);
    assert(symbol);

    LLVMValueRef llvm_val = pp_environment_backend_lookup_val(env_b, symbol, true);
    assert(llvm_val);

    if (pp_type_is_reference(symbol->type)) {
        LLVMTypeRef ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
        llvm_val = LLVMBuildLoad2(cg->builder, ptr_type, llvm_val, "ref_deref");
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    LLVMValueRef llvm_val = NULL;

    switch (node->node_type) {
        case PP_AST_BinaryExpressionNode:
            llvm_val = pp_code_generator_generate_rvalue_binary(cg, symbol_table, env_b, node, false);
            break;
        case PP_AST_UnaryExpressionNode:
            llvm_val = pp_code_generator_generate_rvalue_unary(cg, symbol_table, env_b, node, false);
            break;
            /*
                case PP_AST_CastExpressionNode:
                    llvm_val = pp_code_generator_generate_rvalue_cast(cg, symbol_table, env_b, node, false);
                    break;
            */
        case PP_AST_LiteralNode:
            llvm_val = pp_code_generator_generate_rvalue_literal(cg, symbol_table, env_b, node, false);
            break;
            /*
                case PP_AST_StructInitNode:
                    llvm_val = pp_code_generator_generate_rvalue_struct_init(cg, symbol_table, env_b, node, false);
                    break;
                case PP_AST_CollectionInitializerNode:
                    //llvm_val = pp_code_generator_generate_rvalue_collection_init(cg, symbol_table, env_b, node,
               false); break; case PP_AST_StructMemberAccessNode:
                    {
                        LLVMValueRef llvm_struct_access_ref = pp_code_generator_generate_lvalue(cg, symbol_table, env_b,
               node); llvm_val = pp_code_generator_l_value_to_r_value(cg, node->decoration->resolved_type,
               llvm_struct_access_ref);
                    }
                    break;
                case PP_AST_FunctionCallNode:
                    {
                        LLVMValueRef llvm_function_call = pp_code_generator_generate_lvalue(cg, symbol_table, env_b,
               node); const PPType* ret_type = node->decoration->resolved_type;

                        if (ret_type->kind == PP_TYPE_KIND_REFERENCE) {
                            llvm_val = pp_code_generator_l_value_to_r_value(cg, node->decoration->resolved_type,
               llvm_function_call); } else { llvm_val = llvm_function_call;
                        }
                    }
                    break;
            */
        case PP_AST_IdentifierNode:
            llvm_val = pp_code_generator_generate_rvalue_identifier(cg, symbol_table, env_b, node);
            break;
        default:
            assert(0 && "Cannot get r-value");
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_binary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                             PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                             bool is_compile_time) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_BinaryExpressionNode);

    LLVMValueRef llvm_val = NULL;

    LLVMValueRef llvm_lhs = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.left);
    assert(llvm_lhs);
    LLVMValueRef llvm_rhs = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.right);
    assert(llvm_rhs);
    const PP_AST_Decoration* lhs_decoration = pp_ast_get_decoration(node->binary_expression.left);
    assert(lhs_decoration);
    const PP_AST_Decoration* rhs_decoration = pp_ast_get_decoration(node->binary_expression.right);
    assert(rhs_decoration);
    LLVMTypeRef llvm_lhs_type = pp_environment_backend_lookup_type(cg, lhs_decoration->resolved_type);
    assert(llvm_lhs_type);
    LLVMTypeRef llvm_rhs_type = pp_environment_backend_lookup_type(cg, rhs_decoration->resolved_type);
    assert(llvm_rhs_type);
    LLVMTypeKind llvm_lhs_type_kind = LLVMGetTypeKind(llvm_lhs_type);
    assert(llvm_lhs_type_kind);
    LLVMTypeKind llvm_rhs_type_kind = LLVMGetTypeKind(llvm_rhs_type);
    assert(llvm_rhs_type_kind);

    switch (node->binary_expression.op) {
        case PP_TT_PLUS:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFAdd(cg->builder, llvm_lhs, llvm_rhs, "faddtmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildAdd(cg->builder, llvm_lhs, llvm_rhs, "addtmp");
            }
            break;
        case PP_TT_MINUS:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFSub(cg->builder, llvm_lhs, llvm_rhs, "fsubtmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildSub(cg->builder, llvm_lhs, llvm_rhs, "subtmp");
            }
            break;
        case PP_TT_STAR:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFMul(cg->builder, llvm_lhs, llvm_rhs, "fmultmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildMul(cg->builder, llvm_lhs, llvm_rhs, "multmp");
            }
            break;
        case PP_TT_PERCENT:
            if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (lhs_decayed_type->kind == PP_TK_UInt32) {
                    llvm_val = LLVMBuildURem(cg->builder, llvm_lhs, llvm_rhs, "uremtmp");
                } else {
                    llvm_val = LLVMBuildSRem(cg->builder, llvm_lhs, llvm_rhs, "sremtmp");
                }
            }
            break;
        case PP_TT_SLASH:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFDiv(cg->builder, llvm_lhs, llvm_rhs, "fdivtmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (lhs_decayed_type->kind == PP_TK_UInt32) {
                    llvm_val = LLVMBuildUDiv(cg->builder, llvm_lhs, llvm_rhs, "udivtmp");
                } else {
                    llvm_val = LLVMBuildSDiv(cg->builder, llvm_lhs, llvm_rhs, "sdivtmp");
                }
            }
            break;

        case PP_TT_AMPERSAND:
            if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildAnd(cg->builder, llvm_lhs, llvm_rhs, "andtmp");
            }
            break;
        case PP_TT_BAR:
            if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildOr(cg->builder, llvm_lhs, llvm_rhs, "ortmp");
            }
            break;
        case PP_TT_CARET:
            if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildXor(cg->builder, llvm_lhs, llvm_rhs, "xortmp");
            }
            break;
        case PP_TT_GREATER:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealOGT, llvm_lhs, llvm_rhs, "fgttmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (lhs_decayed_type->kind == PP_TK_UInt32) {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntUGT, llvm_lhs, llvm_rhs, "gttmp");
                } else {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntSGT, llvm_lhs, llvm_rhs, "gttmp");
                }
            }
            break;
        case PP_TT_LESS:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealOLT, llvm_lhs, llvm_rhs, "flttmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (lhs_decayed_type->kind == PP_TK_UInt32) {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntULT, llvm_lhs, llvm_rhs, "lttmp");
                } else {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntSLT, llvm_lhs, llvm_rhs, "lttmp");
                }
            }
            break;
        case PP_TT_GREATER_EQUAL:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealOGE, llvm_lhs, llvm_rhs, "fgetmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (lhs_decayed_type->kind == PP_TK_UInt32) {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntUGE, llvm_lhs, llvm_rhs, "getmp");
                } else {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntSGE, llvm_lhs, llvm_rhs, "getmp");
                }
            }
            break;
        case PP_TT_LESS_EQUAL:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealOLE, llvm_lhs, llvm_rhs, "fletmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (lhs_decayed_type->kind == PP_TK_UInt32) {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntULE, llvm_lhs, llvm_rhs, "letmp");
                } else {
                    llvm_val = LLVMBuildICmp(cg->builder, LLVMIntSLE, llvm_lhs, llvm_rhs, "letmp");
                }
            }
            break;
        case PP_TT_EQUAL_EQUAL:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealOEQ, llvm_lhs, llvm_rhs, "feqtmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildICmp(cg->builder, LLVMIntEQ, llvm_lhs, llvm_rhs, "eqtmp");
            }
            break;
        case PP_TT_EXCLAMATION_EQUAL:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealONE, llvm_lhs, llvm_rhs, "fneqtmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildICmp(cg->builder, LLVMIntNE, llvm_lhs, llvm_rhs, "neqtmp");
            }
            break;
        default:
#warning "&& and !! require control-flow branch generation, hence not handled yet."
            assert(0 && "Unhandled binary operator");
            return NULL;
    }

    if (llvm_val == NULL) { return NULL; }
    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_unary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                            PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                            bool is_compile_time) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_UnaryExpressionNode);

    LLVMValueRef llvm_val = NULL;

    LLVMValueRef llvm_operand =
        pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->unary_expression.operand);
    LLVMTypeRef llvm_operand_type = LLVMTypeOf(llvm_operand);
    LLVMTypeKind llvm_operand_type_kind = LLVMGetTypeKind(llvm_operand_type);

    switch (node->unary_expression.op) {
        case PP_TT_MINUS:
            switch (llvm_operand_type_kind) {
                case LLVMFloatTypeKind: {
                    const LLVMValueRef zero_const = LLVMConstReal(LLVMFloatTypeInContext(cg->ctx), 0);
                    llvm_val = LLVMBuildFSub(cg->builder, zero_const, llvm_operand, "fnegtmp");
                } break;
                case LLVMIntegerTypeKind: {
                    const LLVMValueRef zero_const = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, true);
                    llvm_val = LLVMBuildSub(cg->builder, zero_const, llvm_operand, "negtmp");
                } break;
                default:
                    assert(0 && "Unary minus type failure");
                    break;
            }
            break;
        case PP_TT_EXCLAMATION: {
            if (llvm_operand_type_kind == LLVMIntegerTypeKind) {
                const LLVMValueRef one_const = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 1, true);
                llvm_val = LLVMBuildXor(cg->builder, one_const, llvm_operand, "nottmp");
            }
        } break;
        default:
            assert(0 && "Unary expression failure");
            return NULL;
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_cast(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                           bool is_compile_time) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    // Stubbed for now.
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                              PP_Environment_Backend* env_b, const PP_AST_Node* node,
                                                              bool is_compile_time) {
    LLVMValueRef llvm_val = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);

    switch (node->literal.literal_type) {
        case PP_TT_INTEGER_LITERAL: {
            char* endptr = NULL;
            errno = 0;
            long long val = strtoll(node->literal.lexeme, &endptr, 10);
            if (errno != 0 || node->literal.lexeme == endptr) { return NULL; }
            llvm_val = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), (unsigned long long)val, 1);
        } break;
        case PP_TT_UNSIGNED_INTEGER_LITERAL: {
            char* endptr = NULL;
            errno = 0;
            unsigned long long val = strtoull(node->literal.lexeme, &endptr, 10);
            if (errno != 0 || node->literal.lexeme == endptr) { return NULL; }
            llvm_val = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), val, 0);
        } break;
        case PP_TT_FLOAT_LITERAL: {
            char* endptr = NULL;
            errno = 0;
            double val = strtod(node->literal.lexeme, &endptr);
            if (errno != 0 || node->literal.lexeme == endptr) { return NULL; }
            llvm_val = LLVMConstReal(LLVMFloatTypeInContext(cg->ctx), val);
        } break;
        case PP_TT_TRUE:
            llvm_val = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 1, false);
            break;
        case PP_TT_FALSE:
            llvm_val = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 0, false);
            break;
        default:
            assert(0 && "Invalid literal");
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_struct_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                  PP_Environment_Backend* env_b,
                                                                  const PP_AST_Node* node, bool is_compile_time) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    // Stubbed for now.
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_collection_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                      PP_Environment_Backend* env_b,
                                                                      const PP_AST_Node* node, bool is_compile_time) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    // Stubbed for now.
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_struct_member_access(PP_CodeGenerator* cg,
                                                                           PPSymbolTable* symbol_table,
                                                                           PP_Environment_Backend* env_b,
                                                                           const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    // Stubbed for now.
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_function_call(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                    PP_Environment_Backend* env_b,
                                                                    const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    // Stubbed for now.
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_identifier(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node) {
    LLVMValueRef llvm_val = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_IdentifierNode);

    LLVMValueRef llvm_identifier_ref = pp_code_generator_generate_lvalue_identifier(cg, symbol_table, env_b, node);
    assert(llvm_identifier_ref);

    const PP_AST_Decoration* identifier_decoration = pp_ast_get_decoration(node);
    assert(identifier_decoration);
    assert(identifier_decoration->resolved_type);

    if (identifier_decoration->resolved_type->kind == PP_TK_Function ||
        identifier_decoration->resolved_type->kind == PP_TK_Lambda) {
        llvm_val = llvm_identifier_ref;
    } else {
        llvm_val = pp_code_generator_l_value_to_r_value(cg, identifier_decoration->resolved_type, llvm_identifier_ref);
    }

    return llvm_val;
}
