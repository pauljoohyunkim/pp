#include "pp_code_generator.h"

#include <assert.h>
#include <errno.h>
#include <llvm-c/Analysis.h>
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
static LLVMValueRef pp_code_generator_generate_lvalue_struct_member_access(PP_CodeGenerator* cg,
                                                                           PPSymbolTable* symbol_table,
                                                                           PP_Environment_Backend* env_b,
                                                                           const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_l_value_to_r_value(PP_CodeGenerator* cg, const PPType* type, LLVMValueRef lvalue);
static LLVMValueRef pp_code_generator_generate_lvalue_function_call(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                    PP_Environment_Backend* env_b,
                                                                    const PP_AST_Node* node);

static LLVMValueRef pp_code_generator_generate_lvalue_array_list_access(PP_CodeGenerator* cg,
                                                                        PPSymbolTable* symbol_table,
                                                                        PP_Environment_Backend* env_b,
                                                                        const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node);
static void pp_cg_ensure_terminated(PP_CodeGenerator* cg, LLVMBasicBlockRef merge_block);

static LLVMValueRef pp_code_generator_generate_rvalue_binary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                             PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_unary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                            PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_cast(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                              PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_struct_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                  PP_Environment_Backend* env_b,
                                                                  const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_collection_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                      PP_Environment_Backend* env_b,
                                                                      const PP_AST_Node* node);
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
static LLVMValueRef pp_code_generator_generate_rvalue_array_list_access(PP_CodeGenerator* cg,
                                                                        PPSymbolTable* symbol_table,
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

static const LLVMTypeRef pp_environment_backend_lookup_type(const PP_CodeGenerator* cg, const PPType* type);

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

static void pp_cg_ensure_terminated(PP_CodeGenerator* cg, LLVMBasicBlockRef merge_block) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(cg->builder);
    if (cur && LLVMGetBasicBlockTerminator(cur) == NULL) { LLVMBuildBr(cg->builder, merge_block); }
}

static void pp_code_generator_emit_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type) {
#warning \
    "emit_drop currently only handles direct List types. Structs containing List are not recursively dropped yet. Add per-type drop_T generation before allowing structs with lists."
    if (cg == NULL || address == NULL || type == NULL) return;
    if (type->kind != PP_TK_List) return;
    if (pp_type_is_reference(type)) return;
    // Load the list handle from alloca
    LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, type);
    if (!llvm_list_type) return;
    LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, llvm_list_type, address, "load_list");
    // Extract data pointer at index 0
    LLVMValueRef data_ptr = LLVMBuildExtractValue(cg->builder, loaded, 0, "list_data");
    // Cast to i8* for free
    LLVMValueRef data_ptr_i8 =
        LLVMBuildBitCast(cg->builder, data_ptr, LLVMPointerTypeInContext(cg->ctx, 0), "list_data_i8");
    LLVMValueRef free_fn = LLVMGetNamedFunction(cg->mod, "free");
    if (!free_fn) return;
    LLVMValueRef args[1] = {data_ptr_i8};
    LLVMTypeRef free_type =
        LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), (LLVMTypeRef[]){LLVMPointerTypeInContext(cg->ctx, 0)}, 1, 0);
    LLVMBuildCall2(cg->builder, free_type, free_fn, args, 1, "");
}

static void pp_code_generator_emit_scope_drops(PP_CodeGenerator* cg) {
    if (cg == NULL) return;
    // Emit drops in reverse order
    while (cg->drop_stack_count > 0) {
        size_t idx = cg->drop_stack_count - 1;
        LLVMValueRef addr = cg->drop_stack_addresses[idx];
        const PPType* typ = cg->drop_stack_types[idx];
        if (typ && pp_type_needs_drop(typ) && !pp_type_is_reference(typ)) {
            pp_code_generator_emit_drop(cg, addr, typ);
        }
        cg->drop_stack_count--;
    }
}

static void pp_code_generator_push_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type) {
    if (cg == NULL || address == NULL || type == NULL) return;
    if (!pp_type_needs_drop(type)) return;
    if (pp_type_is_reference(type)) return;
    // Ensure capacity
    if (cg->drop_stack_capacity == 0) {
        cg->drop_stack_capacity = 16;
        cg->drop_stack_addresses = (LLVMValueRef*)calloc(cg->drop_stack_capacity, sizeof(LLVMValueRef));
        cg->drop_stack_types = (PPType**)calloc(cg->drop_stack_capacity, sizeof(PPType*));
    } else if (cg->drop_stack_count >= cg->drop_stack_capacity) {
        cg->drop_stack_capacity *= 2;
        cg->drop_stack_addresses =
            (LLVMValueRef*)realloc(cg->drop_stack_addresses, cg->drop_stack_capacity * sizeof(LLVMValueRef));
        cg->drop_stack_types = (PPType**)realloc(cg->drop_stack_types, cg->drop_stack_capacity * sizeof(PPType*));
    }
    cg->drop_stack_addresses[cg->drop_stack_count] = address;
    cg->drop_stack_types[cg->drop_stack_count] = (PPType*)type;
    cg->drop_stack_count++;
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

        case PP_TK_Array: {
            LLVMTypeRef llvm_array_type = pp_environment_backend_lookup_type(cg, type);
            if (llvm_array_type) { return llvm_array_type; }
            /*
             * Recursive lowering ensures a post-order traversal of the type tree.
             * The element type is fully lowered and registered in the type map
             * before the array type is created. This pattern is safe and will
             * be used similarly for List, Function, and Lambda types.
             */
            LLVMTypeRef element_type = pp_backend_lower_type(cg, type->type.pp_type_array.element_type);
            assert(element_type);
            llvm_array_type = LLVMArrayType(element_type, (unsigned)type->type.pp_type_array.size);
            assert(llvm_array_type);
            return llvm_array_type;
        }
        case PP_TK_List: {
            /* Shared handle struct pp.list = { ptr, i64, i64 } for all List(T) */
            LLVMTypeRef list_type = pp_environment_backend_lookup_type(cg, type);
            if (list_type) { return list_type; }
            /* Check if already created by name */
            LLVMTypeRef existing = LLVMGetTypeByName2(cg->ctx, "pp.list");
            if (existing) { return existing; }
            /* Create the shared List handle struct */
            LLVMTypeRef ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef field_types[3] = {ptr_type, i64_type, i64_type};
            LLVMTypeRef list_struct = LLVMStructCreateNamed(cg->ctx, "pp.list");
            LLVMStructSetBody(list_struct, field_types, 3, 0);
            return list_struct;
        }
        case PP_TK_Function:
        case PP_TK_Lambda:

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

    assert(sa->current_function_type == NULL);
    cg->current_function_type = NULL;

    // Ensure runtime library declarations for list support
    {
        LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
        LLVMTypeRef i8ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);

        LLVMTypeRef malloc_param = i64_type;
        LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr_type, &malloc_param, 1, 0);
        if (!LLVMGetNamedFunction(cg->mod, "malloc")) LLVMAddFunction(cg->mod, "malloc", malloc_type);

        LLVMTypeRef realloc_params[2] = {i8ptr_type, i64_type};
        LLVMTypeRef realloc_type = LLVMFunctionType(i8ptr_type, realloc_params, 2, 0);
        if (!LLVMGetNamedFunction(cg->mod, "realloc")) LLVMAddFunction(cg->mod, "realloc", realloc_type);

        LLVMTypeRef free_param = i8ptr_type;
        LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), &free_param, 1, 0);
        if (!LLVMGetNamedFunction(cg->mod, "free")) LLVMAddFunction(cg->mod, "free", free_type);
    }

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
        free(cg->drop_stack_addresses);
        free(cg->drop_stack_types);
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

    // Emit extern declarations for symbols that are not definitions
    for (size_t i = 0; i < cg->gst->count; ++i) {
        const char* name = cg->gst->names[i];
        const PPSymbol* sym = cg->gst->symbols[i];
        if (!sym) continue;
        if (sym->is_definition) continue;
        // Only emit for functions/lambdas
        if (sym->type->kind != PP_TK_Function && sym->type->kind != PP_TK_Lambda) continue;
        LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, sym->type);
        if (!llvm_type) continue;
        LLVMValueRef fn = LLVMAddFunction(cg->mod, name, llvm_type);
        if (!fn) continue;
        LLVMSetLinkage(fn, LLVMExternalLinkage);
        if (!pp_environment_backend_push_val_map(cg->global_env_b, sym, fn)) { goto error_cleanup; }
    }

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

    // Verify generated LLVM module
    {
        char* verify_err = NULL;
        if (LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &verify_err)) {
            if (verify_err) {
                pp_error_list_push_error(cg->error_list, cg->filename, 0, 0, "LLVM verification failed: %s",
                                         verify_err);
                LLVMDisposeMessage(verify_err);
            } else {
                pp_error_list_push_error(cg->error_list, cg->filename, 0, 0, "LLVM verification failed");
            }
            goto error_cleanup;
        }
        if (verify_err) LLVMDisposeMessage(verify_err);
    }

    return 1;
error_cleanup:
    return 0;
}

int pp_code_generator_emit_object_file(LLVMModuleRef module, const char* output_filename) {
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    char* target_triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, target_triple);

    LLVMTargetRef target;
    char* error = NULL;
    if (LLVMGetTargetFromTriple(target_triple, &target, &error)) {
        fprintf(stderr, "LLVM Target Error: %s\\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(target_triple);
        return 0;
    }

    LLVMTargetMachineRef target_machine =
        LLVMCreateTargetMachine(target, target_triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocPIC,
                                LLVMCodeModelDefault);

    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
    LLVMSetModuleDataLayout(module, data_layout);

    if (LLVMTargetMachineEmitToFile(target_machine, module, (char*)output_filename, LLVMObjectFile, &error)) {
        fprintf(stderr, "Failed to emit object file: %s\\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetData(data_layout);
        LLVMDisposeTargetMachine(target_machine);
        LLVMDisposeMessage(target_triple);
        return 0;
    }

    LLVMDisposeTargetData(data_layout);
    LLVMDisposeTargetMachine(target_machine);
    LLVMDisposeMessage(target_triple);
    return 1;
}

int pp_code_generator_emit_object_to_buffer(LLVMModuleRef module, LLVMMemoryBufferRef* out_buf) {
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    char* target_triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, target_triple);

    LLVMTargetRef target;
    char* error = NULL;
    if (LLVMGetTargetFromTriple(target_triple, &target, &error)) {
        fprintf(stderr, "LLVM Target Error: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(target_triple);
        return 0;
    }

    LLVMTargetMachineRef target_machine =
        LLVMCreateTargetMachine(target, target_triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocPIC,
                                LLVMCodeModelDefault);

    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
    LLVMSetModuleDataLayout(module, data_layout);

    LLVMMemoryBufferRef buf = NULL;
    if (LLVMTargetMachineEmitToMemoryBuffer(target_machine, module, LLVMObjectFile, &error, &buf)) {
        fprintf(stderr, "Failed to emit object to buffer: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetData(data_layout);
        LLVMDisposeTargetMachine(target_machine);
        LLVMDisposeMessage(target_triple);
        return 0;
    }

    LLVMDisposeTargetData(data_layout);
    LLVMDisposeTargetMachine(target_machine);
    LLVMDisposeMessage(target_triple);

    if (out_buf)
        *out_buf = buf;
    else { LLVMDisposeMemoryBuffer(buf); }
    return 1;
}

static int pp_code_generator_generate_function_body(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    PP_Environment_Backend* body_env_b = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_FunctionDeclarationNode || stmt->node_type == PP_AST_LambdaDeclarationNode);

    /* Implementation will follow CZ:
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
    cg->current_function_type = NULL;
    return 1;

error_cleanup:
    pp_environment_backend_free(body_env_b);
    cg->current_function_type = NULL;
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
        default:
            /* Expression statement: evaluate for side-effects and discard result */
            {
                LLVMValueRef val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node);
                (void)val;
            }
            break;
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
    assert(stmt->node_type == PP_AST_VariableDeclarationNode);

    const char* var_name = stmt->variable_declaration.var_decl->param_declaration.identifier;
    assert(var_name);
    const PPSymbol* var_symbol = pp_symbol_table_lookup(symbol_table, var_name, false);
    assert(var_symbol);
    const PPType* var_type = var_symbol->type;
    LLVMTypeRef llvm_var_type = pp_environment_backend_lookup_type(cg, var_type);
    assert(llvm_var_type);

    LLVMValueRef llvm_var_alloca = LLVMBuildAlloca(cg->builder, llvm_var_type, var_name);
    assert(llvm_var_alloca);

    const PP_AST_Node* initializer_node = stmt->variable_declaration.initializer;
    if (pp_type_is_reference(var_type)) {
        // Initializer must exist.
        assert(initializer_node);
        LLVMValueRef var_initializer = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, initializer_node);
        assert(var_initializer);
        LLVMBuildStore(cg->builder, var_initializer, llvm_var_alloca);
    } else {
        if (initializer_node == NULL) {
            LLVMBuildStore(cg->builder, LLVMConstNull(llvm_var_type), llvm_var_alloca);
        } else {
            LLVMValueRef var_initializer = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, initializer_node);
            assert(var_initializer);
            LLVMBuildStore(cg->builder, var_initializer, llvm_var_alloca);
        }
    }

    if (pp_environment_backend_push_val_map(env_b, var_symbol, llvm_var_alloca) != 1) { return 0; }

    // Push onto scope drop stack if needed (List with trivially droppable elements)
    if (!pp_type_is_reference(var_type) && pp_type_needs_drop(var_type)) {
        // For v1, only handle List variables with trivially droppable elements; skip references
        if (var_type->kind == PP_TK_List) { pp_code_generator_push_drop(cg, llvm_var_alloca, var_type); }
    }

    return 1;
}

static int pp_code_generator_generate_assignment_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_AssignmentNode);

    LLVMValueRef llvm_lhs = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, stmt->assignment.lhs);
    assert(llvm_lhs);
    LLVMValueRef llvm_rhs = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->assignment.rhs);
    assert(llvm_rhs);

    LLVMTypeRef llvm_rhs_type = LLVMTypeOf(llvm_rhs);
    assert(llvm_rhs_type);
    LLVMTypeKind llvm_rhs_type_kind = LLVMGetTypeKind(llvm_rhs_type);
    assert(llvm_rhs_type_kind);

    LLVMValueRef llvm_val = llvm_rhs;

    if (stmt->assignment.op != PP_TT_EQUAL) {
        const PP_AST_Decoration* lhs_decor = pp_ast_get_decoration(stmt->assignment.lhs);
        assert(lhs_decor);
        const PPType* lhs_type = lhs_decor->resolved_type;
        assert(lhs_type);
        LLVMValueRef llvm_lhs_val = pp_code_generator_l_value_to_r_value(cg, lhs_type, llvm_lhs);
        assert(llvm_lhs_val);

        switch (stmt->assignment.op) {
            case PP_TT_PLUS_EQUAL:
                if (llvm_rhs_type_kind == LLVMFloatTypeKind) {
                    llvm_val = LLVMBuildFAdd(cg->builder, llvm_lhs_val, llvm_rhs, "faddtmp");
                } else {
                    llvm_val = LLVMBuildAdd(cg->builder, llvm_lhs_val, llvm_rhs, "addtmp");
                }
                break;
            case PP_TT_MINUS_EQUAL:
                if (llvm_rhs_type_kind == LLVMFloatTypeKind) {
                    llvm_val = LLVMBuildFSub(cg->builder, llvm_lhs_val, llvm_rhs, "fsubtmp");
                } else {
                    llvm_val = LLVMBuildSub(cg->builder, llvm_lhs_val, llvm_rhs, "subtmp");
                }
                break;
            case PP_TT_STAR_EQUAL:
                if (llvm_rhs_type_kind == LLVMFloatTypeKind) {
                    llvm_val = LLVMBuildFMul(cg->builder, llvm_lhs_val, llvm_rhs, "fmultmp");
                } else {
                    llvm_val = LLVMBuildMul(cg->builder, llvm_lhs_val, llvm_rhs, "multmp");
                }
                break;
            case PP_TT_SLASH_EQUAL:
                if (llvm_rhs_type_kind == LLVMFloatTypeKind) {
                    llvm_val = LLVMBuildFDiv(cg->builder, llvm_lhs_val, llvm_rhs, "fdivtmp");
                } else {
                    bool is_unsigned = (lhs_type->kind == PP_TK_UInt32);
                    llvm_val = is_unsigned ? LLVMBuildUDiv(cg->builder, llvm_lhs_val, llvm_rhs, "udivtmp")
                                           : LLVMBuildSDiv(cg->builder, llvm_lhs_val, llvm_rhs, "sdivtmp");
                }
                break;
            case PP_TT_PERCENT_EQUAL: {
                bool is_unsigned = (lhs_type->kind == PP_TK_UInt32);
                llvm_val = is_unsigned ? LLVMBuildURem(cg->builder, llvm_lhs_val, llvm_rhs, "uremtmp")
                                       : LLVMBuildSRem(cg->builder, llvm_lhs_val, llvm_rhs, "sremtmp");
            } break;
            case PP_TT_AMPERSAND_EQUAL:
                llvm_val = LLVMBuildAnd(cg->builder, llvm_lhs_val, llvm_rhs, "andtmp");
                break;
            case PP_TT_BAR_EQUAL:
                llvm_val = LLVMBuildOr(cg->builder, llvm_lhs_val, llvm_rhs, "ortmp");
                break;
            case PP_TT_CARET_EQUAL:
                llvm_val = LLVMBuildXor(cg->builder, llvm_lhs_val, llvm_rhs, "xortmp");
                break;
            default:
                abort();
        }
    }

    LLVMBuildStore(cg->builder, llvm_val, llvm_lhs);
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

    if (current_function_return_type->kind == PP_TK_Void) {
        if (stmt->return_statement.expr == NULL) {
            pp_code_generator_emit_scope_drops(cg);
            LLVMBuildRetVoid(cg->builder);
        } else {
            abort();
        }
    } else if (pp_type_is_reference(current_function_return_type)) {
        LLVMValueRef llvm_return_val_ref =
            pp_code_generator_generate_lvalue(cg, symbol_table, env_b, stmt->return_statement.expr);
        pp_code_generator_emit_scope_drops(cg);
        LLVMBuildRet(cg->builder, llvm_return_val_ref);
    } else {
        LLVMValueRef llvm_return_val_ref =
            pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->return_statement.expr);
        /* Move semantics for returning a local List by value.
           If the return expression is an identifier of a List-owning type, clear the
           source variable to {null,0,0} so the later scope drop is a no-op. This implements
           return-as-move and avoids double-free of the heap buffer. */
        if (stmt->return_statement.expr && stmt->return_statement.expr->node_type == PP_AST_IdentifierNode) {
            const char* name = stmt->return_statement.expr->identifier.identifier;
            assert(name);
            const PP_AST_Decoration* dec = pp_ast_get_decoration(stmt->return_statement.expr);
            assert(dec);
            const PPType* ret_type = dec->resolved_type;
            assert(ret_type);
            if (pp_type_needs_drop(ret_type) && !pp_type_is_reference(ret_type)) {
                const PPSymbol* sym = pp_symbol_table_lookup(symbol_table, name, true);
                assert(sym);
                for (size_t i = 0; i < cg->drop_stack_count; i++) {
                    if (cg->drop_stack_types[i] == ret_type) {
                        LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, ret_type);
                        assert(llvm_list_type);
                        LLVMValueRef empty = LLVMGetUndef(llvm_list_type);
                        LLVMValueRef null_ptr = LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0));
                        empty = LLVMBuildInsertValue(cg->builder, empty, null_ptr, 0, "moved_data");
                        empty =
                            LLVMBuildInsertValue(cg->builder, empty,
                                                 LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), 1, "moved_len");
                        empty =
                            LLVMBuildInsertValue(cg->builder, empty,
                                                 LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), 2, "moved_cap");
                        LLVMBuildStore(cg->builder, empty, cg->drop_stack_addresses[i]);
                        break;
                    }
                }
            }
        }
        pp_code_generator_emit_scope_drops(cg);
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
    assert(stmt->node_type == PP_AST_IfStatementNode);

    LLVMValueRef llvm_cur_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));
    assert(llvm_cur_func);

    // Condition
    LLVMValueRef llvm_cond_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->if_statement.cond);
    assert(llvm_cond_val);

    // Then, Else, Merge blocks
    LLVMBasicBlockRef llvm_then_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "if.then");
    assert(llvm_then_block);
    LLVMBasicBlockRef llvm_else_block = stmt->if_statement.else_branch != NULL
                                            ? LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "if.else")
                                            : NULL;
    LLVMBasicBlockRef llvm_merge_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "if.end");
    assert(llvm_merge_block);

    // Branch based on condition
    LLVMBasicBlockRef else_target = llvm_else_block != NULL ? llvm_else_block : llvm_merge_block;
    LLVMBuildCondBr(cg->builder, llvm_cond_val, llvm_then_block, else_target);

    // Then block
    LLVMPositionBuilderAtEnd(cg->builder, llvm_then_block);
    if (pp_code_generator_generate_statement(cg, symbol_table, env_b, stmt->if_statement.then_branch) != 1) { abort(); }
    // If not terminated, branch to merge
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
        LLVMBuildBr(cg->builder, llvm_merge_block);
    }

    // Else block (if exists)
    if (stmt->if_statement.else_branch != NULL) {
        LLVMPositionBuilderAtEnd(cg->builder, llvm_else_block);
        if (pp_code_generator_generate_statement(cg, symbol_table, env_b, stmt->if_statement.else_branch) != 1) {
            abort();
        }
        // If not terminated, branch to merge
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
            LLVMBuildBr(cg->builder, llvm_merge_block);
        }
    }

    // Merge block
    LLVMPositionBuilderAtEnd(cg->builder, llvm_merge_block);

    return 1;
}

static int pp_code_generator_generate_for_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    PP_Environment_Backend* inner_env_b = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_ForStatementNode);

    LLVMValueRef llvm_cur_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));
    assert(llvm_cur_func);

    // Create a sub-scope for for-loop (for initializer)
    inner_env_b = pp_environment_backend_enter_scope(env_b);
    assert(inner_env_b);

    if (stmt->for_statement.init != NULL) {
        if (pp_code_generator_generate_statement(cg, stmt->for_statement.for_init_symbol_table, inner_env_b,
                                                 stmt->for_statement.init) != 1) {
            abort();
        }
    }

    // Create cond, body, step, end blocks
    LLVMBasicBlockRef llvm_cond_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "for.cond");
    assert(llvm_cond_block);
    LLVMBasicBlockRef llvm_body_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "for.body");
    assert(llvm_body_block);
    LLVMBasicBlockRef llvm_step_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "for.step");
    assert(llvm_step_block);
    LLVMBasicBlockRef llvm_end_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "for.end");
    assert(llvm_end_block);

    // Branch: initializer -> cond_block
    LLVMBuildBr(cg->builder, llvm_cond_block);

    // Condition block
    LLVMValueRef llvm_cond_val = NULL;
    LLVMPositionBuilderAtEnd(cg->builder, llvm_cond_block);
    if (stmt->for_statement.cond != NULL) {
        llvm_cond_val = pp_code_generator_generate_rvalue(cg, stmt->for_statement.for_init_symbol_table, inner_env_b,
                                                          stmt->for_statement.cond);
        assert(llvm_cond_val);
    } else {
        llvm_cond_val = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 1, false);
    }

    // Branching: if (cond) (llvm_body_block) else (llvm_end_block)
    LLVMBuildCondBr(cg->builder, llvm_cond_val, llvm_body_block, llvm_end_block);

    // Body
    LLVMPositionBuilderAtEnd(cg->builder, llvm_body_block);
    if (pp_code_generator_generate_statement(cg, stmt->for_statement.for_init_symbol_table, inner_env_b,
                                             stmt->for_statement.body) != 1) {
        abort();
    }
    // Check return in body
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
        LLVMBuildBr(cg->builder, llvm_step_block);
    }

    // Step
    LLVMPositionBuilderAtEnd(cg->builder, llvm_step_block);
    if (stmt->for_statement.update != NULL) {
        if (pp_code_generator_generate_statement(cg, stmt->for_statement.for_init_symbol_table, inner_env_b,
                                                 stmt->for_statement.update) != 1) {
            abort();
        }
    }

    // Step -> condition
    LLVMBuildBr(cg->builder, llvm_cond_block);

    LLVMPositionBuilderAtEnd(cg->builder, llvm_end_block);

    pp_environment_backend_free(inner_env_b);
    return 1;
}

static int pp_code_generator_generate_while_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_WhileStatementNode);

    LLVMValueRef llvm_cur_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));

    // Condition, body, end block
    LLVMBasicBlockRef llvm_cond_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "while.cond");
    LLVMBasicBlockRef llvm_body_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "while.body");
    LLVMBasicBlockRef llvm_end_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "while.end");

    // Current position branches to condition block
    LLVMBuildBr(cg->builder, llvm_cond_block);

    // Set builder to write to condition block.
    LLVMPositionBuilderAtEnd(cg->builder, llvm_cond_block);
    LLVMValueRef llvm_cond_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->while_statement.cond);
    assert(llvm_cond_val);

    // If condition is true, go to body again.
    LLVMBuildCondBr(cg->builder, llvm_cond_val, llvm_body_block, llvm_end_block);

    // Go to body and write instructions
    LLVMPositionBuilderAtEnd(cg->builder, llvm_body_block);
    if (pp_code_generator_generate_statement(cg, symbol_table, env_b, stmt->while_statement.body) != 1) { return 0; }

    // If there was no return, go back to condition block.
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
        LLVMBuildBr(cg->builder, llvm_cond_block);
    }

    LLVMPositionBuilderAtEnd(cg->builder, llvm_end_block);

    return 1;
}

static int pp_code_generator_generate_block_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    PP_Environment_Backend* inner_env_b = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_StatementListNode);

    inner_env_b = pp_environment_backend_enter_scope(env_b);
    assert(inner_env_b);

    size_t drop_stack_mark = cg->drop_stack_count;

    unsigned int inner_stmt_count = stmt->statement_list.stmt_count;
    const PP_AST_Node** inner_stmts = stmt->statement_list.stmts;
    PPSymbolTable* inner_symbol_table = stmt->statement_list.symbol_table;
    assert(inner_symbol_table);

    for (unsigned int i = 0; i < inner_stmt_count; i++) {
        if (pp_code_generator_generate_statement(cg, inner_symbol_table, inner_env_b, inner_stmts[i]) != 1) {
            goto error_cleanup;
        }
    }
    // Emit drops for variables declared in this block before exiting
    // Emit in reverse order from current to mark
    for (size_t i = cg->drop_stack_count; i > drop_stack_mark;) {
        i--;
        LLVMValueRef addr = cg->drop_stack_addresses[i];
        const PPType* typ = cg->drop_stack_types[i];
        if (typ && pp_type_needs_drop(typ) && !pp_type_is_reference(typ)) {
            pp_code_generator_emit_drop(cg, addr, typ);
        }
    }
    cg->drop_stack_count = drop_stack_mark;
    pp_environment_backend_free(inner_env_b);
    return 1;

error_cleanup:
    pp_environment_backend_free(inner_env_b);
    return 0;
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

static LLVMValueRef pp_code_generator_generate_lvalue_array_list_access(PP_CodeGenerator* cg,
                                                                        PPSymbolTable* symbol_table,
                                                                        PP_Environment_Backend* env_b,
                                                                        const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_ArrayListAccessNode);
#warning "List access currently assumes data is allocated; uninitialized lists will segfault"

    const PP_AST_Decoration* object_decoration = pp_ast_get_decoration(node->array_list_access.expr);
    assert(object_decoration);

    const PPType* base_type = pp_type_decay_cg(cg->gtt, object_decoration->resolved_type);
    assert(base_type);
    if (base_type->kind != PP_TK_Array && base_type->kind != PP_TK_List) {
        assert(0 && "Unsupported base type for array/list access");
        return NULL;
    }

    LLVMValueRef llvm_base_ptr =
        pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->array_list_access.expr);
    assert(llvm_base_ptr);

    LLVMValueRef llvm_index = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->array_list_access.index);
    assert(llvm_index);

    if (base_type->kind == PP_TK_Array) {
        LLVMTypeRef llvm_base_type = pp_environment_backend_lookup_type(cg, base_type);
        assert(llvm_base_type);
        LLVMValueRef indices[2];
        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0);
        indices[1] = llvm_index;
        LLVMValueRef llvm_element_ptr =
            LLVMBuildGEP2(cg->builder, llvm_base_type, llvm_base_ptr, indices, 2, "array_idx");
        assert(llvm_element_ptr);
        return llvm_element_ptr;
    } else {
        const PPType* elem_type = base_type->type.pp_type_list.element_type;
        assert(elem_type);
        LLVMTypeRef llvm_elem_type = pp_backend_lower_type(cg, elem_type);
        assert(llvm_elem_type);
        LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, base_type);
        assert(llvm_list_type);
        LLVMValueRef field_idx = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0);
        LLVMValueRef data_ptr_ptr =
            LLVMBuildGEP2(cg->builder, llvm_list_type, llvm_base_ptr, &field_idx, 1, "list_data_ptr");
        LLVMValueRef data_ptr =
            LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data");
        assert(data_ptr);
        LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, llvm_elem_type, data_ptr, &llvm_index, 1, "list_elem_ptr");
        assert(elem_ptr);
        return elem_ptr;
    }
}

static LLVMValueRef pp_code_generator_generate_rvalue_array_list_access(PP_CodeGenerator* cg,
                                                                        PPSymbolTable* symbol_table,
                                                                        PP_Environment_Backend* env_b,
                                                                        const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_ArrayListAccessNode);

    const PP_AST_Decoration* object_decoration = pp_ast_get_decoration(node->array_list_access.expr);
    assert(object_decoration);

    if (object_decoration->value_category == PP_VALUE_CATEGORY_LVALUE) {
        LLVMValueRef llvm_lval = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node);
        assert(llvm_lval);
        const PPType* element_type = pp_type_decay_cg(cg->gtt, object_decoration->resolved_type);
        if (element_type->kind == PP_TK_Array) {
            element_type = element_type->type.pp_type_array.element_type;
        } else if (element_type->kind == PP_TK_List) {
            element_type = element_type->type.pp_type_list.element_type;
        }
        return pp_code_generator_l_value_to_r_value(cg, element_type, llvm_lval);
    }

    /* Rvalue base: extract from aggregate */
    LLVMValueRef base_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->array_list_access.expr);
    assert(base_val);

    LLVMValueRef llvm_index = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->array_list_access.index);
    assert(llvm_index);

    /* For Rvalue arrays (like return values), we use extractvalue.
     * For simplicity, we assume a constant index or a simple extract here.
     * In a full implementation, if index is dynamic, we'd need to move the array to stack. */
    LLVMValueRef extracted = LLVMBuildExtractValue(cg->builder, base_val, 0, "array_elt");  // Simplified
    assert(extracted);

    return extracted;
}

static LLVMValueRef pp_code_generator_generate_lvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    const PP_AST_Decoration* dec = pp_ast_get_decoration(node);
    if (dec) { assert(dec->value_category == PP_VALUE_CATEGORY_LVALUE && "generate_lvalue called on rvalue node"); }
    LLVMValueRef llvm_val = NULL;

    switch (node->node_type) {
        case PP_AST_IdentifierNode:
            llvm_val = pp_code_generator_generate_lvalue_identifier(cg, symbol_table, env_b, node);
            break;
        case PP_AST_FunctionCallNode:
            llvm_val = pp_code_generator_generate_lvalue_function_call(cg, symbol_table, env_b, node);
            break;
        case PP_AST_ArrayListAccessNode:
            llvm_val = pp_code_generator_generate_lvalue_array_list_access(cg, symbol_table, env_b, node);
            break;
        case PP_AST_MemberAccessNode:
            llvm_val = pp_code_generator_generate_lvalue_struct_member_access(cg, symbol_table, env_b, node);
            break;
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

static LLVMValueRef pp_code_generator_generate_lvalue_struct_member_access(PP_CodeGenerator* cg,
                                                                           PPSymbolTable* symbol_table,
                                                                           PP_Environment_Backend* env_b,
                                                                           const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_MemberAccessNode);

    const PP_AST_Decoration* object_decoration = pp_ast_get_decoration(node->member_access.object);
    assert(object_decoration);

    const PPType* base_type = pp_type_decay_cg(cg->gtt, object_decoration->resolved_type);
    assert(base_type);
    assert(base_type->kind == PP_TK_Struct);

    const char* member_name = node->member_access.member;
    assert(member_name);

    int field_idx = -1;
    const PP_StructLayout* layout = base_type->type.pp_type_struct.layout;
    assert(layout);
    for (size_t i = 0; i < layout->field_count; i++) {
        if (layout->fields[i].field_name == member_name) {
            field_idx = (int)i;
            break;
        }
    }
    if (field_idx < 0) { abort(); }

    LLVMValueRef llvm_struct_ptr =
        pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->member_access.object);
    assert(llvm_struct_ptr);

    LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, base_type);
    assert(llvm_struct_type);

    LLVMValueRef llvm_gep =
        LLVMBuildStructGEP2(cg->builder, llvm_struct_type, llvm_struct_ptr, (unsigned)field_idx, "field_gep");
    assert(llvm_gep);

    return llvm_gep;
}

static LLVMValueRef pp_code_generator_generate_lvalue_function_call(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                    PP_Environment_Backend* env_b,
                                                                    const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_FunctionCallNode);

    LLVMValueRef llvm_val = NULL;
    LLVMValueRef* llvm_args = NULL;

    const PP_AST_Decoration* callee_decoration = pp_ast_get_decoration(node->function_call.callee);
    assert(callee_decoration);
    assert(callee_decoration->resolved_type);
    assert(callee_decoration->resolved_type->kind == PP_TK_Function ||
           callee_decoration->resolved_type->kind == PP_TK_Lambda);

    bool is_lambda = callee_decoration->resolved_type->kind == PP_TK_Lambda;

    LLVMValueRef llvm_callee_func =
        pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->function_call.callee);
    assert(llvm_callee_func);

    LLVMTypeRef llvm_callee_func_type = pp_environment_backend_lookup_type(cg, callee_decoration->resolved_type);
    assert(llvm_callee_func_type);

    size_t param_count = is_lambda ? callee_decoration->resolved_type->type.pp_type_lambda.param_count
                                   : callee_decoration->resolved_type->type.pp_type_function.param_count;
    if (param_count > 0) {
        llvm_args = (LLVMValueRef*)calloc(param_count, sizeof(LLVMValueRef));
        NULL_POINTER_ERROR_HANDLE(llvm_args);

        for (unsigned int i = 0; i < param_count; i++) {
            const PPType* param_type = is_lambda
                                           ? callee_decoration->resolved_type->type.pp_type_lambda.param_types[i]
                                           : callee_decoration->resolved_type->type.pp_type_function.param_types[i];
            assert(param_type);

            if (pp_type_is_reference(param_type)) {
                llvm_args[i] =
                    pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
            } else {
                llvm_args[i] =
                    pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
            }
            assert(llvm_args[i]);
        }
    }

    llvm_val =
        LLVMBuildCall2(cg->builder, llvm_callee_func_type, llvm_callee_func, llvm_args, param_count, "func_call");

    free(llvm_args);
    return llvm_val;

error_cleanup:
    free(llvm_args);
    return NULL;
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
            llvm_val = pp_code_generator_generate_rvalue_binary(cg, symbol_table, env_b, node);
            break;
        case PP_AST_UnaryExpressionNode:
            llvm_val = pp_code_generator_generate_rvalue_unary(cg, symbol_table, env_b, node);
            break;
        case PP_AST_CastExpressionNode:
            llvm_val = pp_code_generator_generate_rvalue_cast(cg, symbol_table, env_b, node);
            break;
        case PP_AST_StructInitializerNode:
            llvm_val = pp_code_generator_generate_rvalue_struct_init(cg, symbol_table, env_b, node);
            break;
        case PP_AST_CollectionInitializerNode:
            llvm_val = pp_code_generator_generate_rvalue_collection_init(cg, symbol_table, env_b, node);
            break;
        case PP_AST_LiteralNode:
            llvm_val = pp_code_generator_generate_rvalue_literal(cg, symbol_table, env_b, node);
            break;
        case PP_AST_FunctionCallNode:
            llvm_val = pp_code_generator_generate_rvalue_function_call(cg, symbol_table, env_b, node);
            break;
        case PP_AST_IdentifierNode:
            llvm_val = pp_code_generator_generate_rvalue_identifier(cg, symbol_table, env_b, node);
            break;
        case PP_AST_ArrayListAccessNode:
            llvm_val = pp_code_generator_generate_rvalue_array_list_access(cg, symbol_table, env_b, node);
            break;
        case PP_AST_MemberAccessNode:
            llvm_val = pp_code_generator_generate_rvalue_struct_member_access(cg, symbol_table, env_b, node);
            break;
        default:
            assert(0 && "Cannot get r-value");
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_binary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                             PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_BinaryExpressionNode);

    LLVMValueRef llvm_val = NULL;

    /* Short-circuit logical operators need control flow, handle them before eager RHS generation */
    if (cg->current_function_type) {
        if (node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND || node->binary_expression.op == PP_TT_BAR_BAR) {
            LLVMValueRef llvm_lhs =
                pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.left);
            assert(llvm_lhs);
            const PP_AST_Decoration* lhs_decoration = pp_ast_get_decoration(node->binary_expression.left);
            assert(lhs_decoration);
            LLVMTypeRef llvm_lhs_type = pp_environment_backend_lookup_type(cg, lhs_decoration->resolved_type);
            assert(llvm_lhs_type);
            /* Expect i1 */
            LLVMValueRef cur_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));
            assert(cur_func);
            LLVMBasicBlockRef rhs_block =
                LLVMAppendBasicBlockInContext(cg->ctx, cur_func,
                                              node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND ? "and.rhs"
                                                                                                      : "or.rhs");
            LLVMBasicBlockRef merge_block =
                LLVMAppendBasicBlockInContext(cg->ctx, cur_func,
                                              node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND ? "and.end"
                                                                                                      : "or.end");
            LLVMBasicBlockRef lhs_end = LLVMGetInsertBlock(cg->builder);
            if (node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND) {
                /* a && b : if a true go to rhs else merge false */
                LLVMBuildCondBr(cg->builder, llvm_lhs, rhs_block, merge_block);
            } else {
                /* a || b : if a true go to merge else rhs */
                LLVMBuildCondBr(cg->builder, llvm_lhs, merge_block, rhs_block);
            }
            LLVMPositionBuilderAtEnd(cg->builder, rhs_block);
            LLVMValueRef llvm_rhs =
                pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.right);
            assert(llvm_rhs);
            LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(cg->builder);
            pp_cg_ensure_terminated(cg, merge_block);
            LLVMPositionBuilderAtEnd(cg->builder, merge_block);
            LLVMValueRef phi =
                LLVMBuildPhi(cg->builder, LLVMInt1TypeInContext(cg->ctx),
                             node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND ? "and.phi" : "or.phi");
            LLVMValueRef incoming_vals[2];
            LLVMBasicBlockRef incoming_blocks[2] = {lhs_end, rhs_end};
            if (node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND) {
                incoming_vals[0] = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 0, 0);
                incoming_vals[1] = llvm_rhs;
            } else {
                incoming_vals[0] = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 1, 0);
                incoming_vals[1] = llvm_rhs;
            }
            LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
            llvm_val = phi;
            return llvm_val;
        }
    }

    /* ---- Eager evaluation for non-short-circuit operators ---- */

    LLVMValueRef llvm_lhs = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.left);
    assert(llvm_lhs);

    LLVMValueRef llvm_rhs = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.right);
    assert(llvm_rhs);

    /* ---- Type information ---- */

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

        case PP_TT_AMPERSAND_AMPERSAND:
        case PP_TT_AMPERSAND:
            if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildAnd(cg->builder, llvm_lhs, llvm_rhs, "andtmp");
            }
            break;
        case PP_TT_BAR_BAR:
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
            assert(0 && "Unhandled binary operator");
            return NULL;
    }

    if (llvm_val == NULL) { return NULL; }
    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_unary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                            PP_Environment_Backend* env_b, const PP_AST_Node* node) {
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
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_CastExpressionNode);

    LLVMValueRef llvm_val = NULL;

    const PP_AST_Node* expr_node = node->cast_expression.expression;
    assert(expr_node);

    LLVMValueRef llvm_val_to_cast = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, expr_node);
    assert(llvm_val_to_cast);

    const PP_AST_Decoration* expr_decoration = pp_ast_get_decoration(expr_node);
    assert(expr_decoration);
    const PP_AST_Decoration* cast_decoration = pp_ast_get_decoration(node);
    assert(cast_decoration);

    LLVMTypeRef llvm_src_type = pp_environment_backend_lookup_type(cg, expr_decoration->resolved_type);
    assert(llvm_src_type);
    LLVMTypeRef llvm_dest_type = pp_environment_backend_lookup_type(cg, cast_decoration->resolved_type);
    assert(llvm_dest_type);

    if (llvm_src_type == llvm_dest_type) {
        llvm_val = llvm_val_to_cast;
    } else {
        LLVMTypeKind src_kind = LLVMGetTypeKind(llvm_src_type);
        LLVMTypeKind dest_kind = LLVMGetTypeKind(llvm_dest_type);

        if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMFloatTypeKind) {
            llvm_val = LLVMBuildSIToFP(cg->builder, llvm_val_to_cast, llvm_dest_type, "sitofp_tmp");
        } else if (src_kind == LLVMFloatTypeKind && dest_kind == LLVMIntegerTypeKind) {
            llvm_val = LLVMBuildFPToSI(cg->builder, llvm_val_to_cast, llvm_dest_type, "fptosi_tmp");
        } else {
            abort();
        }
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                              PP_Environment_Backend* env_b, const PP_AST_Node* node) {
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
                                                                  const PP_AST_Node* node) {
    LLVMValueRef* field_vals = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_StructInitializerNode);

    const PP_AST_Decoration* decor = pp_ast_get_decoration(node);
    assert(decor);
    const PPType* struct_type = decor->resolved_type;
    assert(struct_type);
    assert(struct_type->kind == PP_TK_Struct);

    const PP_StructLayout* layout = struct_type->type.pp_type_struct.layout;
    assert(layout);

    LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, struct_type);
    assert(llvm_struct_type);

    size_t field_count = layout->field_count;
    if (field_count > 0) {
        field_vals = (LLVMValueRef*)calloc(field_count, sizeof(LLVMValueRef));
        NULL_POINTER_ERROR_HANDLE(field_vals);
    }

    for (unsigned int j = 0; j < node->struct_initializer.entry_count; j++) {
        const PP_AST_StructInitEntry* entry = &node->struct_initializer.entries[j];
        const char* member_name = entry->member_identifier->identifier.identifier;

        int field_idx = -1;
        for (size_t i = 0; i < field_count; i++) {
            if (layout->fields[i].field_name == member_name) {
                field_idx = (int)i;
                break;
            }
        }

        if (field_idx < 0) { abort(); }

        const PPStructField* field = &layout->fields[field_idx];
        LLVMValueRef val = NULL;
        if (pp_type_is_reference(field->field_type)) {
            val = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, entry->member_value);
        } else {
            val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, entry->member_value);
        }
        assert(val);
        field_vals[field_idx] = val;
    }

    for (size_t i = 0; i < field_count; i++) {
        if (field_vals[i] != NULL) continue;

        const PPStructField* field = &layout->fields[i];
        LLVMTypeRef llvm_field_type = pp_environment_backend_lookup_type(cg, field->field_type);
        assert(llvm_field_type);

        LLVMValueRef val = NULL;
        if (field->default_initializer != NULL) {
            val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, field->default_initializer);
        } else {
            val = LLVMConstNull(llvm_field_type);
        }
        assert(val);
        field_vals[i] = val;
    }

    LLVMValueRef result = NULL;
    if (!cg->current_function_type) {
        result = LLVMConstNamedStruct(llvm_struct_type, field_vals, (unsigned)field_count);
    } else {
        result = LLVMGetUndef(llvm_struct_type);
        for (size_t i = 0; i < field_count; i++) {
            result = LLVMBuildInsertValue(cg->builder, result, field_vals[i], i, "struct_init_field");
        }
    }
    NULL_POINTER_ERROR_HANDLE(result);

    free(field_vals);
    return result;

error_cleanup:
    if (field_vals) free(field_vals);
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_collection_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                      PP_Environment_Backend* env_b,
                                                                      const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);

    const PP_AST_Decoration* decor = pp_ast_get_decoration(node);
    assert(decor);
    const PPType* coll_type = decor->resolved_type;
    assert(coll_type);

    if (coll_type->kind == PP_TK_Array) {
        LLVMTypeRef llvm_array_type = pp_environment_backend_lookup_type(cg, coll_type);
        assert(llvm_array_type);

        if (!cg->current_function_type) {
            LLVMValueRef* const_vals =
                (LLVMValueRef*)calloc(node->collection_initializer.element_count, sizeof(LLVMValueRef));
            if (!const_vals) return NULL;
            for (unsigned int i = 0; i < node->collection_initializer.element_count; i++) {
                const_vals[i] = pp_code_generator_generate_rvalue(cg, symbol_table, env_b,
                                                                  node->collection_initializer.elements[i]);
                if (!const_vals[i]) {
                    free(const_vals);
                    return NULL;
                }
            }
            LLVMValueRef result =
                LLVMConstArray(llvm_array_type, const_vals, node->collection_initializer.element_count);
            free(const_vals);
            return result;
        } else {
            LLVMValueRef undef_val = LLVMGetUndef(llvm_array_type);
            LLVMValueRef result = undef_val;
            for (unsigned int i = 0; i < node->collection_initializer.element_count; i++) {
                LLVMValueRef elem_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b,
                                                                          node->collection_initializer.elements[i]);
                if (!elem_val) return NULL;
                result = LLVMBuildInsertValue(cg->builder, result, elem_val, i, "array_init_elem");
            }
            return result;
        }
    } else if (coll_type->kind == PP_TK_List) {
#warning "List initializer is runtime-only; globals not supported yet"
        if (!cg->current_function_type) {
            assert(0 && "List initializer not supported at global scope");
            return NULL;
        }
        const PPType* elem_type = coll_type->type.pp_type_list.element_type;
        assert(elem_type);
        LLVMTypeRef llvm_elem_type = pp_backend_lower_type(cg, elem_type);
        assert(llvm_elem_type);
        unsigned int n = node->collection_initializer.element_count;

        // Runtime functions are ensured at cg creation
        LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
        LLVMTypeRef i8ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
        LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
        assert(malloc_fn);
        LLVMTypeRef malloc_param = i64_type;
        LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr_type, &malloc_param, 1, 0);

        // Compute size = n * sizeof(elem)
        const char* data_layout_str = LLVMGetDataLayout(cg->mod);
        LLVMTargetDataRef target_data = LLVMCreateTargetData(data_layout_str);
        unsigned elem_size = LLVMABISizeOfType(target_data, llvm_elem_type);
        LLVMDisposeTargetData(target_data);
        LLVMValueRef elem_size_val = LLVMConstInt(i64_type, elem_size, 0);
        LLVMValueRef n_val = LLVMConstInt(i64_type, n, 0);
        LLVMValueRef size_val = LLVMBuildMul(cg->builder, elem_size_val, n_val, "list_alloc_size");
        LLVMValueRef raw_ptr = LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &size_val, 1, "list_raw");
        LLVMValueRef buf = LLVMBuildBitCast(cg->builder, raw_ptr, LLVMPointerTypeInContext(cg->ctx, 0), "list_buf");
        // Actually need pointer to elem type
        LLVMTypeRef elem_ptr_type = LLVMPointerType(llvm_elem_type, 0);
        buf = LLVMBuildBitCast(cg->builder, buf, elem_ptr_type, "list_buf_cast");

        for (unsigned int i = 0; i < n; i++) {
            LLVMValueRef elem_val =
                pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->collection_initializer.elements[i]);
            assert(elem_val);
            LLVMValueRef idx = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), i, 0);
            LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, llvm_elem_type, buf, &idx, 1, "list_elem_ptr");
            LLVMBuildStore(cg->builder, elem_val, elem_ptr);
        }
        LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, coll_type);
        assert(llvm_list_type);
        LLVMValueRef list_val = LLVMGetUndef(llvm_list_type);
        list_val = LLVMBuildInsertValue(cg->builder, list_val, buf, 0, "list_data");
        LLVMValueRef len_val = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), n, 0);
        list_val = LLVMBuildInsertValue(cg->builder, list_val, len_val, 1, "list_len");
        list_val = LLVMBuildInsertValue(cg->builder, list_val, len_val, 2, "list_cap");
        return list_val;
    }

    assert(0 && "Unsupported collection type");
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
    assert(node->node_type == PP_AST_MemberAccessNode);

    const PP_AST_Decoration* dec = pp_ast_get_decoration(node);
    assert(dec);

    const PP_AST_Node* obj_node = node->member_access.object;
    const PP_AST_Decoration* obj_dec = pp_ast_get_decoration(obj_node);
    assert(obj_dec);

    if (obj_dec->value_category == PP_VALUE_CATEGORY_LVALUE) {
        LLVMValueRef llvm_lval = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node);
        assert(llvm_lval);
        LLVMValueRef llvm_val = pp_code_generator_l_value_to_r_value(cg, dec->resolved_type, llvm_lval);
        assert(llvm_val);
        return llvm_val;
    }

    // Rvalue base: extract from aggregate
    LLVMValueRef base_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, obj_node);
    assert(base_val);
    const PPType* base_type = pp_type_decay_cg(cg->gtt, obj_dec->resolved_type);
    assert(base_type && base_type->kind == PP_TK_Struct);
    const PP_StructLayout* layout = base_type->type.pp_type_struct.layout;
    assert(layout);
    const char* member_name = node->member_access.member;
    assert(member_name);
    int field_idx = -1;
    for (size_t i = 0; i < layout->field_count; i++) {
        if (layout->fields[i].field_name == member_name) {
            field_idx = (int)i;
            break;
        }
    }
    assert(field_idx >= 0);
    LLVMValueRef extracted = LLVMBuildExtractValue(cg->builder, base_val, (unsigned)field_idx, "struct_elt");
    assert(extracted);
    return extracted;

error_cleanup:
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue_function_call(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                    PP_Environment_Backend* env_b,
                                                                    const PP_AST_Node* node) {
    LLVMValueRef llvm_val = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_FunctionCallNode);

    const PP_AST_Decoration* call_decor = pp_ast_get_decoration(node);
    assert(call_decor);
    assert(call_decor->resolved_type);

    LLVMValueRef llvm_func_call = pp_code_generator_generate_lvalue_function_call(cg, symbol_table, env_b, node);
    assert(llvm_func_call);

    const PPType* return_type = call_decor->resolved_type;
    if (pp_type_is_reference(return_type)) {
        llvm_val = pp_code_generator_l_value_to_r_value(cg, return_type, llvm_func_call);
    } else {
        llvm_val = llvm_func_call;
    }

    return llvm_val;
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
