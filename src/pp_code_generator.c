#include "pp_code_generator.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DWARF base type encodings (DW_ATE_*) are not exported by the LLVM C API. */
typedef enum {
    PP_DW_ATE_ADDR = 0x01,
    PP_DW_ATE_BOOLEAN = 0x02,
    PP_DW_ATE_COMPLEX_FLOAT = 0x03,
    PP_DW_ATE_FLOAT = 0x04,
    PP_DW_ATE_SIGNED = 0x05,
    PP_DW_ATE_SIGNED_CHAR = 0x06,
    PP_DW_ATE_UNSIGNED = 0x07,
    PP_DW_ATE_UNSIGNED_CHAR = 0x08,
    PP_DW_ATE_IMAGINARY_FLOAT = 0x09,
    PP_DW_ATE_PACKED_DECIMAL = 0x0a,
    PP_DW_ATE_NUMERIC_STRING = 0x0b,
    PP_DW_ATE_EDITED = 0x0c,
    PP_DW_ATE_SIGNED_FIXED = 0x0d,
    PP_DW_ATE_UNSIGNED_FIXED = 0x0e,
    PP_DW_ATE_DECIMAL_FLOAT = 0x0f,
    PP_DW_ATE_UTF = 0x10,
    PP_DW_ATE_UCS = 0x11,
    PP_DW_ATE_ASCII = 0x12,
    PP_DW_ATE_LO_USER = 0x80,
    PP_DW_ATE_HI_USER = 0xff,
} PP_DWARF_BaseTypeEncoding;

static PP_Environment_Backend* pp_environment_backend_enter_scope(const PP_Environment_Backend* current_env_b);
static void pp_code_generator_ensure_terminated(PP_CodeGenerator* cg, LLVMBasicBlockRef merge_block);
static void pp_code_generator_emit_memcpy(PP_CodeGenerator* cg, LLVMValueRef dst, LLVMValueRef src, LLVMValueRef size,
                                          const char* name);
static void pp_code_generator_emit_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type);
static void pp_code_generator_emit_copy(PP_CodeGenerator* cg, LLVMValueRef dst, LLVMValueRef src, const PPType* type);
static void pp_code_generator_emit_scope_drops(PP_CodeGenerator* cg);
static void pp_code_generator_emit_drops_since(PP_CodeGenerator* cg, size_t mark);
static void pp_code_generator_push_temp_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type);
static void pp_code_generator_flush_temp_drops(PP_CodeGenerator* cg);
static LLVMValueRef pp_code_generator_generate_rvalue_disposable(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node);
static void pp_code_generator_push_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type);
static int pp_environment_backend_push_val_map(PP_Environment_Backend* env_b, const PPSymbol* symbol,
                                               LLVMValueRef llvmval);
static int pp_environment_backend_push_type_map(PP_Environment_Backend* env_b, const PPType* type,
                                                LLVMTypeRef llvmtype);
static const LLVMValueRef pp_environment_backend_lookup_val(const PP_Environment_Backend* env_b, const PPSymbol* symbol,
                                                            bool cascade);
static const LLVMTypeRef pp_environment_backend_lookup_type(const PP_CodeGenerator* cg, const PPType* type);
static void pp_debug_print_type_inner(const PPType* t);
static LLVMTypeRef pp_backend_lower_type(PP_CodeGenerator* cg, const PPType* type);
static void pp_code_generator_debug_set_location(PP_CodeGenerator* cg, const PP_SourceLoc* loc);
static int pp_code_generator_push_loop_context(PP_CodeGenerator* cg, LLVMBasicBlockRef continue_bb,
                                               LLVMBasicBlockRef exit_bb, size_t drop_stack_mark);
static void pp_code_generator_pop_loop_context(PP_CodeGenerator* cg);
static PP_LoopContext* pp_code_generator_get_current_loop_context(PP_CodeGenerator* cg);
static int pp_code_generator_generate_break(PP_CodeGenerator* cg, const PP_AST_Node* node);
static int pp_code_generator_generate_continue(PP_CodeGenerator* cg, const PP_AST_Node* node);
static LLVMMetadataRef pp_code_generator_di_basic_type(PP_CodeGenerator* cg, const char* name, uint64_t size_in_bits,
                                                       LLVMDWARFTypeEncoding encoding);
static LLVMMetadataRef pp_code_generator_di_void_type(PP_CodeGenerator* cg);
static void pp_code_generator_di_function_signature(const PPType* func_type, const PPType** out_return_type,
                                                    const PPType*** out_param_types, size_t* out_param_count);
static LLVMMetadataRef pp_code_generator_di_subroutine_type(PP_CodeGenerator* cg, const PPType* func_type);
static LLVMMetadataRef pp_code_generator_get_di_type(PP_CodeGenerator* cg, const PPType* t);
static int pp_code_generator_fill_type_map_primitive_opaque_struct(PP_CodeGenerator* cg);
static int pp_code_generator_fill_type_map_complex(PP_CodeGenerator* cg);
static uint64_t pp_code_generator_type_store_size(PP_CodeGenerator* cg, LLVMTypeRef type);
static LLVMValueRef pp_code_generator_entry_alloca(PP_CodeGenerator* cg, LLVMTypeRef type, const char* name);
static int pp_code_generator_emit_internal(LLVMModuleRef module, const char* output_filename,
                                           LLVMMemoryBufferRef* out_buf, int to_assembly, LLVMTargetMachineRef tm_in);
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
static LLVMTypeRef pp_code_generator_llvm_subroutine_type(PP_CodeGenerator* cg, const PPType* callable_type);
static LLVMValueRef pp_code_generator_generate_lvalue_array_list_access(PP_CodeGenerator* cg,
                                                                        PPSymbolTable* symbol_table,
                                                                        PP_Environment_Backend* env_b,
                                                                        const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_array_list_access(PP_CodeGenerator* cg,
                                                                        PPSymbolTable* symbol_table,
                                                                        PP_Environment_Backend* env_b,
                                                                        const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_lvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_l_value_to_r_value(PP_CodeGenerator* cg, const PPType* type, LLVMValueRef lvalue);
static LLVMValueRef pp_code_generator_generate_lvalue_identifier(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_lvalue_struct_member_access(PP_CodeGenerator* cg,
                                                                           PPSymbolTable* symbol_table,
                                                                           PP_Environment_Backend* env_b,
                                                                           const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_lvalue_function_call(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                    PP_Environment_Backend* env_b,
                                                                    const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_binary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                             PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_unary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                            PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_cast(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                           PP_Environment_Backend* env_b, const PP_AST_Node* node);
static ssize_t pp_codegen_unescape_lexeme_to_buf(const char* lexeme, unsigned char* buf, size_t bufsize);
static LLVMValueRef pp_codegen_static_string_literal_to_llvm_constant(LLVMContextRef ctx, const char* lexeme);
static LLVMValueRef pp_codegen_string_literal_to_llvm_value(PP_CodeGenerator* cg, const char* lexeme);
static LLVMValueRef pp_code_generator_generate_rvalue_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                              PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_qualified_constant(PP_CodeGenerator* cg, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_null_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                   PP_Environment_Backend* env_b,
                                                                   const PP_AST_Node* node);
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
static PP_TypeCacheEntry* pp_code_generator_find_cache_entry(PP_CodeGenerator* cg, const PPType* type);
static PP_TypeCacheEntry* pp_code_generator_get_or_create_cache_entry(PP_CodeGenerator* cg, const PPType* type);
static void pp_code_generator_gen_copy_body(PP_CodeGenerator* cg, LLVMValueRef func, const PPType* type);
static void pp_code_generator_gen_drop_body(PP_CodeGenerator* cg, LLVMValueRef func, const PPType* type);
static LLVMBasicBlockRef pp_code_generator_save_builder_insert_block(PP_CodeGenerator* cg);
static void pp_code_generator_restore_builder_insert_block(PP_CodeGenerator* cg, LLVMBasicBlockRef block);
static LLVMValueRef pp_code_generator_ensure_copy_fn(PP_CodeGenerator* cg, PP_TypeCacheEntry* entry);
static LLVMValueRef pp_code_generator_ensure_drop_fn(PP_CodeGenerator* cg, PP_TypeCacheEntry* entry);
static LLVMValueRef pp_code_generator_generate_rvalue_list_push(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_list_pop(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                               PP_Environment_Backend* env_b, const PP_AST_Node* node);
static LLVMValueRef pp_code_generator_generate_rvalue_list_clear(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node);

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

static void pp_code_generator_ensure_terminated(PP_CodeGenerator* cg, LLVMBasicBlockRef merge_block) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(cg->builder);
    if (cur && LLVMGetBasicBlockTerminator(cur) == NULL) { LLVMBuildBr(cg->builder, merge_block); }
}

static void pp_code_generator_emit_memcpy(PP_CodeGenerator* cg, LLVMValueRef dst, LLVMValueRef src, LLVMValueRef size,
                                          const char* name) {
    if (!cg || !dst || !src || !size) return;
    // Use LLVM's built-in memcpy builder which emits llvm.memcpy intrinsic with correct signature
    // Alignment of 1 is safe default; can be improved with target data if desired
    LLVMBuildMemCpy(cg->builder, dst, 1, src, 1, size);
}

static void pp_code_generator_emit_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type) {
    if (cg == NULL || address == NULL || type == NULL) return;
    if (pp_type_is_reference(type)) return;

    // Unwrap const and newtype transparently
    if (type->kind == PP_TK_Const) {
        pp_code_generator_emit_drop(cg, address, type->type.pp_type_const.const_of);
        return;
    }
    // A plain newtype is transparent, but a handle is not: it owns a resource and drops through its bound
    // function, which gen_drop_body emits.
    if (type->kind == PP_TK_Newtype && !pp_type_is_handle(type)) {
        pp_code_generator_emit_drop(cg, address, type->type.pp_type_newtype.underlying);
        return;
    }

    PP_TypeCacheEntry* entry = pp_code_generator_get_or_create_cache_entry(cg, type);
    LLVMValueRef drop_fn = pp_code_generator_ensure_drop_fn(cg, entry);
    if (drop_fn) {
        LLVMValueRef args[1] = {address};
        LLVMBuildCall2(cg->builder,
                       LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                        (LLVMTypeRef[1]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)}, 1, 0),
                       drop_fn, args, 1, "");
        return;
    }

    switch (type->kind) {
        case PP_TK_List: {
            const PPType* elem_type = type->type.pp_type_list.element_type;
            LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_list_type) return;
            // Get pointers to fields
            LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, address, 0, "list_data_ptr");
            LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, address, 1, "list_len_ptr");
            LLVMValueRef data_ptr =
                LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data");
            LLVMValueRef len = LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), len_ptr, "list_len");
            // If data is null, nothing to drop
            LLVMValueRef is_null =
                LLVMBuildICmp(cg->builder, LLVMIntEQ, data_ptr, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)),
                              "list_data_is_null");
            LLVMBasicBlockRef then_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "list_drop_null");
            LLVMBasicBlockRef else_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "list_drop_nonnull");
            LLVMBasicBlockRef merge_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "list_drop_merge");
            LLVMBuildCondBr(cg->builder, is_null, then_bb, else_bb);
            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            // If element needs drop, drop each element
            if (pp_type_needs_drop(elem_type)) {
                // Loop over elements
                LLVMBasicBlockRef loop_cond_bb =
                    LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                                  "list_drop_loop_cond");
                LLVMBasicBlockRef loop_body_bb =
                    LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                                  "list_drop_loop_body");
                LLVMBasicBlockRef loop_after_bb =
                    LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                                  "list_drop_loop_after");
                // i = 0
                LLVMValueRef i_alloca =
                    pp_code_generator_entry_alloca(cg, LLVMInt64TypeInContext(cg->ctx), "list_drop_i");
                LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), i_alloca);
                LLVMBuildBr(cg->builder, loop_cond_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
                LLVMValueRef i_val =
                    LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), i_alloca, "list_drop_i_val");
                LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, len, "list_drop_loop_cond");
                LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
                // elem_ptr = data_ptr + i
                // `data_ptr` is the array base, so the GEP element type is the element type itself
                // (using a pointer-to-element here would advance by sizeof(ptr) instead of sizeof(elem)).
                LLVMTypeRef elem_llvm_ty = pp_environment_backend_lookup_type(cg, elem_type);
                LLVMValueRef elem_ptr =
                    LLVMBuildGEP2(cg->builder, elem_llvm_ty, data_ptr, &i_val, 1, "list_drop_elem_ptr");
                // Drop element
                pp_code_generator_emit_drop(cg, elem_ptr, elem_type);
                // i++
                LLVMValueRef i_next =
                    LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 1, 0),
                                 "list_drop_i_next");
                LLVMBuildStore(cg->builder, i_next, i_alloca);
                LLVMBuildBr(cg->builder, loop_cond_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
            }
            // Free data buffer
            LLVMValueRef data_ptr_i8 =
                LLVMBuildBitCast(cg->builder, data_ptr, LLVMPointerTypeInContext(cg->ctx, 0), "list_data_i8");
            LLVMValueRef free_fn = LLVMGetNamedFunction(cg->mod, "free");
            assert(free_fn);
            {
                LLVMValueRef args[1] = {data_ptr_i8};
                LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                         (LLVMTypeRef[]){LLVMPointerTypeInContext(cg->ctx, 0)}, 1, 0);
                LLVMBuildCall2(cg->builder, free_type, free_fn, args, 1, "");
            }
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
        } break;
        case PP_TK_Struct: {
            const PP_StructLayout* layout = type->type.pp_type_struct.layout;
            if (!layout) return;
            LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_struct_type) return;
            for (size_t i = 0; i < layout->field_count; i++) {
                const PPType* field_type = layout->fields[i].field_type;
                if (!pp_type_needs_drop(field_type)) continue;
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(cg->builder, llvm_struct_type, address, i, "");
                pp_code_generator_emit_drop(cg, field_ptr, field_type);
            }
        } break;
        case PP_TK_Array: {
            const PPType* elem_type = type->type.pp_type_array.element_type;
            if (!pp_type_needs_drop(elem_type)) break;
            size_t arr_size = type->type.pp_type_array.size;
            LLVMTypeRef llvm_array_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_array_type) break;
            for (size_t i = 0; i < arr_size; i++) {
                LLVMValueRef idx = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), i, 0);
                LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, llvm_array_type, address, &idx, 1, "array_elem_ptr");
                pp_code_generator_emit_drop(cg, elem_ptr, elem_type);
            }
        } break;
        case PP_TK_String: {
            LLVMTypeRef llvm_str_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_str_type) return;
            LLVMValueRef ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, address, 0, "str_ptr_ptr");
            LLVMValueRef ptr = LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), ptr_ptr, "str_ptr");
            LLVMValueRef is_null =
                LLVMBuildICmp(cg->builder, LLVMIntNE, ptr, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)),
                              "str_ptr_not_null");
            LLVMBasicBlockRef then_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "str_drop_then");
            LLVMBasicBlockRef else_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "str_drop_else");
            LLVMBasicBlockRef merge_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "str_drop_merge");
            LLVMBuildCondBr(cg->builder, is_null, then_bb, else_bb);
            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            LLVMValueRef free_fn = LLVMGetNamedFunction(cg->mod, "free");
            assert(free_fn);
            {
                LLVMValueRef args[1] = {ptr};
                LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                         (LLVMTypeRef[]){LLVMPointerTypeInContext(cg->ctx, 0)}, 1, 0);
                LLVMBuildCall2(cg->builder, free_type, free_fn, args, 1, "");
            }
            // Null out fields to avoid double free
            LLVMBuildStore(cg->builder, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), ptr_ptr);
            LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, address, 1, "str_len_ptr");
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), len_ptr);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
        } break;
        default:
            return;
    }
}

static void pp_code_generator_emit_copy(PP_CodeGenerator* cg, LLVMValueRef dst, LLVMValueRef src, const PPType* type) {
    if (cg == NULL || dst == NULL || src == NULL || type == NULL) return;
    if (pp_type_is_reference(type)) return;

    // Unwrap const and newtype
    if (type->kind == PP_TK_Const) {
        pp_code_generator_emit_copy(cg, dst, src, type->type.pp_type_const.const_of);
        return;
    }
    if (type->kind == PP_TK_Newtype) {
        pp_code_generator_emit_copy(cg, dst, src, type->type.pp_type_newtype.underlying);
        return;
    }

    PP_TypeCacheEntry* entry = pp_code_generator_get_or_create_cache_entry(cg, type);
    LLVMValueRef copy_fn = pp_code_generator_ensure_copy_fn(cg, entry);
    if (copy_fn) {
        LLVMValueRef args[2] = {dst, src};
        LLVMBuildCall2(cg->builder,
                       LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                        (LLVMTypeRef[2]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                                         LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)},
                                        2, 0),
                       copy_fn, args, 2, "");
        return;
    }

    if (pp_type_is_trivially_copyable(type)) {
        // Bitwise copy for trivial types
        LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, type);
        assert(llvm_type);
        // For now emit a store/load for trivial scalars
        LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, llvm_type, src, "copy_src");
        LLVMBuildStore(cg->builder, loaded, dst);
        return;
    }

    // Non-trivial types: dispatch per kind
    switch (type->kind) {
        case PP_TK_List: {
            const PPType* elem_type = type->type.pp_type_list.element_type;
            LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_list_type) return;
            // Get src fields pointers
            LLVMValueRef src_data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, src, 0, "src_data_ptr");
            LLVMValueRef src_len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, src, 1, "src_len");
            LLVMValueRef src_cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, src, 2, "src_cap");
            LLVMValueRef dst_data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, dst, 0, "dst_data_ptr");
            LLVMValueRef dst_len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, dst, 1, "dst_len");
            LLVMValueRef dst_cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, dst, 2, "dst_cap");

            LLVMValueRef src_data =
                LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), src_data_ptr_ptr, "src_data");
            LLVMValueRef src_len =
                LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), src_len_ptr, "src_len_val");
            LLVMValueRef src_cap =
                LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), src_cap_ptr, "src_cap_val");

            // If src is empty, set dst empty
            LLVMValueRef is_empty = LLVMBuildICmp(cg->builder, LLVMIntEQ, src_len,
                                                  LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), "list_is_empty");
            // Zero init dst
            LLVMValueRef null_ptr = LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0));
            // Branch
            LLVMBasicBlockRef then_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "list_copy_empty");
            LLVMBasicBlockRef else_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "list_copy_nonempty");
            LLVMBasicBlockRef merge_bb =
                LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                              "list_copy_merge");
            LLVMBuildCondBr(cg->builder, is_empty, then_bb, else_bb);

            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            LLVMBuildStore(cg->builder, null_ptr, dst_data_ptr_ptr);
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), dst_len_ptr);
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), dst_cap_ptr);
            LLVMBuildBr(cg->builder, merge_bb);

            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            // Compute size
            LLVMTypeRef elem_llvm_type = pp_environment_backend_lookup_type(cg, elem_type);
            LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef i8ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
            LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr_type, &i64_type, 1, 0);
            // Allocate new buffer
            const char* data_layout_str = LLVMGetDataLayout(cg->mod);
            LLVMTargetDataRef target_data = LLVMCreateTargetData(data_layout_str);
            unsigned elem_size = LLVMABISizeOfType(target_data, elem_llvm_type);
            LLVMDisposeTargetData(target_data);
            LLVMValueRef elem_size_val = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), elem_size, 0);
            LLVMValueRef bytes = LLVMBuildMul(cg->builder, src_len, elem_size_val, "list_copy_bytes");
            LLVMValueRef new_data_raw = LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &bytes, 1, "list_new_data");
            LLVMValueRef new_data =
                LLVMBuildBitCast(cg->builder, new_data_raw, LLVMPointerType(elem_llvm_type, 0), "list_new_data_cast");
            LLVMBuildStore(cg->builder, new_data, dst_data_ptr_ptr);
            LLVMBuildStore(cg->builder, src_len, dst_len_ptr);
            LLVMBuildStore(cg->builder, src_cap, dst_cap_ptr);
            // Copy elements: fast memcpy for trivial, element-wise copy otherwise
            if (pp_type_is_trivially_copyable(elem_type)) {
                LLVMValueRef src_data_i8 = LLVMBuildBitCast(cg->builder, src_data, i8ptr_type, "src_data_i8");
                LLVMValueRef dst_data_i8 = LLVMBuildBitCast(cg->builder, new_data, i8ptr_type, "dst_data_i8");
                pp_code_generator_emit_memcpy(cg, dst_data_i8, src_data_i8, bytes, "list_memcpy");
            } else {
                // Element-wise copy
                LLVMBasicBlockRef loop_cond_bb =
                    LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                                  "list_copy_loop_cond");
                LLVMBasicBlockRef loop_body_bb =
                    LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                                  "list_copy_loop_body");
                LLVMBasicBlockRef loop_after_bb =
                    LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                                  "list_copy_loop_after");
                LLVMValueRef i_alloca =
                    pp_code_generator_entry_alloca(cg, LLVMInt64TypeInContext(cg->ctx), "list_copy_i");
                LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), i_alloca);
                LLVMBuildBr(cg->builder, loop_cond_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
                LLVMValueRef i_val =
                    LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), i_alloca, "list_copy_i_val");
                LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, src_len, "list_copy_loop_cond");
                LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
                LLVMValueRef src_elem_ptr =
                    LLVMBuildGEP2(cg->builder, elem_llvm_type, src_data, &i_val, 1, "list_copy_src_elem");
                LLVMValueRef dst_elem_ptr =
                    LLVMBuildGEP2(cg->builder, elem_llvm_type, new_data, &i_val, 1, "list_copy_dst_elem");
                pp_code_generator_emit_copy(cg, dst_elem_ptr, src_elem_ptr, elem_type);
                LLVMValueRef i_next =
                    LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 1, 0),
                                 "list_copy_i_next");
                LLVMBuildStore(cg->builder, i_next, i_alloca);
                LLVMBuildBr(cg->builder, loop_cond_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
            }
            LLVMBuildBr(cg->builder, merge_bb);

            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
            break;
        }
        case PP_TK_Struct: {
            const PP_StructLayout* layout = type->type.pp_type_struct.layout;
            if (!layout) return;
            LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_struct_type) return;
            for (size_t i = 0; i < layout->field_count; i++) {
                const PPType* field_type = layout->fields[i].field_type;
                LLVMValueRef field_dst = LLVMBuildStructGEP2(cg->builder, llvm_struct_type, dst, i, "");
                LLVMValueRef field_src = LLVMBuildStructGEP2(cg->builder, llvm_struct_type, src, i, "");
                pp_code_generator_emit_copy(cg, field_dst, field_src, field_type);
            }
            break;
        }
        case PP_TK_Array: {
            const PPType* elem_type = type->type.pp_type_array.element_type;
            size_t arr_size = type->type.pp_type_array.size;
            LLVMTypeRef llvm_array_type = pp_environment_backend_lookup_type(cg, type);
            if (!llvm_array_type) break;
            for (size_t i = 0; i < arr_size; i++) {
                LLVMValueRef idx = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), i, 0);
                LLVMValueRef elem_dst = LLVMBuildGEP2(cg->builder, llvm_array_type, dst, &idx, 1, "array_elem_dst");
                LLVMValueRef elem_src = LLVMBuildGEP2(cg->builder, llvm_array_type, src, &idx, 1, "array_elem_src");
                pp_code_generator_emit_copy(cg, elem_dst, elem_src, elem_type);
            }
            break;
        }
        default:
            // Fallback bitwise
            {
                LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, type);
                assert(llvm_type);
                LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, llvm_type, src, "copy_src");
                LLVMBuildStore(cg->builder, loaded, dst);
            }
            break;
    }
}

/* Emit drops for every live entry created at or above `mark`, in reverse creation order. The stack
 * is left intact: this runs on one control-flow path (a return, a break/continue, a function
 * fall-through) while sibling paths still need to know what is live. */
static void pp_code_generator_emit_drops_since(PP_CodeGenerator* cg, size_t mark) {
    if (cg == NULL) return;
    for (size_t i = cg->drop_stack_count; i > mark;) {
        i--;
        LLVMValueRef addr = cg->drop_stack_addresses[i];
        const PPType* typ = cg->drop_stack_types[i];
        if (typ && pp_type_needs_drop(typ) && !pp_type_is_reference(typ)) {
            pp_code_generator_emit_drop(cg, addr, typ);
        }
    }
}

static void pp_code_generator_emit_scope_drops(PP_CodeGenerator* cg) { pp_code_generator_emit_drops_since(cg, 0); }

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

/* Register an owning temporary for release at the end of the current statement.
 *
 * This runs on every expression codegen path that hands out an owned value, and a missed registration
 * silently leaks it. Growing the bookkeeping is therefore not a recoverable condition here: report it
 * and abort, rather than returning a status that callers would have to thread through every expression
 * or, worse, hide inside an assert() that disappears under NDEBUG. */
static void pp_code_generator_push_temp_drop(PP_CodeGenerator* cg, LLVMValueRef address, const PPType* type) {
    LLVMValueRef* new_addresses = NULL;
    PPType** new_types = NULL;
    size_t new_capacity = 0;
    size_t i = 0;

    assert(cg != NULL);
    assert(address != NULL);
    assert(type != NULL);
    if (!pp_type_needs_drop(type) || pp_type_is_reference(type)) return;

    if (cg->temp_drop_count >= cg->temp_drop_capacity) {
        new_capacity = cg->temp_drop_capacity ? cg->temp_drop_capacity * 2 : 8;
        new_addresses = (LLVMValueRef*)malloc(new_capacity * sizeof(LLVMValueRef));
        NULL_POINTER_ERROR_HANDLE(new_addresses);
        new_types = (PPType**)malloc(new_capacity * sizeof(PPType*));
        NULL_POINTER_ERROR_HANDLE(new_types);
        for (i = 0; i < cg->temp_drop_count; i++) {
            new_addresses[i] = cg->temp_drop_addresses[i];
            new_types[i] = cg->temp_drop_types[i];
        }
        free(cg->temp_drop_addresses);
        free(cg->temp_drop_types);
        cg->temp_drop_addresses = new_addresses;
        cg->temp_drop_types = new_types;
        cg->temp_drop_capacity = new_capacity;
        /* Ownership moved into cg; clear so error_cleanup cannot free them. */
        new_addresses = NULL;
        new_types = NULL;
    }

    cg->temp_drop_addresses[cg->temp_drop_count] = address;
    cg->temp_drop_types[cg->temp_drop_count] = (PPType*)type;
    cg->temp_drop_count++;
    return;

error_cleanup:
    free(new_addresses);
    free(new_types);
    fprintf(stderr, "PP codegen: out of memory while growing the temporary drop stack\n");
    abort();
}

/* Release the statement's own temporaries. Must run before the block is terminated: after a return,
 * break or continue there is nowhere to put the drop, and those paths flush before branching. */
static void pp_code_generator_flush_temp_drops(PP_CodeGenerator* cg) {
    if (cg == NULL) return;
    LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(cg->builder);
    if (current_bb != NULL && LLVMGetBasicBlockTerminator(current_bb) != NULL) {
        cg->temp_drop_count = 0;
        return;
    }
    for (size_t i = cg->temp_drop_count; i > 0;) {
        i--;
        pp_code_generator_emit_drop(cg, cg->temp_drop_addresses[i], cg->temp_drop_types[i]);
    }
    cg->temp_drop_count = 0;
}

/* Generate a node's value, and if it is an owning r-value whose descriptor the caller only reads,
 * park it in a temporary so it is released at the end of the statement. */
static LLVMValueRef pp_code_generator_generate_rvalue_disposable(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node) {
    LLVMValueRef value = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node);
    assert(value);
    assert(node);
    /* Value-producing expressions are decorated by the semantic pass. Statement-like expression
     * nodes (list_push, list_clear) produce no value, so there is nothing of theirs to release. */
    const PP_AST_Decoration* dec = pp_ast_get_decoration(node);
    if (dec == NULL || dec->resolved_type == NULL) return value;
    const PPType* type = pp_type_decay_cg(cg->gtt, dec->resolved_type);
    assert(type);
    if (dec->value_category == PP_VALUE_CATEGORY_LVALUE) return value;
    if (pp_type_is_reference(type) || pp_type_is_handle(type) || !pp_type_needs_drop(type)) return value;

    LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, type);
    assert(llvm_type);
    LLVMValueRef slot = pp_code_generator_entry_alloca(cg, llvm_type, "owned_temp");
    LLVMBuildStore(cg->builder, value, slot);
    pp_code_generator_push_temp_drop(cg, slot, type);
    return value;
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
    for (size_t i = 0; i < env_b->value_count; i++) {
        if (env_b->value_map[i].symbol == symbol) { return env_b->value_map[i].ref; }
    }
    if (env_b->parent != NULL && cascade) { return pp_environment_backend_lookup_val(env_b->parent, symbol, cascade); }
    return NULL;
}

static const LLVMTypeRef pp_environment_backend_lookup_type(const PP_CodeGenerator* cg, const PPType* type) {
    if (cg == NULL || type == NULL) return NULL;

    PP_Environment_Backend* env_b = cg->global_env_b;
    if (env_b == NULL) return NULL;

    for (size_t i = 0; i < env_b->type_count; i++) {
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
        case PP_TK_Int8:
            return LLVMInt8TypeInContext(cg->ctx);
        case PP_TK_UInt8:
            return LLVMInt8TypeInContext(cg->ctx);
        case PP_TK_Int16:
            return LLVMInt16TypeInContext(cg->ctx);
        case PP_TK_UInt16:
            return LLVMInt16TypeInContext(cg->ctx);
        case PP_TK_Int32:
            return LLVMInt32TypeInContext(cg->ctx);
        case PP_TK_UInt32:
            return LLVMInt32TypeInContext(cg->ctx);
        case PP_TK_Int64:
            return LLVMInt64TypeInContext(cg->ctx);
        case PP_TK_UInt64:
            return LLVMInt64TypeInContext(cg->ctx);
        case PP_TK_Int128:
            return LLVMInt128TypeInContext(cg->ctx);
        case PP_TK_UInt128:
            return LLVMInt128TypeInContext(cg->ctx);
        case PP_TK_Float:
            return LLVMFloatTypeInContext(cg->ctx);
        case PP_TK_String: {
            LLVMTypeRef existing = LLVMGetTypeByName2(cg->ctx, "pp.string");
            if (existing) return existing;
            LLVMTypeRef ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef fields[2] = {ptr_type, i64_type};
            LLVMTypeRef str_struct = LLVMStructCreateNamed(cg->ctx, "pp.string");
            LLVMStructSetBody(str_struct, fields, 2, 0);
            return str_struct;
        }

        case PP_TK_Newtype:
            return pp_backend_lower_type(cg, type->type.pp_type_newtype.underlying);

        case PP_TK_Const:
            return pp_backend_lower_type(cg, type->type.pp_type_const.const_of);

        case PP_TK_Pointer:
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
            LLVMTypeRef field_types[4] = {ptr_type, i64_type, i64_type, i64_type};
            LLVMTypeRef list_struct = LLVMStructCreateNamed(cg->ctx, "pp.list");
            LLVMStructSetBody(list_struct, field_types, 4, 0);
            return list_struct;
        }
        case PP_TK_Function:
        case PP_TK_Lambda:
        case PP_TK_Operator:
            /* A callable VALUE is an opaque code pointer; the signature is kept in
             * the PPType and materialized on demand via
             * pp_code_generator_llvm_subroutine_type. */
            return LLVMPointerTypeInContext(cg->ctx, 0);

        default:
            break;
    }

    return NULL;
error_cleanup:
    return NULL;
}

/* Build the true LLVM subroutine type of a callable signature on demand.
 * Callable VALUES lower to an opaque `ptr`, so this is the only place where the
 * signature becomes an LLVMFunctionType. Parameter and return types are resolved
 * through the type map, so nested callable types correctly appear as `ptr`. */
static LLVMTypeRef pp_code_generator_llvm_subroutine_type(PP_CodeGenerator* cg, const PPType* callable_type) {
    LLVMTypeRef llvm_return_type = NULL;
    LLVMTypeRef* llvm_param_types = NULL;
    LLVMTypeRef llvm_func_type = NULL;
    const PPType* return_type = NULL;
    const PPType* const* param_types = NULL;
    size_t param_count = 0;

    assert(cg);
    assert(callable_type);

    switch (callable_type->kind) {
        case PP_TK_Function:
            return_type = callable_type->type.pp_type_function.return_type;
            param_types = callable_type->type.pp_type_function.param_types;
            param_count = callable_type->type.pp_type_function.param_count;
            break;
        case PP_TK_Operator:
            return_type = callable_type->type.pp_type_operator.return_type;
            param_types = callable_type->type.pp_type_operator.param_types;
            param_count = callable_type->type.pp_type_operator.param_count;
            break;
        case PP_TK_Lambda:
            return_type = callable_type->type.pp_type_lambda.return_type;
            param_types = callable_type->type.pp_type_lambda.param_types;
            param_count = callable_type->type.pp_type_lambda.param_count;
            break;
        default:
            assert(0 && "pp_code_generator_llvm_subroutine_type: not a callable type");
            return NULL;
    }

    assert(return_type);
    llvm_return_type = pp_environment_backend_lookup_type(cg, return_type);
    assert(llvm_return_type);

    if (param_count > 0) {
        llvm_param_types = (LLVMTypeRef*)calloc(param_count, sizeof(LLVMTypeRef));
        NULL_POINTER_ERROR_HANDLE(llvm_param_types);
        for (size_t i = 0; i < param_count; i++) {
            assert(param_types[i]);
            llvm_param_types[i] = pp_environment_backend_lookup_type(cg, param_types[i]);
            assert(llvm_param_types[i]);
        }
    }

    llvm_func_type = LLVMFunctionType(llvm_return_type, llvm_param_types, (unsigned)param_count, 0);
    free(llvm_param_types);
    llvm_param_types = NULL;
    assert(llvm_func_type);
    return llvm_func_type;

error_cleanup:
    free(llvm_param_types);
    return NULL;
}

PP_CodeGenerator* pp_code_generator_create(PP_SemanticAnalyzer* sa, PP_StringPool* sp, bool debug) {
    PP_CodeGenerator* cg = NULL;
    LLVMContextRef ctx = NULL;
    LLVMModuleRef mod = NULL;
    LLVMBuilderRef builder = NULL;
    PP_Environment_Backend* env_b = NULL;
    PP_ErrorList* error_list = NULL;
    LLVMDIBuilderRef di_builder = NULL;
    LLVMMetadataRef di_compile_unit = NULL;
    LLVMMetadataRef di_file = NULL;
    PP_DITypeCacheEntry* di_type_cache = NULL;

    if (sa == NULL) goto error_cleanup;

    cg = (PP_CodeGenerator*)calloc(1, sizeof(PP_CodeGenerator));
    if (cg == NULL) goto error_cleanup;

    ctx = LLVMContextCreate();
    if (ctx == NULL) goto error_cleanup;
    cg->ctx = ctx;
    ctx = NULL;

    mod = LLVMModuleCreateWithNameInContext("input", cg->ctx);
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

    // cg filename removed
    cg->code_length = sa->code_length;

    cg->program = sa->program;
    sa->program = NULL;

    cg->gst = sa->gst;
    sa->gst = NULL;

    cg->gtt = sa->gtt;
    sa->gtt = NULL;

    cg->code = sa->code;
    sa->code = NULL;

    cg->sp = sp;

    cg->debug = debug;

    assert(cg->program && "program node must be present");

    assert(sa->current_function_type == NULL);
    cg->current_function_type = NULL;

    if (debug) {
        // Create DIBuilder
        di_builder = LLVMCreateDIBuilder(cg->mod);
        if (di_builder == NULL) goto error_cleanup;
        cg->di_builder = di_builder;
        di_builder = NULL;

        // Module flags for DWARF
        LLVMValueRef dwarf_version = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 5, 0);
        LLVMValueRef dbg_version = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), LLVMDebugMetadataVersion(), 0);
        LLVMAddModuleFlag(cg->mod, LLVMModuleFlagBehaviorWarning, "Dwarf Version", strlen("Dwarf Version"),
                          LLVMValueAsMetadata(dwarf_version));
        LLVMAddModuleFlag(cg->mod, LLVMModuleFlagBehaviorWarning, "Debug Info Version", strlen("Debug Info Version"),
                          LLVMValueAsMetadata(dbg_version));

        // DIFile and DICompileUnit
        const char* filename = "input";
        if (cg->program && cg->program->loc.filename) { filename = cg->program->loc.filename; }
        const char* dirname = "";
        di_file = LLVMDIBuilderCreateFile(cg->di_builder, filename, strlen(filename), dirname, strlen(dirname));
        if (di_file == NULL) goto error_cleanup;
        cg->di_file = di_file;
        di_file = NULL;

        di_compile_unit = LLVMDIBuilderCreateCompileUnit(cg->di_builder, LLVMDWARFSourceLanguageC99, cg->di_file,
                                                         "pplang", strlen("pplang"), 0, "", 0, 0, "", 0,
                                                         LLVMDWARFEmissionFull, 0, 0, 0, "", 0, "", 0);
        if (di_compile_unit == NULL) goto error_cleanup;
        cg->di_compile_unit = di_compile_unit;
        di_compile_unit = NULL;

        /* Initialize DI type cache */
        cg->di_type_cache_capacity = 16;
        di_type_cache = (PP_DITypeCacheEntry*)calloc(cg->di_type_cache_capacity, sizeof(PP_DITypeCacheEntry));
        if (di_type_cache == NULL) goto error_cleanup;
        cg->di_type_cache = di_type_cache;
        di_type_cache = NULL;
        cg->di_type_cache_count = 0;
    }

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

        // memcmp backs builtin string == / !=. Declared with the C signature: i32 (i8*, i8*, i64).
        LLVMTypeRef memcmp_params[3] = {i8ptr_type, i8ptr_type, i64_type};
        LLVMTypeRef memcmp_type = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), memcmp_params, 3, 0);
        if (!LLVMGetNamedFunction(cg->mod, "memcmp")) LLVMAddFunction(cg->mod, "memcmp", memcmp_type);
    }

    return cg;

error_cleanup:
    if (ctx) LLVMContextDispose(ctx);
    if (mod) LLVMDisposeModule(mod);
    if (builder) LLVMDisposeBuilder(builder);
    pp_environment_backend_free(env_b);
    pp_error_list_free(error_list);
    pp_code_generator_free(cg);
    free(di_type_cache);
    return NULL;
}

void pp_code_generator_free(PP_CodeGenerator* cg) {
    if (cg != NULL) {
        if (cg->debug && cg->di_builder) {
            LLVMDIBuilderFinalize(cg->di_builder);
            LLVMDisposeDIBuilder(cg->di_builder);
        }
        if (cg->builder) LLVMDisposeBuilder(cg->builder);
        if (cg->mod) LLVMDisposeModule(cg->mod);
        if (cg->ctx) LLVMContextDispose(cg->ctx);
        pp_ast_root_free(cg->program);
        pp_environment_backend_free(cg->global_env_b);
        pp_symbol_table_free(cg->gst);
        pp_error_list_free(cg->error_list);
        pp_global_type_table_free(cg->gtt);
        free((char*)cg->code);
        /* String pool is not owned by code generator; do not free */
        free(cg->drop_stack_addresses);
        free(cg->temp_drop_addresses);
        free(cg->temp_drop_types);
        free(cg->drop_stack_types);
        free(cg->type_cache);
        free(cg->di_type_cache);
        free(cg->loop_context_stack);
    }
    free(cg);
    LLVMShutdown();
}

static void pp_code_generator_debug_set_location(PP_CodeGenerator* cg, const PP_SourceLoc* loc) {
    if (!cg->debug || !cg->di_builder || !loc) return;
    unsigned int line = loc->line;
    unsigned int col = loc->column;
    LLVMMetadataRef dwarf_loc = LLVMDIBuilderCreateDebugLocation(cg->ctx, line, col, cg->di_current_scope, NULL);
    if (dwarf_loc) { LLVMSetCurrentDebugLocation2(cg->builder, dwarf_loc); }
}

/* Loop context stack helpers for break/continue */
static int pp_code_generator_push_loop_context(PP_CodeGenerator* cg, LLVMBasicBlockRef continue_bb,
                                               LLVMBasicBlockRef exit_bb, size_t drop_stack_mark) {
    PP_LoopContext* new_stack = NULL;
    size_t new_capacity = 0;
    NULL_POINTER_ERROR_HANDLE(cg);

    if (cg->loop_context_count >= cg->loop_context_capacity) {
        new_capacity = cg->loop_context_capacity ? cg->loop_context_capacity * 2 : 4;
        new_stack = (PP_LoopContext*)realloc(cg->loop_context_stack, new_capacity * sizeof(PP_LoopContext));
        NULL_POINTER_ERROR_HANDLE(new_stack);
        cg->loop_context_stack = new_stack;
        cg->loop_context_capacity = new_capacity;
        /* Ownership moved into cg; clear so error_cleanup cannot free it. */
        new_stack = NULL;
    }

    cg->loop_context_stack[cg->loop_context_count].continue_bb = continue_bb;
    cg->loop_context_stack[cg->loop_context_count].exit_bb = exit_bb;
    cg->loop_context_stack[cg->loop_context_count].drop_stack_mark = drop_stack_mark;
    cg->loop_context_count++;

    return 1;

error_cleanup:
    /* On realloc failure the original buffer stays owned by cg. */
    free(new_stack);
    return 0;
}

static void pp_code_generator_pop_loop_context(PP_CodeGenerator* cg) {
    if (cg->loop_context_count > 0) { cg->loop_context_count--; }
}

static PP_LoopContext* pp_code_generator_get_current_loop_context(PP_CodeGenerator* cg) {
    if (cg->loop_context_count == 0) return NULL;
    return &cg->loop_context_stack[cg->loop_context_count - 1];
}

static int pp_code_generator_generate_break(PP_CodeGenerator* cg, const PP_AST_Node* node) {
    PP_LoopContext* loop_ctx = NULL;
    assert(cg);
    assert(node);
    assert(node->node_type == PP_AST_BreakStatementNode);

    loop_ctx = pp_code_generator_get_current_loop_context(cg);
    if (!loop_ctx) return 0; /* Should have been caught in semantic analysis */

    if (cg->debug) pp_code_generator_debug_set_location(cg, &node->loc);
    /* Leaving the loop body releases everything it declared (and every nested block inside it). */
    pp_code_generator_emit_drops_since(cg, loop_ctx->drop_stack_mark);
    LLVMBuildBr(cg->builder, loop_ctx->exit_bb);
    return 1;
}

static int pp_code_generator_generate_continue(PP_CodeGenerator* cg, const PP_AST_Node* node) {
    PP_LoopContext* loop_ctx = NULL;
    assert(cg);
    assert(node);
    assert(node->node_type == PP_AST_ContinueStatementNode);

    loop_ctx = pp_code_generator_get_current_loop_context(cg);
    if (!loop_ctx) return 0; /* Should have been caught in semantic analysis */

    if (cg->debug) pp_code_generator_debug_set_location(cg, &node->loc);
    /* The step/condition runs after the body scope ends, so body locals are released here too. */
    pp_code_generator_emit_drops_since(cg, loop_ctx->drop_stack_mark);
    LLVMBuildBr(cg->builder, loop_ctx->continue_bb);
    return 1;
}

/* ABI layout for size/alignment/offset queries.
 *
 * The module carries a data layout only once the object is emitted, so codegen builds the layout from a
 * native target machine instead. The caller owns the returned layout and releases it with
 * LLVMDisposeTargetData(). Returns NULL when the native target is unavailable. */
static LLVMTargetDataRef pp_code_generator_target_data_create(void) {
    char* triple = NULL;
    char* err = NULL;
    LLVMTargetRef target = NULL;
    LLVMTargetMachineRef target_machine = NULL;
    LLVMTargetDataRef target_data = NULL;

    LLVMInitializeNativeTarget();
    triple = LLVMGetDefaultTargetTriple();
    if (triple == NULL) return NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        if (err) LLVMDisposeMessage(err);
        LLVMDisposeMessage(triple);
        return NULL;
    }

    target_machine = LLVMCreateTargetMachine(target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocPIC,
                                             LLVMCodeModelDefault);
    LLVMDisposeMessage(triple);
    if (target_machine == NULL) return NULL;

    target_data = LLVMCreateTargetDataLayout(target_machine);
    LLVMDisposeTargetMachine(target_machine);
    return target_data;
}

/* A DW_TAG_member for an aggregate type. A raw type in a composite type's element list produces no
 * component, so every field of a struct/list/string goes through this helper. */
static LLVMMetadataRef pp_code_generator_di_member(PP_CodeGenerator* cg, const char* name, uint64_t size_bits,
                                                   uint32_t align_bits, uint64_t offset_bits, LLVMMetadataRef type_di) {
    return LLVMDIBuilderCreateMemberType(cg->di_builder, cg->di_compile_unit, name, strlen(name), cg->di_file, 0,
                                         size_bits, align_bits, offset_bits, LLVMDIFlagZero, type_di);
}

static LLVMMetadataRef pp_code_generator_di_basic_type(PP_CodeGenerator* cg, const char* name, uint64_t size_in_bits,
                                                       LLVMDWARFTypeEncoding encoding) {
    return LLVMDIBuilderCreateBasicType(cg->di_builder, name, strlen(name), size_in_bits, encoding, LLVMDIFlagZero);
}

static LLVMMetadataRef pp_code_generator_di_void_type(PP_CodeGenerator* cg) {
    return pp_code_generator_di_basic_type(cg, "void", 0, 0);
}

/* Returns the return type and parameter types for function-like types. */
static void pp_code_generator_di_function_signature(const PPType* func_type, const PPType** out_return_type,
                                                    const PPType*** out_param_types, size_t* out_param_count) {
    *out_return_type = NULL;
    *out_param_types = NULL;
    *out_param_count = 0;
    assert(func_type);
    switch (func_type->kind) {
        case PP_TK_Function:
            *out_return_type = func_type->type.pp_type_function.return_type;
            *out_param_types = func_type->type.pp_type_function.param_types;
            *out_param_count = func_type->type.pp_type_function.param_count;
            break;
        case PP_TK_Lambda:
            *out_return_type = func_type->type.pp_type_lambda.return_type;
            *out_param_types = func_type->type.pp_type_lambda.param_types;
            *out_param_count = func_type->type.pp_type_lambda.param_count;
            break;
        case PP_TK_Operator:
            *out_return_type = func_type->type.pp_type_operator.return_type;
            *out_param_types = func_type->type.pp_type_operator.param_types;
            *out_param_count = func_type->type.pp_type_operator.param_count;
            break;
        default:
            assert(0 && "expected function-like type");
    }
}

static LLVMMetadataRef pp_code_generator_di_subroutine_type(PP_CodeGenerator* cg, const PPType* func_type) {
    assert(cg);
    const PPType* return_type = NULL;
    const PPType** param_types = NULL;
    size_t param_count = 0;
    LLVMMetadataRef* types = NULL;
    pp_code_generator_di_function_signature(func_type, &return_type, &param_types, &param_count);

    types = (LLVMMetadataRef*)calloc(param_count + 1, sizeof(LLVMMetadataRef));
    NULL_POINTER_ERROR_HANDLE(types);

    LLVMMetadataRef return_di = NULL;
    if (return_type) return_di = pp_code_generator_get_di_type(cg, return_type);
    if (return_di == NULL) return_di = pp_code_generator_di_void_type(cg);
    types[0] = return_di;

    for (size_t i = 0; i < param_count; i++) {
        LLVMMetadataRef param_di = pp_code_generator_get_di_type(cg, param_types[i]);
        if (param_di == NULL) param_di = pp_code_generator_di_void_type(cg);
        types[i + 1] = param_di;
    }

    LLVMMetadataRef subroutine_type =
        LLVMDIBuilderCreateSubroutineType(cg->di_builder, cg->di_file, types, (unsigned int)(param_count + 1),
                                          LLVMDIFlagZero);
    free(types);
    types = NULL;
    return subroutine_type;

error_cleanup:
    free(types);
    return NULL;
}

/** @brief Sign-extend a masked enum member value to 64 bits when the enum's underlying type is signed, so
 * DWARF and LLVM print negative members as negative numbers. */
static int64_t pp_code_generator_enum_member_value(PPTypeKind kind, unsigned long long value) {
    switch (kind) {
        case PP_TK_Int8:
            return (int64_t)(int8_t)(uint8_t)value;
        case PP_TK_Int16:
            return (int64_t)(int16_t)(uint16_t)value;
        case PP_TK_Int32:
            return (int64_t)(int32_t)(uint32_t)value;
        default:
            return (int64_t)value;
    }
}

static LLVMMetadataRef pp_code_generator_get_di_type(PP_CodeGenerator* cg, const PPType* t) {
    bool guard_pushed = false;
    if (!cg->debug || !t) return NULL;
    if (cg->di_type_cache != NULL) {
        for (size_t i = 0; i < cg->di_type_cache_count; i++) {
            if (cg->di_type_cache[i].type == t) { return cg->di_type_cache[i].di; }
        }
    }
    // Recursion guard: callable signatures and struct layouts can refer back to a
    // type that is currently being constructed. Returning NULL lets the caller fall
    // back to an opaque (void) member type instead of recursing forever.
    for (size_t i = 0; i < cg->di_type_in_progress_count; i++) {
        if (cg->di_type_in_progress[i] == t) return NULL;
    }
    if (cg->di_type_in_progress_count < 32) {
        cg->di_type_in_progress[cg->di_type_in_progress_count] = t;
        cg->di_type_in_progress_count++;
        guard_pushed = true;
    }

    LLVMMetadataRef result = NULL;
    LLVMMetadataRef* members = NULL;
    switch (t->kind) {
        case PP_TK_Int8:
            result = pp_code_generator_di_basic_type(cg, "int8", 8, PP_DW_ATE_SIGNED);
            break;
        case PP_TK_UInt8:
            result = pp_code_generator_di_basic_type(cg, "uint8", 8, PP_DW_ATE_UNSIGNED);
            break;
        case PP_TK_Int16:
            result = pp_code_generator_di_basic_type(cg, "int16", 16, PP_DW_ATE_SIGNED);
            break;
        case PP_TK_UInt16:
            result = pp_code_generator_di_basic_type(cg, "uint16", 16, PP_DW_ATE_UNSIGNED);
            break;
        case PP_TK_Int32:
            result = pp_code_generator_di_basic_type(cg, "int32", 32, PP_DW_ATE_SIGNED);
            break;
        case PP_TK_UInt32:
            result = pp_code_generator_di_basic_type(cg, "uint32", 32, PP_DW_ATE_UNSIGNED);
            break;
        case PP_TK_Int64:
            result = pp_code_generator_di_basic_type(cg, "int64", 64, PP_DW_ATE_SIGNED);
            break;
        case PP_TK_UInt64:
            result = pp_code_generator_di_basic_type(cg, "uint64", 64, PP_DW_ATE_UNSIGNED);
            break;
        case PP_TK_Int128:
            result = pp_code_generator_di_basic_type(cg, "int128", 128, PP_DW_ATE_SIGNED);
            break;
        case PP_TK_UInt128:
            result = pp_code_generator_di_basic_type(cg, "uint128", 128, PP_DW_ATE_UNSIGNED);
            break;
        case PP_TK_Float:
            result = pp_code_generator_di_basic_type(cg, "float", 64, PP_DW_ATE_FLOAT);
            break;
        case PP_TK_Bool:
            result = pp_code_generator_di_basic_type(cg, "bool", 8, PP_DW_ATE_BOOLEAN);
            break;
        case PP_TK_Void:
            result = pp_code_generator_di_void_type(cg);
            break;
        case PP_TK_Const: {
            const PPType* underlying = t->type.pp_type_const.const_of;
            result = pp_code_generator_get_di_type(cg, underlying);
            break;
        }
        case PP_TK_Newtype: {
            const PPType* underlying = t->type.pp_type_newtype.underlying;
            if (!pp_type_is_enum_or_flag(t)) {
                result = pp_code_generator_get_di_type(cg, underlying);
                break;
            }
            const PP_EnumMember* enum_members = t->type.pp_type_newtype.members;
            const size_t enum_member_count = t->type.pp_type_newtype.member_count;
            LLVMMetadataRef* enumerators = NULL;
            if (enum_member_count > 0) {
                enumerators = (LLVMMetadataRef*)calloc(enum_member_count, sizeof(LLVMMetadataRef));
                NULL_POINTER_ERROR_HANDLE(enumerators);
                const bool is_unsigned = pp_type_is_flag(t) || pp_type_kind_is_unsigned(underlying->kind);
                for (size_t i = 0; i < enum_member_count; i++) {
                    const int64_t member_value =
                        is_unsigned ? (int64_t)enum_members[i].value
                                    : pp_code_generator_enum_member_value(underlying->kind, enum_members[i].value);
                    enumerators[i] =
                        LLVMDIBuilderCreateEnumerator(cg->di_builder, enum_members[i].name,
                                                      strlen(enum_members[i].name), member_value, is_unsigned);
                }
            }
            LLVMTypeRef llvm_underlying_type = pp_backend_lower_type(cg, underlying);
            uint64_t size_bits = 0;
            if (llvm_underlying_type) { size_bits = pp_code_generator_type_store_size(cg, llvm_underlying_type) * 8; }
            const char* enum_name = t->type.pp_type_newtype.name ? t->type.pp_type_newtype.name : "<anonymous>";
            // ClassTy is the underlying integer type; without it DWARF has no DW_AT_type for the enumeration.
            LLVMMetadataRef enum_class_di = pp_code_generator_get_di_type(cg, underlying);
            result =
                LLVMDIBuilderCreateEnumerationType(cg->di_builder, cg->di_compile_unit, enum_name, strlen(enum_name),
                                                   cg->di_file, 0, size_bits, (uint32_t)size_bits, enumerators,
                                                   (unsigned)enum_member_count, enum_class_di);
            free(enumerators);
            break;
        }
        case PP_TK_Pointer: {
            const PPType* pointee = t->type.pp_type_pointer.pointee;
            LLVMMetadataRef pointee_di = pp_code_generator_get_di_type(cg, pointee);
            if (pointee_di) {
                uint64_t size_bits = 64;
                uint32_t align_bits = 64;
                result = LLVMDIBuilderCreatePointerType(cg->di_builder, pointee_di, size_bits, align_bits, 0, NULL, 0);
            }
            break;
        }
        case PP_TK_Reference: {
            const PPType* ref_to = t->type.pp_type_reference.reference_to;
            LLVMMetadataRef ref_di = pp_code_generator_get_di_type(cg, ref_to);
            if (ref_di) {
                /* The language auto-dereferences a reference, so describe it as DW_TAG_reference_type (0x10).
                 * As a pointer type, "p ctx.key" would become a member access on a pointer and fail. */
                result = LLVMDIBuilderCreateReferenceType(cg->di_builder, 0x10, ref_di);
            }
            break;
        }
        case PP_TK_Array: {
            const PPType* elem = t->type.pp_type_array.element_type;
            LLVMMetadataRef elem_di = pp_code_generator_get_di_type(cg, elem);
            if (elem_di) {
                LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, t);
                uint64_t size_bits = 0;
                uint32_t align_bits = 0;
                if (llvm_type) {
                    size_bits = pp_code_generator_type_store_size(cg, llvm_type) * 8;
                    align_bits = 64;
                }
                LLVMMetadataRef subrange =
                    LLVMDIBuilderGetOrCreateSubrange(cg->di_builder, 0, (int64_t)t->type.pp_type_array.size);
                LLVMMetadataRef* subscripts = &subrange;
                result = LLVMDIBuilderCreateArrayType(cg->di_builder, size_bits, align_bits, elem_di, subscripts, 1);
            }
            break;
        }
        case PP_TK_Struct: {
            const char* name = t->type.pp_type_struct.name ? t->type.pp_type_struct.name : "<anonymous>";
            const PP_StructLayout* layout = t->type.pp_type_struct.layout;
            size_t field_count = layout ? layout->field_count : 0;
            LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, t);
            LLVMTargetDataRef target_data = NULL;
            if (field_count > 0) {
                members = (LLVMMetadataRef*)calloc(field_count, sizeof(LLVMMetadataRef));
                NULL_POINTER_ERROR_HANDLE(members);
                /* Field offsets need the struct body, which is not set for opaque/recursive placeholders. */
                if (llvm_type && !LLVMIsOpaqueStruct(llvm_type)) {
                    target_data = pp_code_generator_target_data_create();
                }
                for (size_t i = 0; i < field_count; i++) {
                    const PPStructField* field = &layout->fields[i];
                    LLVMMetadataRef field_di = pp_code_generator_get_di_type(cg, field->field_type);
                    LLVMTypeRef llvm_field_type = NULL;
                    uint64_t field_size_bits = 0;
                    uint32_t field_align_bits = 0;
                    uint64_t field_offset_bits = 0;
                    /* A struct may contain itself through a pointer; the in-progress guard returns no type. */
                    if (!field_di) { field_di = pp_code_generator_di_void_type(cg); }
                    if (target_data) { llvm_field_type = pp_environment_backend_lookup_type(cg, field->field_type); }
                    if (target_data && llvm_field_type) {
                        field_size_bits = (uint64_t)LLVMStoreSizeOfType(target_data, llvm_field_type) * 8;
                        field_align_bits = LLVMABIAlignmentOfType(target_data, llvm_field_type) * 8;
                        field_offset_bits = LLVMOffsetOfElement(target_data, llvm_type, (unsigned)i) * 8;
                    }
                    members[i] = pp_code_generator_di_member(cg, field->field_name, field_size_bits, field_align_bits,
                                                             field_offset_bits, field_di);
                }
                if (target_data) {
                    LLVMDisposeTargetData(target_data);
                    target_data = NULL;
                }
            }
            uint64_t size_bits = 0;
            uint32_t align_bits = 0;
            if (llvm_type) {
                size_bits = pp_code_generator_type_store_size(cg, llvm_type) * 8;
                align_bits = 64;
            }
            result = LLVMDIBuilderCreateStructType(cg->di_builder, cg->di_compile_unit, name, strlen(name), cg->di_file,
                                                   0, size_bits, align_bits, LLVMDIFlagZero, NULL, members,
                                                   (unsigned)field_count, 0, NULL, NULL, 0);
            free(members);
            members = NULL;
            break;
        }
        case PP_TK_String: {
            /* Layout mirrors pp_string_t: { uint8* ptr; uint64 len; } */
            LLVMMetadataRef members_stack[2];
            LLVMMetadataRef i64_di = pp_code_generator_di_basic_type(cg, "i64", 64, PP_DW_ATE_SIGNED);
            LLVMMetadataRef i8_ptr_di =
                LLVMDIBuilderCreatePointerType(cg->di_builder,
                                               pp_code_generator_di_basic_type(cg, "i8", 8, PP_DW_ATE_SIGNED_CHAR), 64,
                                               64, 0, NULL, 0);
            members_stack[0] = pp_code_generator_di_member(cg, "ptr", 64, 64, 0, i8_ptr_di);
            members_stack[1] = pp_code_generator_di_member(cg, "len", 64, 64, 64, i64_di);
            /* SizeInBits is in bits: a byte count here makes gdb treat the members as out of bounds. */
            result = LLVMDIBuilderCreateStructType(cg->di_builder, cg->di_compile_unit, "pp.string", 9, cg->di_file, 0,
                                                   128, 64, LLVMDIFlagZero, NULL, members_stack, 2, 0, NULL, NULL, 0);
            break;
        }
        case PP_TK_List: {
            /* Layout mirrors pp_list_t: { void* data; uint64 len, cap, elem_size; }.
             * data is typed as element* so elements can be expanded with "p *l.data@l.len". */
            LLVMMetadataRef members_stack[4];
            LLVMMetadataRef i64_di = pp_code_generator_di_basic_type(cg, "i64", 64, PP_DW_ATE_SIGNED);
            LLVMMetadataRef element_di = pp_code_generator_get_di_type(cg, t->type.pp_type_list.element_type);
            LLVMMetadataRef data_di = NULL;
            LLVMMetadataRef i8_ptr_di =
                LLVMDIBuilderCreatePointerType(cg->di_builder,
                                               pp_code_generator_di_basic_type(cg, "i8", 8, PP_DW_ATE_SIGNED_CHAR), 64,
                                               64, 0, NULL, 0);
            if (element_di) {
                data_di = LLVMDIBuilderCreatePointerType(cg->di_builder, element_di, 64, 64, 0, NULL, 0);
            } else {
                /* A list of its own type recurses; the in-progress guard yields no element type. */
                data_di = i8_ptr_di;
            }
            members_stack[0] = pp_code_generator_di_member(cg, "data", 64, 64, 0, data_di);
            members_stack[1] = pp_code_generator_di_member(cg, "len", 64, 64, 64, i64_di);
            members_stack[2] = pp_code_generator_di_member(cg, "cap", 64, 64, 128, i64_di);
            members_stack[3] = pp_code_generator_di_member(cg, "elem_size", 64, 64, 192, i64_di);
            /* SizeInBits is in bits: a byte count here makes gdb treat the members as out of bounds. */
            result = LLVMDIBuilderCreateStructType(cg->di_builder, cg->di_compile_unit, "pp.list", 7, cg->di_file, 0,
                                                   256, 64, LLVMDIFlagZero, NULL, members_stack, 4, 0, NULL, NULL, 0);
            break;
        }
        case PP_TK_Function:
        case PP_TK_Lambda:
        case PP_TK_Operator: {
            // A callable value is a code pointer: describe it as pointer-to-subroutine.
            LLVMMetadataRef subroutine = pp_code_generator_di_subroutine_type(cg, t);
            if (subroutine) { result = LLVMDIBuilderCreatePointerType(cg->di_builder, subroutine, 64, 64, 0, NULL, 0); }
            break;
        }
        default:
            result = NULL;
            break;
    }
    if (guard_pushed) { cg->di_type_in_progress_count--; }
    if (result) {
        if (cg->di_type_cache_count >= cg->di_type_cache_capacity) {
            cg->di_type_cache_capacity *= 2;
            cg->di_type_cache = (PP_DITypeCacheEntry*)realloc(cg->di_type_cache,
                                                              cg->di_type_cache_capacity * sizeof(PP_DITypeCacheEntry));
        }
        cg->di_type_cache[cg->di_type_cache_count].type = t;
        cg->di_type_cache[cg->di_type_cache_count].di = result;
        cg->di_type_cache_count++;
    }
    return result;

error_cleanup:
    if (guard_pushed) { cg->di_type_in_progress_count--; }
    free(members);
    return NULL;
}

static int pp_code_generator_fill_type_map_primitive_opaque_struct(PP_CodeGenerator* cg) {
    assert(cg);
    assert(cg->gtt);
    assert(cg->global_env_b);

    for (size_t i = 0; i < cg->gtt->type_count; i++) {
        const PPType* type = cg->gtt->types[i];
        assert(type);

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
            default:
                break;
        }
    }

    return 1;

error_cleanup:
    free(llvm_types);
    return 0;
}

static uint64_t pp_code_generator_type_store_size(PP_CodeGenerator* cg, LLVMTypeRef type) {
    LLVMTargetDataRef target_data = NULL;
    uint64_t size = 0;
    (void)cg;

    target_data = pp_code_generator_target_data_create();
    if (target_data == NULL) return 0;

    size = (uint64_t)LLVMStoreSizeOfType(target_data, type);
    LLVMDisposeTargetData(target_data);
    return size;
}

static LLVMValueRef pp_code_generator_entry_alloca(PP_CodeGenerator* cg, LLVMTypeRef type, const char* name) {
    LLVMBasicBlockRef entry_bb = LLVMGetFirstBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)));
    LLVMBuilderRef tmp_builder = LLVMCreateBuilderInContext(cg->ctx);
    if (LLVMGetFirstInstruction(entry_bb)) {
        LLVMPositionBuilderBefore(tmp_builder, LLVMGetFirstInstruction(entry_bb));
    } else {
        LLVMPositionBuilderAtEnd(tmp_builder, entry_bb);
    }
    LLVMValueRef alloca = LLVMBuildAlloca(tmp_builder, type, name);
    LLVMDisposeBuilder(tmp_builder);
    return alloca;
}

int pp_code_generator_generate(PP_CodeGenerator* cg) {
    assert(cg);
    assert(cg->program);
    assert(cg->program->node_type == PP_AST_ProgramNode);

    // Pass 1 to build type map. (Primitive & Opaque Struct & Reference)
    if (pp_code_generator_fill_type_map_primitive_opaque_struct(cg) != 1) { goto error_cleanup; }

    // Pass 2 to build type map. (Struct Body & Function)
    if (pp_code_generator_fill_type_map_complex(cg) != 1) { goto error_cleanup; }

    // Pre-declare every callable symbol (definitions and externs alike) so that a
    // callable value can be referenced before its definition is emitted: global
    // function-typed initializers, or a call appearing textually earlier.
    for (size_t i = 0; i < cg->gst->count; i++) {
        const char* name = cg->gst->names[i];
        const PPSymbol* sym = cg->gst->symbols[i];
        if (!sym || !sym->is_callable_decl) continue;
        if (sym->type->kind != PP_TK_Function && sym->type->kind != PP_TK_Lambda && sym->type->kind != PP_TK_Operator)
            continue;
        LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, name);
        if (!fn) {
            LLVMTypeRef llvm_type = pp_code_generator_llvm_subroutine_type(cg, sym->type);
            if (!llvm_type) continue;
            fn = LLVMAddFunction(cg->mod, name, llvm_type);
            if (!fn) continue;
            if (!sym->is_definition) { LLVMSetLinkage(fn, LLVMExternalLinkage); }
        }
        if (pp_environment_backend_lookup_val(cg->global_env_b, sym, false)) continue;
        if (!pp_environment_backend_push_val_map(cg->global_env_b, sym, fn)) { goto error_cleanup; }
    }

    for (unsigned int i = 0; i < cg->program->program.decl_count; i++) {
        const PP_AST_Node* statement = cg->program->program.decls[i];
        NULL_POINTER_ERROR_HANDLE(statement);

        switch (statement->node_type) {
            case PP_AST_CallableDeclarationNode:
                pp_code_generator_emit_function(cg, cg->gst, cg->global_env_b, statement);
                break;
            case PP_AST_StructDeclarationNode:
                break;
            case PP_AST_VariableDeclarationNode:
                pp_code_generator_emit_global_variable(cg, cg->gst, cg->global_env_b, statement);
                break;
            case PP_AST_ConstantDeclarationNode:
                /* Constants have no storage and no link-time symbol: references are folded. */
                break;
            case PP_AST_TypedefDeclarationNode:
            case PP_AST_NewtypeDeclarationNode:
            case PP_AST_HandleDeclarationNode:
            case PP_AST_EnumDeclarationNode:
            case PP_AST_FlagDeclarationNode:
            case PP_AST_ExternCallableDeclarationNode:
                break;
            default:
                abort();
                break;
        }
    }

    // Second pass to generate bodies for top-level functions (legacy, disabled - bodies are generated in emit_function)
    // for (size_t i = 0; i < cg->program->program.decl_count; i++) {
    //     const PP_AST_Node* stmt = cg->program->program.decls[i];
    //     assert(stmt);
    //
    //     if (stmt->node_type != PP_AST_FunctionCallNode && stmt->node_type != PP_AST_CallableDeclarationNode)
    //     continue;
    //
    //     const char* func_name = stmt->callable_declaration.name;
    //     assert(func_name);
    //
    //     if (!pp_code_generator_generate_function_body(cg, cg->gst, cg->global_env_b, stmt)) {
    //         assert(0 && "Function body generation failure.");
    //     }
    // }

    // Verify generated LLVM module
    {
        char* verify_err = NULL;
        if (LLVMVerifyModule(cg->mod, LLVMReturnStatusAction, &verify_err)) {
            if (verify_err) {
                pp_error_list_push_error(cg->error_list,
                                         (PP_SourceLoc){.filename = "<internal>", .line = 0, .column = 0},
                                         "LLVM verification failed: %s", verify_err);
                LLVMDisposeMessage(verify_err);
            } else {
                pp_error_list_push_error(cg->error_list,
                                         (PP_SourceLoc){.filename = "<internal>", .line = 0, .column = 0},
                                         "LLVM verification failed");
            }
            goto error_cleanup;
        }
        if (verify_err) LLVMDisposeMessage(verify_err);
    }

    return 1;
error_cleanup:
    return 0;
}

static int pp_code_generator_emit_internal(LLVMModuleRef module, const char* output_filename,
                                           LLVMMemoryBufferRef* out_buf, int to_assembly, LLVMTargetMachineRef tm_in) {
    if (!module || !tm_in) {
        fprintf(stderr, "pp_code_generator_emit_internal requires non-NULL module and target machine\n");
        return 0;
    }
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    LLVMTargetMachineRef target_machine = tm_in;
    char* target_triple = NULL;

    /* Ensure module target triple matches the machine */
    target_triple = LLVMGetTargetMachineTriple(target_machine);
    LLVMSetTarget(module, target_triple);

    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(target_machine);
    LLVMSetModuleDataLayout(module, data_layout);

    int result = 1;
    LLVMCodeGenFileType codegen_file_type = to_assembly ? LLVMAssemblyFile : LLVMObjectFile;
    if (output_filename) {
        char* error = NULL;
        if (LLVMTargetMachineEmitToFile(target_machine, module, (char*)output_filename, codegen_file_type, &error)) {
            fprintf(stderr, "Failed to emit object file: %s\n", error);
            LLVMDisposeMessage(error);
            result = 0;
        }
    } else if (out_buf) {
        LLVMMemoryBufferRef buf = NULL;
        char* error = NULL;
        if (LLVMTargetMachineEmitToMemoryBuffer(target_machine, module, codegen_file_type, &error, &buf)) {
            fprintf(stderr, "Failed to emit object to buffer: %s\n", error);
            LLVMDisposeMessage(error);
            result = 0;
        } else {
            if (out_buf)
                *out_buf = buf;
            else
                LLVMDisposeMemoryBuffer(buf);
        }
    } else {
        result = 0;
    }

    LLVMDisposeTargetData(data_layout);
    if (target_triple) LLVMDisposeMessage(target_triple);
    return result;
}

int pp_code_generator_emit_object_file(LLVMModuleRef module, const char* output_filename, LLVMTargetMachineRef tm) {
    return pp_code_generator_emit_internal(module, output_filename, NULL, 0, tm);
}

int pp_code_generator_emit_assembly_file(LLVMModuleRef module, const char* output_filename, LLVMTargetMachineRef tm) {
    return pp_code_generator_emit_internal(module, output_filename, NULL, 1, tm);
}

int pp_code_generator_emit_object_to_buffer(LLVMModuleRef module, LLVMMemoryBufferRef* out_buf,
                                            LLVMTargetMachineRef tm) {
    return pp_code_generator_emit_internal(module, NULL, out_buf, 0, tm);
}

int pp_code_generator_emit_assembly_to_buffer(LLVMModuleRef module, LLVMMemoryBufferRef* out_buf,
                                              LLVMTargetMachineRef tm) {
    return pp_code_generator_emit_internal(module, NULL, out_buf, 1, tm);
}

int pp_code_generator_emit_ir_file(LLVMModuleRef module, const char* output_filename) {
    if (!module || !output_filename) return 0;
    char* error = NULL;
    if (LLVMPrintModuleToFile(module, (char*)output_filename, &error)) {
        if (error) {
            fprintf(stderr, "Failed to emit IR file: %s\n", error);
            LLVMDisposeMessage(error);
        }
        return 0;
    }
    return 1;
}

static int pp_code_generator_generate_function_body(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                    PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    PP_Environment_Backend* body_env_b = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_CallableDeclarationNode);

    /* Implementation will follow CZ:
       1. Create entry basic block.
       2. Populate local environment with parameters (alloca + store).
       3. Generate statements in the body.
       4. Ensure terminator exists for all blocks. */

    const char* func_name = stmt->callable_declaration.name;
    assert(func_name);
    const PPSymbol* func_symbol = pp_symbol_table_lookup(symbol_table, func_name, false);
    assert(func_symbol);

    assert(func_symbol->type);
    cg->current_function_type = func_symbol->type;
    bool is_lambda = func_symbol->type->kind == PP_TK_Lambda;
    bool is_operator = func_symbol->type->kind == PP_TK_Operator;

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
    size_t param_count = is_lambda     ? func_symbol->type->type.pp_type_lambda.param_count
                         : is_operator ? func_symbol->type->type.pp_type_operator.param_count
                                       : func_symbol->type->type.pp_type_function.param_count;
    for (size_t i = 0; i < param_count; i++) {
        LLVMValueRef llvm_param_val = LLVMGetParam(llvm_func, i);
        assert(llvm_param_val);
        const char* param_name = stmt->callable_declaration.params[i]->param_declaration.identifier;
        assert(param_name);
        const PPSymbol* param_symbol =
            pp_symbol_table_lookup(stmt->callable_declaration.param_symbol_table, param_name, false);
        assert(param_symbol);
        const PPType* param_type = param_symbol->type;
        assert(param_type);
        LLVMTypeRef llvm_param_type = pp_environment_backend_lookup_type(cg, param_type);
        assert(llvm_param_type);

        LLVMValueRef llvm_alloca_slot = pp_code_generator_entry_alloca(cg, llvm_param_type, param_name);
        LLVMBuildStore(cg->builder, llvm_param_val, llvm_alloca_slot);

        if (cg->debug) {
            LLVMMetadataRef di_type = pp_code_generator_get_di_type(cg, param_type);
            if (di_type) {
                unsigned int line = stmt->callable_declaration.params[i]->loc.line;
                if (line == 0) line = 1;
                LLVMMetadataRef di_var =
                    LLVMDIBuilderCreateParameterVariable(cg->di_builder, cg->di_current_scope, param_name,
                                                         strlen(param_name), i + 1, cg->di_file, line, di_type, 0,
                                                         LLVMDIFlagZero);
                if (di_var) {
                    LLVMMetadataRef expr = LLVMDIBuilderCreateExpression(cg->di_builder, NULL, 0);
                    LLVMDIBuilderInsertDeclareRecordAtEnd(cg->di_builder, llvm_alloca_slot, di_var, expr,
                                                          LLVMDIBuilderCreateDebugLocation(cg->ctx, line, 0,
                                                                                           cg->di_current_scope, NULL),
                                                          LLVMGetInsertBlock(cg->builder));
                }
            }
        }

        if (!pp_environment_backend_push_val_map(body_env_b, param_symbol, llvm_alloca_slot)) { goto error_cleanup; }

        if (!pp_type_is_reference(param_type) && pp_type_needs_drop(param_type)) {
            pp_code_generator_push_drop(cg, llvm_alloca_slot, param_type);
        }
    }

    size_t stmt_count = stmt->callable_declaration.body->statement_list.stmt_count;
    const PP_AST_Node** stmt_list = stmt->callable_declaration.body->statement_list.stmts;
    for (size_t i = 0; i < stmt_count; i++) {
        if (!pp_code_generator_generate_statement(cg, stmt->callable_declaration.body->statement_list.symbol_table,
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
                pp_code_generator_emit_scope_drops(cg);
                LLVMBuildRetVoid(cg->builder);
            } else {
                // Emits LLVM unreachable for non-void control flow fallthrough
                LLVMBuildUnreachable(cg->builder);
            }
        }
    }

    pp_environment_backend_free(body_env_b);
    // Never carry leftover entries into the next function body.
    cg->drop_stack_count = 0;
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

    pp_code_generator_debug_set_location(cg, &node->loc);

    /* Code after a return/break/continue in the same block is unreachable, and emitting it would
     * append instructions to an already terminated basic block. */
    LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(cg->builder);
    if (current_bb != NULL && LLVMGetBasicBlockTerminator(current_bb) != NULL) { return 1; }

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
        case PP_AST_BreakStatementNode:
            if (!pp_code_generator_generate_break(cg, node)) return 0;
            break;
        case PP_AST_ContinueStatementNode:
            if (!pp_code_generator_generate_continue(cg, node)) return 0;
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
                LLVMValueRef val = pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, node);
                (void)val;
            }
            break;
    }

    pp_code_generator_flush_temp_drops(cg);
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

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(cg->builder);
    LLVMBasicBlockRef entry_bb = LLVMGetFirstBasicBlock(LLVMGetBasicBlockParent(cur_bb));
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry_bb);
    if (term) {
        LLVMPositionBuilderBefore(cg->builder, term);
    } else {
        LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    }
    LLVMValueRef llvm_var_alloca = pp_code_generator_entry_alloca(cg, llvm_var_type, var_name);
    assert(llvm_var_alloca);
    LLVMPositionBuilderAtEnd(cg->builder, cur_bb);

    if (cg->debug) {
        LLVMMetadataRef di_type = pp_code_generator_get_di_type(cg, var_type);
        if (di_type) {
            unsigned int line = stmt->loc.line;
            unsigned int col = stmt->loc.column;
            LLVMMetadataRef di_var =
                LLVMDIBuilderCreateAutoVariable(cg->di_builder, cg->di_current_scope, var_name, strlen(var_name),
                                                cg->di_file, line, di_type, 0, LLVMDIFlagZero, 0);
            if (di_var) {
                LLVMMetadataRef expr = LLVMDIBuilderCreateExpression(cg->di_builder, NULL, 0);
                LLVMMetadataRef dbg_loc =
                    LLVMDIBuilderCreateDebugLocation(cg->ctx, line, col, cg->di_current_scope, NULL);
                LLVMDIBuilderInsertDeclareRecordAtEnd(cg->di_builder, llvm_var_alloca, di_var, expr, dbg_loc, cur_bb);
            }
        }
    }

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
            const PP_AST_Decoration* init_decor = pp_ast_get_decoration(initializer_node);
            assert(init_decor);
            bool is_lvalue = init_decor->value_category == PP_VALUE_CATEGORY_LVALUE;
            if (!pp_type_needs_drop(var_type) || !is_lvalue) {
                LLVMValueRef var_initializer =
                    pp_code_generator_generate_rvalue(cg, symbol_table, env_b, initializer_node);
                assert(var_initializer);
                LLVMBuildStore(cg->builder, var_initializer, llvm_var_alloca);
            } else {
                // Lvalue source for owning type → copy
                LLVMValueRef src_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, initializer_node);
                assert(src_ptr);
                pp_code_generator_emit_copy(cg, llvm_var_alloca, src_ptr, var_type);
            }
        }
    }

    if (pp_environment_backend_push_val_map(env_b, var_symbol, llvm_var_alloca) != 1) { return 0; }

    // Push onto scope drop stack if needed
    if (!pp_type_is_reference(var_type) && pp_type_needs_drop(var_type)) {
        pp_code_generator_push_drop(cg, llvm_var_alloca, var_type);
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

    const PP_AST_Decoration* lhs_decor = pp_ast_get_decoration(stmt->assignment.lhs);
    assert(lhs_decor);
    const PPType* lhs_type = lhs_decor->resolved_type;
    assert(lhs_type);
    /* Assigning through a reference stores into the referent, so ownership decisions (drop the
     * overwritten value, copy the source) are made on the pointee type. */
    const PPType* lhs_storage_type = lhs_type;
    if (pp_type_is_reference(lhs_type)) { lhs_storage_type = lhs_type->type.pp_type_reference.reference_to; }
    assert(lhs_storage_type);

    if (stmt->assignment.op == PP_TT_EQUAL) {
        const PP_AST_Decoration* rhs_decor = pp_ast_get_decoration(stmt->assignment.rhs);
        assert(rhs_decor);
        bool rhs_is_lvalue = rhs_decor->value_category == PP_VALUE_CATEGORY_LVALUE;
        bool needs_drop = pp_type_needs_drop(lhs_storage_type) && !pp_type_is_reference(lhs_storage_type);
        if (needs_drop && rhs_is_lvalue) {
            // Copy from lvalue source
            LLVMValueRef src_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, stmt->assignment.rhs);
            assert(src_ptr);
            // Self-assignment guard
            if (src_ptr == llvm_lhs) {
                // No-op for self-assignment
                return 1;
            }
            // Drop old value to avoid leak
            pp_code_generator_emit_drop(cg, llvm_lhs, lhs_storage_type);
            pp_code_generator_emit_copy(cg, llvm_lhs, src_ptr, lhs_storage_type);
            return 1;
        }
        // Normal rvalue or trivial copy
        LLVMValueRef llvm_rhs = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->assignment.rhs);
        assert(llvm_rhs);
        // An owning lvalue always holds a live value (an uninitialized local is zero-filled at declaration), so
        // whatever it holds now must be released before the new value is stored. This covers handles as well as
        // strings and lists; without it the overwritten value leaks.
        if (needs_drop) { pp_code_generator_emit_drop(cg, llvm_lhs, lhs_storage_type); }
        LLVMBuildStore(cg->builder, llvm_rhs, llvm_lhs);
        return 1;
    }

    /* Compound assignment reads the right-hand side but never takes ownership of it, so an owning
     * r-value operand (e.g. `s += "x" + y`) is released at the end of the statement. */
    LLVMValueRef llvm_rhs = pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, stmt->assignment.rhs);
    assert(llvm_rhs);

    LLVMTypeRef llvm_rhs_type = LLVMTypeOf(llvm_rhs);
    assert(llvm_rhs_type);
    LLVMTypeKind llvm_rhs_type_kind = LLVMGetTypeKind(llvm_rhs_type);
    assert(llvm_rhs_type_kind);

    LLVMValueRef llvm_val = llvm_rhs;

    if (stmt->assignment.op != PP_TT_EQUAL) {
        LLVMValueRef llvm_lhs_val = pp_code_generator_l_value_to_r_value(cg, lhs_type, llvm_lhs);
        assert(llvm_lhs_val);

        switch (stmt->assignment.op) {
            case PP_TT_PLUS_EQUAL:
                if (lhs_storage_type->kind == PP_TK_String) {
                    /* String += String: concat and replace lhs */
                    // Load lhs string value
                    LLVMValueRef lhs_ptr_val = LLVMBuildExtractValue(cg->builder, llvm_lhs_val, 0, "lhs_str_ptr");
                    LLVMValueRef lhs_len_val = LLVMBuildExtractValue(cg->builder, llvm_lhs_val, 1, "lhs_str_len");
                    LLVMValueRef rhs_ptr_val = LLVMBuildExtractValue(cg->builder, llvm_rhs, 0, "rhs_str_ptr");
                    LLVMValueRef rhs_len_val = LLVMBuildExtractValue(cg->builder, llvm_rhs, 1, "rhs_str_len");
                    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(cg->ctx);
                    LLVMValueRef total_len = LLVMBuildAdd(cg->builder, lhs_len_val, rhs_len_val, "str_total_len");
                    LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
                    assert(malloc_fn);
                    LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerTypeInContext(cg->ctx, 0), &i64_ty, 1, 0);
                    LLVMValueRef raw_ptr =
                        LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &total_len, 1, "str_concat_malloc");
                    LLVMValueRef offset_ptr = LLVMBuildGEP2(cg->builder, LLVMInt8TypeInContext(cg->ctx), raw_ptr,
                                                            &lhs_len_val, 1, "str_concat_offset");
                    pp_code_generator_emit_memcpy(cg, raw_ptr, lhs_ptr_val, lhs_len_val, "str_concat_memcpy1");
                    pp_code_generator_emit_memcpy(cg, offset_ptr, rhs_ptr_val, rhs_len_val, "str_concat_memcpy2");
                    // Drop old lhs string to avoid leak
                    pp_code_generator_emit_drop(cg, llvm_lhs, lhs_storage_type);
                    // Store new string into lhs
                    LLVMTypeRef str_struct_ty = pp_environment_backend_lookup_type(cg, lhs_storage_type);
                    LLVMValueRef undef = LLVMGetUndef(str_struct_ty);
                    LLVMValueRef ptr_field = LLVMBuildInsertValue(cg->builder, undef, raw_ptr, 0, "str_new_ptr");
                    LLVMValueRef new_str = LLVMBuildInsertValue(cg->builder, ptr_field, total_len, 1, "str_new");
                    LLVMBuildStore(cg->builder, new_str, llvm_lhs);
                    // No need to continue with generic store
                    return 1;
                }
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
                    bool is_unsigned = pp_type_is_unsigned_integer(lhs_type);
                    llvm_val = is_unsigned ? LLVMBuildUDiv(cg->builder, llvm_lhs_val, llvm_rhs, "udivtmp")
                                           : LLVMBuildSDiv(cg->builder, llvm_lhs_val, llvm_rhs, "sdivtmp");
                }
                break;
            case PP_TT_PERCENT_EQUAL: {
                bool is_unsigned = pp_type_is_unsigned_integer(lhs_type);
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
            case PP_TT_LESS_LESS_EQUAL:
            case PP_TT_GREATER_GREATER_EQUAL:
                if (pp_type_is_unsigned_integer(lhs_type)) {
                    // Left-shift overflow defined.
                    unsigned int bits = 0;
                    switch (lhs_type->kind) {
                        case PP_TK_UInt8:
                            bits = 8;
                            break;
                        case PP_TK_UInt16:
                            bits = 16;
                            break;
                        case PP_TK_UInt32:
                            bits = 32;
                            break;
                        case PP_TK_UInt64:
                            bits = 64;
                            break;
                        case PP_TK_UInt128:
                            bits = 128;
                            break;
                        default:
                            abort();
                    }
                    LLVMTypeRef rhs_type = LLVMTypeOf(llvm_rhs);
                    LLVMValueRef shift_limit = LLVMConstInt(rhs_type, (unsigned long long)bits, false);
                    LLVMValueRef cmp_ge = LLVMBuildICmp(cg->builder, LLVMIntUGE, llvm_rhs, shift_limit, "sh_ge");
                    LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(llvm_lhs_val));
                    LLVMValueRef sh_val;
                    if (stmt->assignment.op == PP_TT_LESS_LESS_EQUAL) {
                        sh_val = LLVMBuildShl(cg->builder, llvm_lhs_val, llvm_rhs, "shltmp");
                    } else {
                        sh_val = LLVMBuildLShr(cg->builder, llvm_lhs_val, llvm_rhs, "shrttmp");
                    }
                    llvm_val = LLVMBuildSelect(cg->builder, cmp_ge, zero, sh_val, "shl_sel");
                }
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
    assert(cg->current_function_type->kind == PP_TK_Function || cg->current_function_type->kind == PP_TK_Lambda ||
           cg->current_function_type->kind == PP_TK_Operator);

    bool is_lambda = cg->current_function_type->kind == PP_TK_Lambda;
    bool is_operator = cg->current_function_type->kind == PP_TK_Operator;
    const PPType* current_function_return_type = is_lambda ? cg->current_function_type->type.pp_type_lambda.return_type
                                                 : is_operator
                                                     ? cg->current_function_type->type.pp_type_operator.return_type
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
        const PP_AST_Node* return_expr = stmt->return_statement.expr;
        assert(return_expr);
        const PP_AST_Decoration* return_dec = pp_ast_get_decoration(return_expr);
        assert(return_dec);
        const PPType* return_expr_type = pp_type_decay_cg(cg->gtt, return_dec->resolved_type);
        assert(return_expr_type);

        bool expr_is_lvalue = return_dec->value_category == PP_VALUE_CATEGORY_LVALUE;
        bool owns_value = pp_type_needs_drop(return_expr_type) && !pp_type_is_reference(return_expr_type);
        LLVMValueRef llvm_return_val_ref = NULL;

        if (owns_value && expr_is_lvalue && !pp_type_is_handle(return_expr_type)) {
            LLVMValueRef src_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, return_expr);
            assert(src_ptr);

            /* Move only when the source is a slot this frame owns and is about to drop, i.e. a
             * local or a by-value parameter. Anything else (element/member access, reference
             * parameter, global) still owns its buffer after the return, so copy instead. */
            LLVMValueRef move_slot = NULL;
            if (return_expr->node_type == PP_AST_IdentifierNode) {
                const char* return_name = return_expr->identifier.identifier;
                assert(return_name);
                const PPSymbol* sym = pp_symbol_table_lookup(symbol_table, return_name, true);
                assert(sym);
                LLVMValueRef sym_slot = pp_environment_backend_lookup_val(env_b, sym, true);
                assert(sym_slot);
                for (size_t i = 0; i < cg->drop_stack_count; i++) {
                    if (cg->drop_stack_addresses[i] == sym_slot) {
                        move_slot = sym_slot;
                        break;
                    }
                }
            }

            LLVMTypeRef llvm_return_type = pp_environment_backend_lookup_type(cg, return_expr_type);
            assert(llvm_return_type);

            if (move_slot) {
                /* Return-as-move: read the value out first, then clear the source so the scope
                 * drop becomes a no-op. A null initializer is the emptied state for every
                 * pointer-owning aggregate. */
                llvm_return_val_ref = LLVMBuildLoad2(cg->builder, llvm_return_type, src_ptr, "return_moved_value");
                LLVMBuildStore(cg->builder, LLVMConstNull(llvm_return_type), move_slot);
            } else {
                /* Return-as-copy: the source keeps ownership, so hand back an independent value. */
                LLVMValueRef copy_slot = pp_code_generator_entry_alloca(cg, llvm_return_type, "return_copy_slot");
                pp_code_generator_emit_copy(cg, copy_slot, src_ptr, return_expr_type);
                llvm_return_val_ref = LLVMBuildLoad2(cg->builder, llvm_return_type, copy_slot, "return_copied_value");
            }
        } else {
            llvm_return_val_ref =
                pp_code_generator_generate_rvalue(cg, symbol_table, env_b, stmt->return_statement.expr);
        }

        assert(llvm_return_val_ref);
        pp_code_generator_flush_temp_drops(cg);
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
    LLVMValueRef llvm_cond_val =
        pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, stmt->if_statement.cond);
    assert(llvm_cond_val);
    /* The condition's temporaries belong to this statement only, and must die on both branches. */
    pp_code_generator_flush_temp_drops(cg);

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
    size_t loop_context_mark = 0;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_ForStatementNode);

    loop_context_mark = cg->loop_context_count;

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
        llvm_cond_val = pp_code_generator_generate_rvalue_disposable(cg, stmt->for_statement.for_init_symbol_table,
                                                                     inner_env_b, stmt->for_statement.cond);
        assert(llvm_cond_val);
        /* Re-evaluated every iteration, so release the condition's temporaries right here. */
        pp_code_generator_flush_temp_drops(cg);
    } else {
        llvm_cond_val = LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 1, false);
    }

    // Branching: if (cond) (llvm_body_block) else (llvm_end_block)
    LLVMBuildCondBr(cg->builder, llvm_cond_val, llvm_body_block, llvm_end_block);

    // Push loop context: continue -> step_block (or cond_block if no step), break -> end_block
    LLVMBasicBlockRef continue_target = stmt->for_statement.update ? llvm_step_block : llvm_cond_block;
    if (!pp_code_generator_push_loop_context(cg, continue_target, llvm_end_block, cg->drop_stack_count)) {
        goto error_cleanup;
    }

    // Body
    LLVMPositionBuilderAtEnd(cg->builder, llvm_body_block);
    if (pp_code_generator_generate_statement(cg, stmt->for_statement.for_init_symbol_table, inner_env_b,
                                             stmt->for_statement.body) != 1) {
        goto error_cleanup;
    }
    // Check return in body
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
        LLVMBuildBr(cg->builder, llvm_step_block);
    }

    pp_code_generator_pop_loop_context(cg);

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

error_cleanup:
    cg->loop_context_count = loop_context_mark;
    pp_environment_backend_free(inner_env_b);
    return 0;
}

static int pp_code_generator_generate_while_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    size_t loop_context_mark = 0;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_WhileStatementNode);

    loop_context_mark = cg->loop_context_count;

    LLVMValueRef llvm_cur_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder));

    // Condition, body, end block
    LLVMBasicBlockRef llvm_cond_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "while.cond");
    LLVMBasicBlockRef llvm_body_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "while.body");
    LLVMBasicBlockRef llvm_end_block = LLVMAppendBasicBlockInContext(cg->ctx, llvm_cur_func, "while.end");

    // Current position branches to condition block
    LLVMBuildBr(cg->builder, llvm_cond_block);

    // Set builder to write to condition block.
    LLVMPositionBuilderAtEnd(cg->builder, llvm_cond_block);
    LLVMValueRef llvm_cond_val =
        pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, stmt->while_statement.cond);
    assert(llvm_cond_val);
    /* Re-evaluated every iteration, so release the condition's temporaries right here. */
    pp_code_generator_flush_temp_drops(cg);

    // If condition is true, go to body again.
    LLVMBuildCondBr(cg->builder, llvm_cond_val, llvm_body_block, llvm_end_block);

    // Push loop context: continue -> cond_block, break -> end_block
    if (!pp_code_generator_push_loop_context(cg, llvm_cond_block, llvm_end_block, cg->drop_stack_count)) {
        goto error_cleanup;
    }

    // Go to body and write instructions
    LLVMPositionBuilderAtEnd(cg->builder, llvm_body_block);
    if (pp_code_generator_generate_statement(cg, symbol_table, env_b, stmt->while_statement.body) != 1) {
        goto error_cleanup;
    }

    pp_code_generator_pop_loop_context(cg);

    // If there was no return, go back to condition block.
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)) == NULL) {
        LLVMBuildBr(cg->builder, llvm_cond_block);
    }

    LLVMPositionBuilderAtEnd(cg->builder, llvm_end_block);

    return 1;

error_cleanup:
    cg->loop_context_count = loop_context_mark;
    return 0;
}

static int pp_code_generator_generate_block_statement(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    PP_Environment_Backend* inner_env_b = NULL;
    LLVMMetadataRef prev_scope = NULL;
    LLVMMetadataRef lexical_block = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_StatementListNode);

    if (cg->debug) {
        prev_scope = cg->di_current_scope;
        unsigned int line = stmt->loc.line;
        unsigned int col = stmt->loc.column;
        lexical_block = LLVMDIBuilderCreateLexicalBlock(cg->di_builder, cg->di_current_scope, cg->di_file, line, col);
        if (lexical_block) {
            cg->di_current_scope = lexical_block;
            pp_code_generator_debug_set_location(cg, &stmt->loc);
        }
    }

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
    // Emit drops for variables declared in this block before exiting, in reverse order from the
    // current end down to the mark. A `return` inside the block already emitted the drops and left a
    // terminator behind, so the block is dead code from here on and must not emit them again.
    LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(cg->builder);
    bool block_terminated = current_bb != NULL && LLVMGetBasicBlockTerminator(current_bb) != NULL;
    if (!block_terminated) {
        for (size_t i = cg->drop_stack_count; i > drop_stack_mark;) {
            i--;
            LLVMValueRef addr = cg->drop_stack_addresses[i];
            const PPType* typ = cg->drop_stack_types[i];
            if (typ && pp_type_needs_drop(typ) && !pp_type_is_reference(typ)) {
                pp_code_generator_emit_drop(cg, addr, typ);
            }
        }
    }
    cg->drop_stack_count = drop_stack_mark;
    pp_environment_backend_free(inner_env_b);
    if (cg->debug && prev_scope) { cg->di_current_scope = prev_scope; }
    return 1;

error_cleanup:
    if (cg->debug && prev_scope) { cg->di_current_scope = prev_scope; }
    pp_environment_backend_free(inner_env_b);
    return 0;
}

static int pp_code_generator_emit_function(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                           PP_Environment_Backend* env_b, const PP_AST_Node* stmt) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(stmt);
    assert(stmt->node_type == PP_AST_CallableDeclarationNode);

    // Retrieve function/lambda
    const char* func_name = stmt->callable_declaration.name;
    assert(func_name);
    const PPSymbol* func_symbol = pp_symbol_table_lookup(cg->gst, func_name, false);
    assert(func_symbol);
    const PPType* func_type = func_symbol->type;
    assert(func_type);

    // Build the subroutine type on demand (the value type of a callable is `ptr`)
    LLVMTypeRef llvm_func_type = pp_code_generator_llvm_subroutine_type(cg, func_type);
    assert(llvm_func_type);

    // In the case of external function declaration has been done for the function defined in the module, use that.
    LLVMValueRef llvm_func_val = LLVMGetNamedFunction(cg->mod, func_name);
    if (!llvm_func_val) { llvm_func_val = LLVMAddFunction(cg->mod, func_name, llvm_func_type); }
    NULL_POINTER_ERROR_HANDLE(llvm_func_val);

    LLVMMetadataRef prev_scope = cg->di_current_scope;
    if (cg->debug) {
        unsigned int line = stmt->loc.line;
        LLVMMetadataRef subprogram_type = pp_code_generator_di_subroutine_type(cg, func_type);
        LLVMMetadataRef subprogram =
            LLVMDIBuilderCreateFunction(cg->di_builder, cg->di_current_scope, func_name, strlen(func_name), func_name,
                                        strlen(func_name), cg->di_file, line, subprogram_type, 0, 1, line,
                                        LLVMDIFlagZero, 0);
        if (subprogram) {
            LLVMSetSubprogram(llvm_func_val, subprogram);
            cg->di_current_scope = subprogram;
            pp_code_generator_debug_set_location(cg, &stmt->loc);
        }
    }

    if (!pp_environment_backend_lookup_val(env_b, func_symbol, false)) {
        if (!pp_environment_backend_push_val_map(env_b, func_symbol, llvm_func_val)) goto error_cleanup;
    }

    /* Generate function body */
    if (!pp_code_generator_generate_function_body(cg, symbol_table, env_b, stmt)) { goto error_cleanup; }

    cg->di_current_scope = prev_scope;

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

    const PP_AST_Decoration* object_decoration = pp_ast_get_decoration(node->array_list_access.expr);
    assert(object_decoration);

    const PPType* base_type = pp_type_decay_cg(cg->gtt, object_decoration->resolved_type);
    assert(base_type);
    if (base_type->kind != PP_TK_Array && base_type->kind != PP_TK_List && base_type->kind != PP_TK_String) {
        assert(0 && "Unsupported base type for array/list/string access");
        return NULL;
    }

    LLVMValueRef llvm_base_ptr =
        pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->array_list_access.expr);
    assert(llvm_base_ptr);

    LLVMValueRef llvm_index = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->array_list_access.index);
    assert(llvm_index);

    /* GEP indices are sign-extended to the pointer width, so zero-extend an unsigned index explicitly.
     * Without this, a uint8 index of 200 would be used as -56. */
    {
        LLVMTypeRef index_llvm_type = LLVMTypeOf(llvm_index);
        if (LLVMGetTypeKind(index_llvm_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(index_llvm_type) < 64) {
            llvm_index = LLVMBuildZExt(cg->builder, llvm_index, LLVMInt64TypeInContext(cg->ctx), "index_zext");
            assert(llvm_index);
        }
    }

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
    } else if (base_type->kind == PP_TK_List) {
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
    } else if (base_type->kind == PP_TK_String) {
        /* String indexing: load data pointer, then GEP to i8 */
        LLVMTypeRef llvm_str_type = pp_environment_backend_lookup_type(cg, base_type);
        assert(llvm_str_type);
        LLVMValueRef ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, llvm_base_ptr, 0, "str_ptr_ptr");
        LLVMValueRef str_data_ptr =
            LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), ptr_ptr, "str_ptr");
        assert(str_data_ptr);
        LLVMValueRef elem_ptr =
            LLVMBuildGEP2(cg->builder, LLVMInt8TypeInContext(cg->ctx), str_data_ptr, &llvm_index, 1, "str_elem_ptr");
        assert(elem_ptr);
        return elem_ptr;
    } else {
        assert(0 && "Unsupported base type");
        return NULL;
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
        } else if (element_type->kind == PP_TK_String) {
            /* string indexing yields uint8 */
            element_type = pp_global_type_table_find_alias(cg->gtt, "uint8");
            assert(element_type);
        }
        return pp_code_generator_l_value_to_r_value(cg, element_type, llvm_lval);
    }

    /* Rvalue base (call result, literal, ...): materialize it so the element address can be
     * computed, and release it at the end of the statement. */
    LLVMValueRef base_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->array_list_access.expr);
    assert(base_val);
    const PPType* base_type = pp_type_decay_cg(cg->gtt, object_decoration->resolved_type);
    assert(base_type);
    LLVMTypeRef llvm_base_type = pp_environment_backend_lookup_type(cg, base_type);
    assert(llvm_base_type);
    LLVMValueRef base_slot = pp_code_generator_entry_alloca(cg, llvm_base_type, "index_base_tmp");
    LLVMBuildStore(cg->builder, base_val, base_slot);
    if (pp_type_needs_drop(base_type)) { pp_code_generator_push_temp_drop(cg, base_slot, base_type); }

    LLVMValueRef llvm_index = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->array_list_access.index);
    assert(llvm_index);
    {
        LLVMTypeRef index_llvm_type = LLVMTypeOf(llvm_index);
        if (LLVMGetTypeKind(index_llvm_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(index_llvm_type) < 64) {
            llvm_index = LLVMBuildZExt(cg->builder, llvm_index, LLVMInt64TypeInContext(cg->ctx), "index_zext");
            assert(llvm_index);
        }
    }

    LLVMTypeRef llvm_elem_type = NULL;
    LLVMValueRef element_ptr = NULL;
    const PPType* element_type = NULL;
    if (base_type->kind == PP_TK_Array) {
        element_type = base_type->type.pp_type_array.element_type;
        llvm_elem_type = pp_environment_backend_lookup_type(cg, element_type);
        LLVMValueRef indices[2] = {LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0), llvm_index};
        element_ptr = LLVMBuildGEP2(cg->builder, llvm_base_type, base_slot, indices, 2, "array_idx");
    } else if (base_type->kind == PP_TK_List) {
        element_type = base_type->type.pp_type_list.element_type;
        llvm_elem_type = pp_environment_backend_lookup_type(cg, element_type);
        LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_base_type, base_slot, 0, "list_data_ptr");
        LLVMValueRef data_ptr =
            LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data");
        element_ptr = LLVMBuildGEP2(cg->builder, llvm_elem_type, data_ptr, &llvm_index, 1, "list_elem_ptr");
    } else {
        assert(base_type->kind == PP_TK_String);
        /* string indexing yields uint8 */
        element_type = pp_global_type_table_find_alias(cg->gtt, "uint8");
        assert(element_type);
        llvm_elem_type = LLVMInt8TypeInContext(cg->ctx);
        LLVMValueRef ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_base_type, base_slot, 0, "str_ptr_ptr");
        LLVMValueRef str_data_ptr =
            LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), ptr_ptr, "str_ptr");
        element_ptr = LLVMBuildGEP2(cg->builder, llvm_elem_type, str_data_ptr, &llvm_index, 1, "str_elem_ptr");
    }
    assert(element_ptr != NULL);
    assert(llvm_elem_type != NULL);

    /* The element is read out of a value that is about to be dropped: copy it when it owns memory. */
    if (element_type != NULL && pp_type_needs_drop(element_type) && !pp_type_is_reference(element_type)) {
        LLVMValueRef elem_slot = pp_code_generator_entry_alloca(cg, llvm_elem_type, "index_elem_copy");
        pp_code_generator_emit_copy(cg, elem_slot, element_ptr, element_type);
        return LLVMBuildLoad2(cg->builder, llvm_elem_type, elem_slot, "index_elem");
    }
    return LLVMBuildLoad2(cg->builder, llvm_elem_type, element_ptr, "index_elem");
}

static LLVMValueRef pp_code_generator_generate_lvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    if (cg->debug) { pp_code_generator_debug_set_location(cg, &node->loc); }
    const PP_AST_Decoration* dec = pp_ast_get_decoration(node);
    assert(dec);
    assert(dec->value_category == PP_VALUE_CATEGORY_LVALUE);
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
        case PP_AST_UnaryExpressionNode: {
            if (node->unary_expression.op == PP_TT_DOLLAR) {
                // Dereference lvalue: generate rvalue of pointer operand, result is address of pointee
                LLVMValueRef ptr_val =
                    pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->unary_expression.operand);
                llvm_val = ptr_val;
            } else {
                assert(0 && "Cannot get l-value for unary");
            }
        } break;
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
    assert(!symbol->is_constant_decl); /* constants are rvalues; they have no addressable storage */

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

    // The callee may be a `const func[...]` or `func[...]&` value, so decay first.
    const PPType* callee_type = pp_type_decay_cg(cg->gtt, callee_decoration->resolved_type);
    assert(callee_type);
    assert(callee_type->kind == PP_TK_Function || callee_type->kind == PP_TK_Lambda ||
           callee_type->kind == PP_TK_Operator);

    bool is_lambda = callee_type->kind == PP_TK_Lambda;
    bool is_operator = callee_type->kind == PP_TK_Operator;

    LLVMValueRef llvm_callee_func =
        pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->function_call.callee);
    assert(llvm_callee_func);

    LLVMTypeRef llvm_callee_func_type = pp_code_generator_llvm_subroutine_type(cg, callee_type);
    assert(llvm_callee_func_type);

    size_t param_count = is_lambda     ? callee_type->type.pp_type_lambda.param_count
                         : is_operator ? callee_type->type.pp_type_operator.param_count
                                       : callee_type->type.pp_type_function.param_count;
    LLVMValueRef* ref_temp_addrs = NULL;
    const PPType** ref_temp_types = NULL;
    LLVMValueRef* owning_temp_addrs = NULL;
    const PPType** owning_temp_types = NULL;
    /* A PP-defined callee drops its by-value owning parameters, so an owning argument temporary is
     * moved into it. An extern C callee only borrows the value, so the caller keeps ownership.
     * Indirect callables are treated as PP-defined: leaking is safer than double-freeing. */
    bool callee_takes_ownership = true;
    if (node->function_call.callee->node_type == PP_AST_IdentifierNode) {
        const char* callee_name = node->function_call.callee->identifier.identifier;
        assert(callee_name);
        const PPSymbol* callee_symbol = pp_symbol_table_lookup(symbol_table, callee_name, true);
        assert(callee_symbol);
        if (callee_symbol->is_callable_decl && !callee_symbol->is_definition) { callee_takes_ownership = false; }
    }
    if (param_count > 0) {
        llvm_args = (LLVMValueRef*)calloc(param_count, sizeof(LLVMValueRef));
        NULL_POINTER_ERROR_HANDLE(llvm_args);

        ref_temp_addrs = calloc(param_count, sizeof(LLVMValueRef));
        ref_temp_types = calloc(param_count, sizeof(const PPType*));
        NULL_POINTER_ERROR_HANDLE(ref_temp_addrs);
        NULL_POINTER_ERROR_HANDLE(ref_temp_types);
        owning_temp_addrs = calloc(param_count, sizeof(LLVMValueRef));
        owning_temp_types = calloc(param_count, sizeof(const PPType*));
        NULL_POINTER_ERROR_HANDLE(owning_temp_addrs);
        NULL_POINTER_ERROR_HANDLE(owning_temp_types);

        for (unsigned int i = 0; i < param_count; i++) {
            const PPType* param_type = is_lambda     ? callee_type->type.pp_type_lambda.param_types[i]
                                       : is_operator ? callee_type->type.pp_type_operator.param_types[i]
                                                     : callee_type->type.pp_type_function.param_types[i];
            assert(param_type);

            if (pp_type_is_reference(param_type)) {
                const PP_AST_Decoration* arg_decor = pp_ast_get_decoration(node->function_call.arguments[i]);
                assert(arg_decor);
                bool arg_is_lvalue = arg_decor->value_category == PP_VALUE_CATEGORY_LVALUE;
                if (arg_is_lvalue) {
                    llvm_args[i] =
                        pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
                } else {
                    // Materialize rvalue into a temporary for const reference binding.
                    const PPType* pointee_type = param_type->type.pp_type_reference.reference_to;
                    LLVMTypeRef llvm_pointee_type = pp_environment_backend_lookup_type(cg, pointee_type);
                    assert(llvm_pointee_type);
                    LLVMValueRef tmp_alloca = pp_code_generator_entry_alloca(cg, llvm_pointee_type, "ref_tmp");
                    LLVMValueRef init =
                        pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
                    assert(init);
                    LLVMBuildStore(cg->builder, init, tmp_alloca);
                    llvm_args[i] = tmp_alloca;
                    if (pp_type_needs_drop(pointee_type)) {
                        ref_temp_addrs[i] = tmp_alloca;
                        ref_temp_types[i] = pointee_type;
                    }
                }
            } else if (pp_type_needs_drop(param_type)) {
                const PP_AST_Decoration* arg_decor = pp_ast_get_decoration(node->function_call.arguments[i]);
                assert(arg_decor);
                bool arg_is_lvalue = arg_decor->value_category == PP_VALUE_CATEGORY_LVALUE;
                if (arg_is_lvalue) {
                    // Copy lvalue argument for owning param
                    // Create a temporary alloca for the param value
                    LLVMTypeRef llvm_param_type = pp_environment_backend_lookup_type(cg, param_type);
                    assert(llvm_param_type);
                    LLVMValueRef tmp_alloca = pp_code_generator_entry_alloca(cg, llvm_param_type, "param_tmp");
                    LLVMValueRef src_ptr =
                        pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
                    pp_code_generator_emit_copy(cg, tmp_alloca, src_ptr, param_type);
                    LLVMValueRef tmp_val = LLVMBuildLoad2(cg->builder, llvm_param_type, tmp_alloca, "param_tmp_val");
                    llvm_args[i] = tmp_val;
                } else {
                    // Rvalue owning argument → materialise, use, drop after call
                    LLVMTypeRef llvm_param_type = pp_environment_backend_lookup_type(cg, param_type);
                    assert(llvm_param_type);
                    LLVMValueRef tmp_alloca = pp_code_generator_entry_alloca(cg, llvm_param_type, "arg_tmp");
                    LLVMValueRef init =
                        pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
                    LLVMBuildStore(cg->builder, init, tmp_alloca);
                    LLVMValueRef tmp_val = LLVMBuildLoad2(cg->builder, llvm_param_type, tmp_alloca, "arg_tmp_val");
                    llvm_args[i] = tmp_val;
                    owning_temp_addrs[i] = tmp_alloca;
                    owning_temp_types[i] = param_type;
                }
            } else {
                llvm_args[i] =
                    pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->function_call.arguments[i]);
            }
            assert(llvm_args[i]);
        }
    }

    llvm_val =
        LLVMBuildCall2(cg->builder, llvm_callee_func_type, llvm_callee_func, llvm_args, param_count,
                       LLVMGetReturnType(llvm_callee_func_type) == LLVMVoidTypeInContext(cg->ctx) ? "" : "func_call");

    // Drop owning temporaries materialized for const reference parameters.
    for (unsigned int i = 0; i < param_count; i++) {
        if (ref_temp_addrs[i]) { pp_code_generator_emit_drop(cg, ref_temp_addrs[i], ref_temp_types[i]); }
    }
    // Drop owning rvalue temporaries used as call arguments, but only when the callee cannot take
    // ownership: a PP callee drops its by-value owning parameters, an extern C callee does not.
    if (!callee_takes_ownership) {
        for (unsigned int i = 0; i < param_count; i++) {
            if (owning_temp_addrs[i]) { pp_code_generator_emit_drop(cg, owning_temp_addrs[i], owning_temp_types[i]); }
        }
    }

    free(llvm_args);
    llvm_args = NULL;
    free(ref_temp_addrs);
    ref_temp_addrs = NULL;
    free(ref_temp_types);
    ref_temp_types = NULL;
    free(owning_temp_addrs);
    owning_temp_addrs = NULL;
    free(owning_temp_types);
    owning_temp_types = NULL;
    return llvm_val;

error_cleanup:
    free(llvm_args);
    free(ref_temp_addrs);
    free(ref_temp_types);
    free(owning_temp_addrs);
    free(owning_temp_types);
    return NULL;
}

static LLVMValueRef pp_code_generator_generate_rvalue(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                      PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    if (cg->debug) { pp_code_generator_debug_set_location(cg, &node->loc); }
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
        case PP_AST_NullLiteralNode:
            llvm_val = pp_code_generator_generate_rvalue_null_literal(cg, symbol_table, env_b, node);
            break;
        case PP_AST_FunctionCallNode:
            llvm_val = pp_code_generator_generate_rvalue_function_call(cg, symbol_table, env_b, node);
            break;
        case PP_AST_ListPushNode:
            llvm_val = pp_code_generator_generate_rvalue_list_push(cg, symbol_table, env_b, node);
            break;
        case PP_AST_ListPopNode:
            llvm_val = pp_code_generator_generate_rvalue_list_pop(cg, symbol_table, env_b, node);
            break;
        case PP_AST_ListClearNode:
            llvm_val = pp_code_generator_generate_rvalue_list_clear(cg, symbol_table, env_b, node);
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
        case PP_AST_QualifiedConstantNode:
            llvm_val = pp_code_generator_generate_rvalue_qualified_constant(cg, node);
            break;
        default:
            assert(0 && "Cannot get r-value");
    }

    return llvm_val;
}

/* Builtin string equality. Strings are {ptr, len} structs, so equality is a length check plus memcmp over the
 * payload. memcmp is only called when both lengths are non-zero and equal, which keeps {NULL, 0} strings (zero
 * initialised or from malloc(0)) out of libc, where memcmp(NULL, NULL, 0) would be undefined behaviour.
 * Returns an i1 equality result; the caller inverts it for '!='. */
static LLVMValueRef pp_code_generator_generate_string_equality(PP_CodeGenerator* cg, LLVMValueRef llvm_lhs,
                                                               LLVMValueRef llvm_rhs) {
    assert(cg);
    assert(llvm_lhs);
    assert(llvm_rhs);

    LLVMContextRef ctx = cg->ctx;
    LLVMBuilderRef builder = cg->builder;
    LLVMTypeRef i1_type = LLVMInt1TypeInContext(ctx);
    LLVMTypeRef i32_type = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef i8ptr_type = LLVMPointerTypeInContext(ctx, 0);

    LLVMValueRef lhs_ptr = LLVMBuildExtractValue(builder, llvm_lhs, 0, "str_lhs_ptr");
    LLVMValueRef lhs_len = LLVMBuildExtractValue(builder, llvm_lhs, 1, "str_lhs_len");
    LLVMValueRef rhs_ptr = LLVMBuildExtractValue(builder, llvm_rhs, 0, "str_rhs_ptr");
    LLVMValueRef rhs_len = LLVMBuildExtractValue(builder, llvm_rhs, 1, "str_rhs_len");

    LLVMBasicBlockRef lhs_block = LLVMGetInsertBlock(builder);
    assert(lhs_block);
    LLVMValueRef llvm_func = LLVMGetBasicBlockParent(lhs_block);
    assert(llvm_func);
    LLVMBasicBlockRef len_eq_block = LLVMAppendBasicBlockInContext(ctx, llvm_func, "str.eq.leneq");
    LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(ctx, llvm_func, "str.eq.body");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(ctx, llvm_func, "str.eq.end");

    // Different lengths are unequal; only equal lengths need the empty check and the byte comparison.
    LLVMValueRef len_eq = LLVMBuildICmp(builder, LLVMIntEQ, lhs_len, rhs_len, "str_len_eq");
    LLVMBuildCondBr(builder, len_eq, len_eq_block, merge_block);

    // Equal lengths: empty strings compare equal without touching memcmp.
    LLVMPositionBuilderAtEnd(builder, len_eq_block);
    LLVMValueRef is_empty = LLVMBuildICmp(builder, LLVMIntEQ, lhs_len, LLVMConstInt(i64_type, 0, 0), "str_len_zero");
    LLVMBuildCondBr(builder, is_empty, merge_block, body_block);

    // Both non-empty: length-aware byte comparison (NUL-safe, unlike strcmp).
    LLVMPositionBuilderAtEnd(builder, body_block);
    LLVMValueRef memcmp_fn = LLVMGetNamedFunction(cg->mod, "memcmp");
    assert(memcmp_fn);
    LLVMTypeRef memcmp_params[3] = {i8ptr_type, i8ptr_type, i64_type};
    LLVMTypeRef memcmp_type = LLVMFunctionType(i32_type, memcmp_params, 3, 0);
    LLVMValueRef memcmp_args[3] = {lhs_ptr, rhs_ptr, lhs_len};
    LLVMValueRef cmp = LLVMBuildCall2(builder, memcmp_type, memcmp_fn, memcmp_args, 3, "str_memcmp");
    LLVMValueRef bytes_eq = LLVMBuildICmp(builder, LLVMIntEQ, cmp, LLVMConstInt(i32_type, 0, 0), "str_bytes_eq");
    LLVMBasicBlockRef body_end_block = LLVMGetInsertBlock(builder);
    LLVMBuildBr(builder, merge_block);

    LLVMPositionBuilderAtEnd(builder, merge_block);
    LLVMValueRef phi = LLVMBuildPhi(builder, i1_type, "str_eq");
    LLVMValueRef incoming_vals[3] = {LLVMConstInt(i1_type, 0, 0), is_empty, bytes_eq};
    LLVMBasicBlockRef incoming_blocks[3] = {lhs_block, len_eq_block, body_end_block};
    LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 3);

    return phi;
}

static LLVMValueRef pp_code_generator_generate_rvalue_binary(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                             PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_BinaryExpressionNode);

    LLVMValueRef llvm_val = NULL;
    LLVMValueRef llvm_args[2] = {0};
    LLVMValueRef ref_temp_addrs[2] = {0};
    const PPType* ref_temp_types[2] = {0};

    const PP_AST_Decoration* bin_decor = pp_ast_get_decoration(node);
    assert(bin_decor);
    if (bin_decor->resolved_operator_callable) {
        const PPSymbol* oc = bin_decor->resolved_operator_callable;
        assert(oc);
        const PPType* op_type = oc->type;
        assert(op_type && op_type->kind == PP_TK_Operator);

        LLVMValueRef llvm_func = pp_environment_backend_lookup_val(env_b, oc, true);
        assert(llvm_func);
        LLVMTypeRef llvm_func_type = pp_code_generator_llvm_subroutine_type(cg, op_type);
        assert(llvm_func_type);

        size_t param_count = op_type->type.pp_type_operator.param_count;
        const PP_AST_Node* args_nodes[2] = {node->binary_expression.left, node->binary_expression.right};
        if (param_count > 0) {
            for (unsigned int i = 0; i < param_count; i++) {
                const PPType* param_type = op_type->type.pp_type_operator.param_types[i];
                assert(param_type);
                if (pp_type_is_reference(param_type)) {
                    const PP_AST_Decoration* arg_decor = pp_ast_get_decoration(args_nodes[i]);
                    assert(arg_decor);
                    bool arg_is_lvalue = arg_decor->value_category == PP_VALUE_CATEGORY_LVALUE;
                    if (arg_is_lvalue) {
                        llvm_args[i] = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, args_nodes[i]);
                    } else {
                        const PPType* pointee_type = param_type->type.pp_type_reference.reference_to;
                        LLVMTypeRef llvm_pointee_type = pp_environment_backend_lookup_type(cg, pointee_type);
                        assert(llvm_pointee_type);
                        LLVMValueRef tmp_alloca = pp_code_generator_entry_alloca(cg, llvm_pointee_type, "ref_tmp");
                        LLVMValueRef init = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, args_nodes[i]);
                        assert(init);
                        LLVMBuildStore(cg->builder, init, tmp_alloca);
                        llvm_args[i] = tmp_alloca;
                        if (pp_type_needs_drop(pointee_type)) {
                            ref_temp_addrs[i] = tmp_alloca;
                            ref_temp_types[i] = pointee_type;
                        }
                    }
                } else {
                    llvm_args[i] = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, args_nodes[i]);
                }
                assert(llvm_args[i]);
            }
        }

        llvm_val = LLVMBuildCall2(cg->builder, llvm_func_type, llvm_func, llvm_args, param_count,
                                  LLVMGetReturnType(llvm_func_type) == LLVMVoidTypeInContext(cg->ctx) ? "" : "op_call");

        for (unsigned int i = 0; i < param_count; i++) {
            if (ref_temp_addrs[i]) { pp_code_generator_emit_drop(cg, ref_temp_addrs[i], ref_temp_types[i]); }
        }

        const PPType* return_type = bin_decor->resolved_type;
        if (pp_type_is_reference(return_type)) {
            llvm_val = pp_code_generator_l_value_to_r_value(cg, return_type, llvm_val);
        }
        return llvm_val;
    }

    /* Short-circuit logical operators need control flow, handle them before eager RHS generation */
    if (cg->current_function_type) {
        if (node->binary_expression.op == PP_TT_AMPERSAND_AMPERSAND || node->binary_expression.op == PP_TT_BAR_BAR) {
            LLVMValueRef llvm_lhs =
                pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->binary_expression.left);
            assert(llvm_lhs);
            const PP_AST_Decoration* lhs_decoration = pp_ast_get_decoration(node->binary_expression.left);
            assert(lhs_decoration);
            const PPType* lhs_dec_type = pp_type_decay_cg(cg->gtt, lhs_decoration->resolved_type);
            assert(lhs_dec_type);
            LLVMTypeRef llvm_lhs_type = pp_environment_backend_lookup_type(cg, lhs_dec_type);
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
            pp_code_generator_ensure_terminated(cg, merge_block);
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

    LLVMValueRef llvm_lhs =
        pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, node->binary_expression.left);
    assert(llvm_lhs);

    LLVMValueRef llvm_rhs =
        pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, node->binary_expression.right);
    assert(llvm_rhs);

    /* ---- Type information ---- */

    const PP_AST_Decoration* lhs_decoration = pp_ast_get_decoration(node->binary_expression.left);
    assert(lhs_decoration);
    const PP_AST_Decoration* rhs_decoration = pp_ast_get_decoration(node->binary_expression.right);
    assert(rhs_decoration);

    const PPType* lhs_type = pp_type_decay_cg(cg->gtt, lhs_decoration->resolved_type);
    assert(lhs_type);
    const PPType* rhs_type = pp_type_decay_cg(cg->gtt, rhs_decoration->resolved_type);
    assert(rhs_type);

    LLVMTypeRef llvm_lhs_type = pp_environment_backend_lookup_type(cg, lhs_type);
    assert(llvm_lhs_type);
    LLVMTypeRef llvm_rhs_type = pp_environment_backend_lookup_type(cg, rhs_type);
    assert(llvm_rhs_type);

    LLVMTypeKind llvm_lhs_type_kind = LLVMGetTypeKind(llvm_lhs_type);
    assert(llvm_lhs_type_kind);
    LLVMTypeKind llvm_rhs_type_kind = LLVMGetTypeKind(llvm_rhs_type);
    assert(llvm_rhs_type_kind);

    switch (node->binary_expression.op) {
        case PP_TT_PLUS:
            if (lhs_type->kind == PP_TK_String && rhs_type->kind == PP_TK_String) {
                /* String concatenation */
                LLVMValueRef lhs_ptr = LLVMBuildExtractValue(cg->builder, llvm_lhs, 0, "str_lhs_ptr");
                LLVMValueRef lhs_len = LLVMBuildExtractValue(cg->builder, llvm_lhs, 1, "str_lhs_len");
                LLVMValueRef rhs_ptr = LLVMBuildExtractValue(cg->builder, llvm_rhs, 0, "str_rhs_ptr");
                LLVMValueRef rhs_len = LLVMBuildExtractValue(cg->builder, llvm_rhs, 1, "str_rhs_len");
                LLVMTypeRef i64_ty = LLVMInt64TypeInContext(cg->ctx);
                LLVMValueRef total_len = LLVMBuildAdd(cg->builder, lhs_len, rhs_len, "str_total_len");
                LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
                assert(malloc_fn);
                LLVMTypeRef malloc_type = LLVMFunctionType(LLVMPointerTypeInContext(cg->ctx, 0), &i64_ty, 1, 0);
                LLVMValueRef raw_ptr =
                    LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &total_len, 1, "str_concat_malloc");
                LLVMValueRef offset_ptr = LLVMBuildGEP2(cg->builder, LLVMInt8TypeInContext(cg->ctx), raw_ptr, &lhs_len,
                                                        1, "str_concat_offset");
                pp_code_generator_emit_memcpy(cg, raw_ptr, lhs_ptr, lhs_len, "str_concat_memcpy1");
                pp_code_generator_emit_memcpy(cg, offset_ptr, rhs_ptr, rhs_len, "str_concat_memcpy2");
                LLVMTypeRef str_struct_ty = pp_environment_backend_lookup_type(cg, lhs_type);
                LLVMValueRef undef = LLVMGetUndef(str_struct_ty);
                LLVMValueRef ptr_field = LLVMBuildInsertValue(cg->builder, undef, raw_ptr, 0, "str_concat_ptr");
                llvm_val = LLVMBuildInsertValue(cg->builder, ptr_field, total_len, 1, "str_concat");
            } else if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
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
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
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
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
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
        case PP_TT_LESS_LESS:
        case PP_TT_GREATER_GREATER:
            if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
                    // Left-shift overflow defined.
                    unsigned int bits = 0;
                    switch (lhs_decayed_type->kind) {
                        case PP_TK_UInt8:
                            bits = 8;
                            break;
                        case PP_TK_UInt16:
                            bits = 16;
                            break;
                        case PP_TK_UInt32:
                            bits = 32;
                            break;
                        case PP_TK_UInt64:
                            bits = 64;
                            break;
                        case PP_TK_UInt128:
                            bits = 128;
                            break;
                        default:
                            abort();
                    }
                    LLVMTypeRef rhs_type = LLVMTypeOf(llvm_rhs);
                    LLVMValueRef shift_limit = LLVMConstInt(rhs_type, (unsigned long long)bits, false);
                    LLVMValueRef cmp_ge = LLVMBuildICmp(cg->builder, LLVMIntUGE, llvm_rhs, shift_limit, "sh_ge");
                    LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(llvm_lhs));
                    LLVMValueRef shl_val;
                    if (node->binary_expression.op == PP_TT_LESS_LESS) {
                        shl_val = LLVMBuildShl(cg->builder, llvm_lhs, llvm_rhs, "shltmp");
                    } else {
                        shl_val = LLVMBuildLShr(cg->builder, llvm_lhs, llvm_rhs, "shrttmp");
                    }
                    llvm_val = LLVMBuildSelect(cg->builder, cmp_ge, zero, shl_val, "shl_sel");
                }
            }
            break;
        case PP_TT_GREATER:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealOGT, llvm_lhs, llvm_rhs, "fgttmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                const PPType* lhs_decayed_type =
                    pp_type_decay_cg(cg->gtt, pp_ast_get_decoration(node->binary_expression.left)->resolved_type);
                assert(lhs_decayed_type);
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
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
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
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
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
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
                if (pp_type_is_unsigned_integer(lhs_decayed_type)) {
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
            } else if (llvm_lhs_type_kind == LLVMPointerTypeKind && llvm_rhs_type_kind == LLVMPointerTypeKind) {
                llvm_val = LLVMBuildICmp(cg->builder, LLVMIntEQ, llvm_lhs, llvm_rhs, "eqtmp");
            } else if (lhs_type->kind == PP_TK_String && rhs_type->kind == PP_TK_String) {
                llvm_val = pp_code_generator_generate_string_equality(cg, llvm_lhs, llvm_rhs);
            }
            break;
        case PP_TT_EXCLAMATION_EQUAL:
            if (llvm_lhs_type_kind == LLVMFloatTypeKind && llvm_rhs_type_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFCmp(cg->builder, LLVMRealONE, llvm_lhs, llvm_rhs, "fneqtmp");
            } else if (llvm_lhs_type_kind == LLVMIntegerTypeKind && llvm_rhs_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildICmp(cg->builder, LLVMIntNE, llvm_lhs, llvm_rhs, "neqtmp");
            } else if (llvm_lhs_type_kind == LLVMPointerTypeKind && llvm_rhs_type_kind == LLVMPointerTypeKind) {
                llvm_val = LLVMBuildICmp(cg->builder, LLVMIntNE, llvm_lhs, llvm_rhs, "neqtmp");
            } else if (lhs_type->kind == PP_TK_String && rhs_type->kind == PP_TK_String) {
                LLVMValueRef eq = pp_code_generator_generate_string_equality(cg, llvm_lhs, llvm_rhs);
                llvm_val = LLVMBuildXor(cg->builder, eq, LLVMConstInt(LLVMInt1TypeInContext(cg->ctx), 1, 0), "str_ne");
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

    const PP_AST_Decoration* un_decor = pp_ast_get_decoration(node);
    assert(un_decor);
    if (un_decor->resolved_operator_callable) {
        const PPSymbol* oc = un_decor->resolved_operator_callable;
        assert(oc);
        const PPType* op_type = oc->type;
        assert(op_type && op_type->kind == PP_TK_Operator);
        LLVMValueRef llvm_func = pp_environment_backend_lookup_val(env_b, oc, true);
        assert(llvm_func);
        LLVMTypeRef llvm_func_type = pp_code_generator_llvm_subroutine_type(cg, op_type);
        assert(llvm_func_type);
        size_t param_count = op_type->type.pp_type_operator.param_count;
        const PP_AST_Node* args_nodes[1] = {node->unary_expression.operand};
        LLVMValueRef* llvm_args = (LLVMValueRef*)calloc(param_count ? param_count : 1, sizeof(LLVMValueRef));
        LLVMValueRef* ref_temp_addrs = calloc(param_count ? param_count : 1, sizeof(LLVMValueRef));
        const PPType** ref_temp_types = calloc(param_count ? param_count : 1, sizeof(const PPType*));
        assert(llvm_args && ref_temp_addrs && ref_temp_types);
        for (unsigned int i = 0; i < param_count; i++) {
            const PPType* param_type = op_type->type.pp_type_operator.param_types[i];
            assert(param_type);
            if (pp_type_is_reference(param_type)) {
                const PP_AST_Decoration* arg_decor = pp_ast_get_decoration(args_nodes[i]);
                assert(arg_decor);
                bool arg_is_lvalue = arg_decor->value_category == PP_VALUE_CATEGORY_LVALUE;
                if (arg_is_lvalue) {
                    llvm_args[i] = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, args_nodes[i]);
                } else {
                    const PPType* pointee_type = param_type->type.pp_type_reference.reference_to;
                    LLVMTypeRef llvm_pointee_type = pp_environment_backend_lookup_type(cg, pointee_type);
                    assert(llvm_pointee_type);
                    LLVMValueRef tmp_alloca = pp_code_generator_entry_alloca(cg, llvm_pointee_type, "ref_tmp");
                    LLVMValueRef init = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, args_nodes[i]);
                    assert(init);
                    LLVMBuildStore(cg->builder, init, tmp_alloca);
                    llvm_args[i] = tmp_alloca;
                    if (pp_type_needs_drop(pointee_type)) {
                        ref_temp_addrs[i] = tmp_alloca;
                        ref_temp_types[i] = pointee_type;
                    }
                }
            } else {
                llvm_args[i] = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, args_nodes[i]);
            }
        }
        LLVMValueRef result = LLVMBuildCall2(cg->builder, llvm_func_type, llvm_func, llvm_args, param_count, "optmp");
        for (unsigned int i = 0; i < param_count; i++) {
            if (ref_temp_addrs[i] && ref_temp_types[i] && pp_type_needs_drop(ref_temp_types[i])) {
                pp_code_generator_emit_drop(cg, ref_temp_addrs[i], ref_temp_types[i]);
            }
        }
        free(llvm_args);
        free(ref_temp_addrs);
        free(ref_temp_types);
        return result;
    }

    LLVMValueRef llvm_val = NULL;

    LLVMValueRef llvm_operand =
        pp_code_generator_generate_rvalue_disposable(cg, symbol_table, env_b, node->unary_expression.operand);
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
                    const LLVMValueRef zero_const = LLVMConstNull(llvm_operand_type);
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
        case PP_TT_HASH: {
            const PP_AST_Decoration* op_dec = pp_ast_get_decoration(node->unary_expression.operand);
            assert(op_dec);
            const PPType* op_type = pp_type_decay_cg(cg->gtt, op_dec->resolved_type);
            assert(op_type);
            if (op_type->kind == PP_TK_Array) {
                size_t sz = op_type->type.pp_type_array.size;
                llvm_val = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), sz, false);
            } else if (op_type->kind == PP_TK_List) {
                // List value is a struct { data_ptr, len, cap }
                LLVMValueRef len_val = LLVMBuildExtractValue(cg->builder, llvm_operand, 1, "list_len");
                // List len is i64 internally, truncate to uint32 as per PP type
                llvm_val = LLVMBuildTrunc(cg->builder, len_val, LLVMInt32TypeInContext(cg->ctx), "len_u32");
            } else if (op_type->kind == PP_TK_String) {
                // String value is a struct { data_ptr, len }
                LLVMValueRef len_val = LLVMBuildExtractValue(cg->builder, llvm_operand, 1, "str_len");
                // String len is i64 internally, truncate to uint32 as per PP type
                llvm_val = LLVMBuildTrunc(cg->builder, len_val, LLVMInt32TypeInContext(cg->ctx), "len_u32");
            } else {
                assert(0 && "Unary '#' requires array, list, or string");
            }
            break;
        }
        case PP_TT_AT: {
            const PP_AST_Node* operand_node = node->unary_expression.operand;
            const PP_AST_Decoration* dec = pp_ast_get_decoration(operand_node);
            assert(dec && dec->value_category == PP_VALUE_CATEGORY_LVALUE);
            llvm_val = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, operand_node);
            break;
        }
        case PP_TT_TILDE: {
            if (llvm_operand_type_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildNot(cg->builder, llvm_operand, "tilttmp");
            } else {
                assert(0 && "Unary '~' requires an integer or flag operand");
            }
            break;
        }
        case PP_TT_DOLLAR: {
            const PP_AST_Decoration* op_dec = pp_ast_get_decoration(node->unary_expression.operand);
            assert(op_dec);
            const PPType* op_type = pp_type_decay_cg(cg->gtt, op_dec->resolved_type);
            assert(op_type);
            const PPType* pointee_type = NULL;
            if (op_type->kind == PP_TK_Pointer) {
                pointee_type = op_type->type.pp_type_pointer.pointee;
            } else if (op_type->kind == PP_TK_Reference) {
                pointee_type = op_type->type.pp_type_reference.reference_to;
            } else {
                assert(0 && "Dereference requires pointer type");
            }
            LLVMTypeRef pointee_llvm_type = pp_environment_backend_lookup_type(cg, pointee_type);
            assert(pointee_llvm_type);
            llvm_val = LLVMBuildLoad2(cg->builder, pointee_llvm_type, llvm_operand, "deref_tmp");
            break;
        }
        default:
            assert(0 && "Unary expression failure");
            return NULL;
    }

    return llvm_val;
}

/* Widen/narrow an integer value preserving the signedness of the PP source type (enums and flags use the
 * signedness of their underlying type). LLVMBuildIntCast would sign-extend unsigned sources. */
static LLVMValueRef pp_code_generator_build_int_cast(PP_CodeGenerator* cg, LLVMValueRef value, LLVMTypeRef dest_type,
                                                     const PPType* src_type) {
    assert(cg);
    assert(value);
    assert(dest_type);
    assert(src_type);

    const PPType* integer_type = src_type;
    if (integer_type->kind == PP_TK_Newtype) {
        integer_type = integer_type->type.pp_type_newtype.underlying;
        assert(integer_type);
    }

    LLVMTypeRef src_llvm_type = LLVMTypeOf(value);
    const unsigned int src_bits = LLVMGetIntTypeWidth(src_llvm_type);
    const unsigned int dest_bits = LLVMGetIntTypeWidth(dest_type);

    if (dest_bits < src_bits) { return LLVMBuildTrunc(cg->builder, value, dest_type, "intcast_trunc"); }
    if (dest_bits > src_bits) {
        if (pp_type_kind_is_unsigned(integer_type->kind)) {
            return LLVMBuildZExt(cg->builder, value, dest_type, "intcast_zext");
        }
        return LLVMBuildSExt(cg->builder, value, dest_type, "intcast_sext");
    }
    return value;
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

    const PP_AST_Decoration* expr_decoration = pp_ast_get_decoration(expr_node);
    assert(expr_decoration);
    const PP_AST_Decoration* cast_decoration = pp_ast_get_decoration(node);
    assert(cast_decoration);

    const PPType* src_orig = expr_decoration->resolved_type;
    const PPType* dest_orig = cast_decoration->resolved_type;
    assert(src_orig);
    assert(dest_orig);

    const PPType* src_type = pp_type_decay_cg(cg->gtt, src_orig);
    assert(src_type);
    const PPType* dest_type = pp_type_decay_cg(cg->gtt, dest_orig);
    assert(dest_type);

    LLVMTypeRef llvm_src_type = pp_environment_backend_lookup_type(cg, src_type);
    assert(llvm_src_type);
    LLVMTypeRef llvm_dest_type = pp_environment_backend_lookup_type(cg, dest_type);
    assert(llvm_dest_type);

    if (llvm_src_type == llvm_dest_type) {
        LLVMValueRef llvm_val_to_cast = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, expr_node);
        assert(llvm_val_to_cast);
        llvm_val = llvm_val_to_cast;
    } else {
        // Handle pointer casts first based on original types
        if (src_orig->kind == PP_TK_Reference && dest_orig->kind == PP_TK_Pointer) {
            LLVMValueRef llvm_lval = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, expr_node);
            assert(llvm_lval);
            llvm_val = LLVMBuildBitCast(cg->builder, llvm_lval, llvm_dest_type, "ptr_cast_ref_to_ptr");
        } else if (src_type->kind == PP_TK_Pointer && dest_type->kind == PP_TK_Pointer) {
            LLVMValueRef llvm_val_to_cast = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, expr_node);
            assert(llvm_val_to_cast);
            llvm_val = LLVMBuildBitCast(cg->builder, llvm_val_to_cast, llvm_dest_type, "ptr_cast_ptr_to_ptr");
        } else if (pp_type_is_numerical(src_type) && dest_type->kind == PP_TK_Pointer) {
            // numeric -> pointer: normalise to i64 then inttoptr
            LLVMValueRef llvm_val_to_cast = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, expr_node);
            assert(llvm_val_to_cast);
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMValueRef val = llvm_val_to_cast;
            LLVMTypeKind src_kind = LLVMGetTypeKind(llvm_src_type);
            if (src_kind == LLVMFloatTypeKind) {
                val = LLVMBuildFPToSI(cg->builder, val, i64_type, "num_to_i64_fp");
            } else if (llvm_src_type != i64_type) {
                val = pp_code_generator_build_int_cast(cg, val, i64_type, src_type);
            }
            llvm_val = LLVMBuildIntToPtr(cg->builder, val, llvm_dest_type, "inttoptr_tmp");
        } else if (src_type->kind == PP_TK_Pointer && pp_type_is_numerical(dest_type)) {
            // pointer -> numeric: ptrtoint to i64 then cast to dest numeric
            LLVMValueRef llvm_val_to_cast = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, expr_node);
            assert(llvm_val_to_cast);
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMValueRef val = LLVMBuildPtrToInt(cg->builder, llvm_val_to_cast, i64_type, "ptrtoint_tmp");
            if (llvm_dest_type != i64_type) {
                LLVMTypeKind dest_kind = LLVMGetTypeKind(llvm_dest_type);
                if (dest_kind == LLVMIntegerTypeKind) {
                    val = LLVMBuildIntCast(cg->builder, val, llvm_dest_type, "i64_to_num");
                } else if (dest_kind == LLVMFloatTypeKind) {
                    val = LLVMBuildSIToFP(cg->builder, val, llvm_dest_type, "i64_to_fp");
                }
            }
            llvm_val = val;
        } else {
            LLVMValueRef llvm_val_to_cast = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, expr_node);
            assert(llvm_val_to_cast);
            LLVMTypeKind src_kind = LLVMGetTypeKind(llvm_src_type);
            LLVMTypeKind dest_kind = LLVMGetTypeKind(llvm_dest_type);

            if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildSIToFP(cg->builder, llvm_val_to_cast, llvm_dest_type, "sitofp_tmp");
            } else if (src_kind == LLVMFloatTypeKind && dest_kind == LLVMIntegerTypeKind) {
                llvm_val = LLVMBuildFPToSI(cg->builder, llvm_val_to_cast, llvm_dest_type, "fptosi_tmp");
            } else if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMIntegerTypeKind) {
                llvm_val = pp_code_generator_build_int_cast(cg, llvm_val_to_cast, llvm_dest_type, src_type);
            } else if (src_kind == LLVMFloatTypeKind && dest_kind == LLVMFloatTypeKind) {
                llvm_val = LLVMBuildFPCast(cg->builder, llvm_val_to_cast, llvm_dest_type, "fpcast_tmp");
            } else {
                abort();
            }
        }
    }

    return llvm_val;
}

/* Helper to unescape a PP string lexeme into a byte buffer.
 * Returns number of bytes written on success, -1 on error.
 * The buffer must be large enough for the unescaped content.
 */
static ssize_t pp_codegen_unescape_lexeme_to_buf(const char* lexeme, unsigned char* buf, size_t bufsize) {
    if (!lexeme || !buf) return -1;
    size_t i = 0;
    size_t len = strlen(lexeme);
    size_t out = 0;
    if (len >= 2 && lexeme[0] == '@' && lexeme[1] == '"') {
        i = 2;
    } else if (len >= 2 && lexeme[0] == '"') {
        i = 1;
    } else {
        return -1;
    }
    while (i < len) {
        char c = lexeme[i];
        if (c == '"') { break; }
        if (c == '\\') {
            if (i + 1 >= len) return -1;
            char esc = lexeme[i + 1];
            unsigned char value = 0;
            switch (esc) {
                case 'n':
                    value = '\n';
                    break;
                case 't':
                    value = '\t';
                    break;
                case 'r':
                    value = '\r';
                    break;
                case '"':
                    value = '"';
                    break;
                case '\\':
                    value = '\\';
                    break;
                case '0':
                    value = '\0';
                    break;
                case 'x': {
                    if (i + 3 >= len) return -1;
                    char h1 = lexeme[i + 2];
                    char h2 = lexeme[i + 3];
                    if (!isxdigit((unsigned char)h1) || !isxdigit((unsigned char)h2)) return -1;
                    char hex[3] = {h1, h2, 0};
                    value = (unsigned char)strtoul(hex, NULL, 16);
                    if (out >= bufsize) return -1;
                    buf[out++] = value;
                    i += 4;
                    continue;
                }
                default:
                    return -1;
            }
            if (out >= bufsize) return -1;
            buf[out++] = value;
            i += 2;
            continue;
        }
        if (out >= bufsize) return -1;
        buf[out++] = (unsigned char)c;
        i += 1;
    }
    return (ssize_t)out;
}

/* Helper to materialise a static string literal as an LLVM constant array of i8 with trailing NUL. */
static LLVMValueRef pp_codegen_static_string_literal_to_llvm_constant(LLVMContextRef ctx, const char* lexeme) {
    if (!lexeme) return NULL;
    size_t lex_len = strlen(lexeme);
    /* Unescaped length cannot exceed lexeme length */
    unsigned char* buf = (unsigned char*)malloc(lex_len + 1);
    if (!buf) return NULL;
    ssize_t n = pp_codegen_unescape_lexeme_to_buf(lexeme, buf, lex_len + 1);
    if (n < 0) {
        free(buf);
        return NULL;
    }
    size_t total = (size_t)n + 1; /* implicit NUL */
    buf[n] = '\0';
    LLVMValueRef* vals = (LLVMValueRef*)calloc(total, sizeof(LLVMValueRef));
    if (!vals) {
        free(buf);
        return NULL;
    }
    LLVMTypeRef i8_ty = LLVMInt8TypeInContext(ctx);
    for (size_t i = 0; i < total; i++) { vals[i] = LLVMConstInt(i8_ty, (unsigned long)buf[i], true); }
    LLVMValueRef arr = LLVMConstArray(i8_ty, vals, total);
    free(vals);
    free(buf);
    return arr;
}

/* Helper to materialise a heap string literal at runtime */
static LLVMValueRef pp_codegen_string_literal_to_llvm_value(PP_CodeGenerator* cg, const char* lexeme) {
    if (!lexeme) return NULL;
    /* Ensure we are inside a function */
    assert(cg->current_function_type);
    // Heap string literals cannot be used at global scope
    size_t lex_len = strlen(lexeme);
    unsigned char* buf = (unsigned char*)malloc(lex_len + 1);
    if (!buf) return NULL;
    ssize_t n = pp_codegen_unescape_lexeme_to_buf(lexeme, buf, lex_len + 1);
    if (n < 0) {
        free(buf);
        return NULL;
    }
    size_t len = (size_t)n;

    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(cg->ctx);
    LLVMTypeRef i8ptr_ty = LLVMPointerTypeInContext(cg->ctx, 0);

    LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
    assert(malloc_fn);
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr_ty, &i64_ty, 1, 0);
    LLVMValueRef len_val = LLVMConstInt(i64_ty, len, 0);
    LLVMValueRef raw_ptr;
    if (len == 0) {
        raw_ptr = LLVMConstNull(i8ptr_ty);
    } else {
        raw_ptr = LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &len_val, 1, "str_malloc");
    }

    LLVMValueRef data_global = NULL;
    if (len > 0) {
        /* Create a private constant global for the literal bytes */
        char name[64];
        snprintf(name, sizeof(name), ".str.lit.%d", cg->str_lit_counter);
        cg->str_lit_counter++;
        LLVMTypeRef i8_ty = LLVMInt8TypeInContext(cg->ctx);
        LLVMValueRef* data_vals = (LLVMValueRef*)calloc(len, sizeof(LLVMValueRef));
        for (size_t i = 0; i < len; i++) { data_vals[i] = LLVMConstInt(i8_ty, (unsigned long)buf[i], true); }
        LLVMValueRef data_arr = LLVMConstArray(i8_ty, data_vals, len);
        free(data_vals);
        LLVMTypeRef arr_ty = LLVMArrayType(i8_ty, (unsigned)len);
        LLVMValueRef global = LLVMAddGlobal(cg->mod, arr_ty, name);
        LLVMSetInitializer(global, data_arr);
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(global, 1);
        data_global = global;
    }

    if (len > 0) {
        /* data_global is a pointer to the array; GEP to get i8* */
        LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0);
        LLVMValueRef indices[2] = {zero, zero};
        LLVMValueRef data_ptr = LLVMBuildGEP2(cg->builder, LLVMArrayType(LLVMInt8TypeInContext(cg->ctx), (unsigned)len),
                                              data_global, indices, 2, "str_data_ptr");
        pp_code_generator_emit_memcpy(cg, raw_ptr, data_ptr, len_val, "str_memcpy");
    } else {
        /* len == 0: raw_ptr may be null, no memcpy needed */
    }

    /* Build pp.string struct { ptr, len } */
    LLVMTypeRef str_struct_ty = pp_backend_lower_type(cg, &(PPType){.kind = PP_TK_String});
    LLVMValueRef undef = LLVMGetUndef(str_struct_ty);
    LLVMValueRef ptr_field = LLVMBuildInsertValue(cg->builder, undef, raw_ptr, 0, "str_ptr");
    LLVMValueRef str_val = LLVMBuildInsertValue(cg->builder, ptr_field, len_val, 1, "str_len");

    free(buf);
    return str_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_qualified_constant(PP_CodeGenerator* cg,
                                                                         const PP_AST_Node* node) {
    assert(cg);
    assert(node);
    assert(node->node_type == PP_AST_QualifiedConstantNode);

    const PP_AST_Decoration* dec = pp_ast_get_decoration(node);
    assert(dec && dec->is_enum_constant);
    assert(dec->resolved_type);

    LLVMTypeRef llvm_type = pp_backend_lower_type(cg, dec->resolved_type);
    assert(llvm_type);
    assert(LLVMGetTypeKind(llvm_type) == LLVMIntegerTypeKind);

    return LLVMConstInt(llvm_type, dec->enum_constant_value, false);
}

static LLVMValueRef pp_code_generator_generate_rvalue_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                              PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    LLVMValueRef llvm_val = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);

    const PP_AST_Decoration* decor = pp_ast_get_decoration(node);
    assert(decor);
    const PPType* lit_type = decor->resolved_type;
    assert(lit_type);

    switch (node->literal.literal_type) {
        case PP_TT_INTEGER_LITERAL: {
            const char* lex = node->literal.lexeme;
            char numbuf[PP_INT_LITERAL_NUMBUF_SIZE] = {0};
            pp_type_parse_int_literal_suffix(lex, numbuf);
            LLVMTypeRef llvm_ty = pp_backend_lower_type(cg, lit_type);
            assert(llvm_ty);
            /* Detect base for LLVMConstIntOfString and strip 0x prefix if present */
            unsigned radix = 10;
            char* num_for_llvm = numbuf;
            static char stripped[PP_INT_LITERAL_NUMBUF_SIZE];
            if (numbuf[0] == '0' && (numbuf[1] == 'x' || numbuf[1] == 'X')) {
                radix = 16;
                /* strip 0x prefix */
                size_t len = strlen(numbuf);
                if (len > 2) {
                    memcpy(stripped, numbuf + 2, len - 2);
                    stripped[len - 2] = '\0';
                    num_for_llvm = stripped;
                } else {
                    num_for_llvm = "0";
                }
            }
            llvm_val = LLVMConstIntOfString(llvm_ty, num_for_llvm, radix);
        } break;
        case PP_TT_CHAR_LITERAL: {
            const char* lex = node->literal.lexeme;
            assert(lex && lex[0] == '\'' && lex[strlen(lex) - 1] == '\'');
            unsigned char ch = 0;
            if (lex[1] == '\\') {
                char esc = lex[2];
                switch (esc) {
                    case 'n':
                        ch = '\n';
                        break;
                    case 't':
                        ch = '\t';
                        break;
                    case 'r':
                        ch = '\r';
                        break;
                    case '0':
                        ch = 0;
                        break;
                    case '\'':
                        ch = '\'';
                        break;
                    case '\\':
                        ch = '\\';
                        break;
                    case 'x': {
                        if (lex[3] && lex[4]) {
                            char h1 = lex[3];
                            char h2 = lex[4];
                            char hex[3] = {h1, h2, 0};
                            char* end = NULL;
                            long v = strtol(hex, &end, 16);
                            if (end != hex) ch = (unsigned char)v;
                        }
                        break;
                    }
                    default:
                        ch = esc;
                        break;
                }
            } else {
                ch = (unsigned char)lex[1];
            }
            LLVMTypeRef llvm_ty = pp_backend_lower_type(cg, lit_type);
            assert(llvm_ty);
            llvm_val = LLVMConstInt(llvm_ty, ch, false);
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
        case PP_TT_STATIC_STRING_LITERAL: {
            llvm_val = pp_codegen_static_string_literal_to_llvm_constant(cg->ctx, node->literal.lexeme);
            if (!llvm_val) return NULL;
        } break;
        case PP_TT_STRING_LITERAL: {
            /* Heap string literal: materialise at runtime via malloc + memcpy */
            llvm_val = pp_codegen_string_literal_to_llvm_value(cg, node->literal.lexeme);
            if (!llvm_val) return NULL;
        } break;
        default:
            assert(0 && "Invalid literal");
    }

    return llvm_val;
}

static LLVMValueRef pp_code_generator_generate_rvalue_null_literal(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                   PP_Environment_Backend* env_b,
                                                                   const PP_AST_Node* node) {
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_NullLiteralNode);
    const PP_AST_Decoration* dec = pp_ast_get_decoration(node);
    assert(dec && dec->resolved_type);
    LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, dec->resolved_type);
    assert(llvm_type);
    return LLVMConstNull(llvm_type);
}

static LLVMValueRef pp_code_generator_generate_rvalue_struct_init(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                  PP_Environment_Backend* env_b,
                                                                  const PP_AST_Node* node) {
    LLVMValueRef* field_vals = NULL;
    LLVMValueRef* field_src_addrs = NULL;
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
    /* Inside a function body, an owning field initialized from an l-value must be copied, not
     * aliased: the aggregate is later dropped field by field. Build such aggregates in a
     * scratch slot so each field can be copied in place. */
    bool needs_deep_init = cg->current_function_type && !pp_type_is_trivially_copyable(struct_type);
    if (field_count > 0) {
        field_vals = (LLVMValueRef*)calloc(field_count, sizeof(LLVMValueRef));
        NULL_POINTER_ERROR_HANDLE(field_vals);
        if (needs_deep_init) {
            field_src_addrs = (LLVMValueRef*)calloc(field_count, sizeof(LLVMValueRef));
            if (!field_src_addrs) { goto error_cleanup; }
        }
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
            field_vals[field_idx] = val;
        } else {
            const PP_AST_Decoration* init_dec = pp_ast_get_decoration(entry->member_value);
            assert(init_dec);
            bool init_is_lvalue = init_dec->value_category == PP_VALUE_CATEGORY_LVALUE;
            if (needs_deep_init && init_is_lvalue && pp_type_needs_drop(field->field_type)) {
                val = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, entry->member_value);
                field_src_addrs[field_idx] = val;
            } else {
                val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, entry->member_value);
                field_vals[field_idx] = val;
            }
        }
        assert(val);
    }

    for (size_t i = 0; i < field_count; i++) {
        if (field_vals[i] != NULL) continue;

        const PPStructField* field = &layout->fields[i];
        LLVMTypeRef llvm_field_type = pp_environment_backend_lookup_type(cg, field->field_type);
        assert(llvm_field_type);

        LLVMValueRef val = NULL;
        if (field->default_initializer != NULL) {
            const PP_AST_Decoration* def_dec = pp_ast_get_decoration(field->default_initializer);
            assert(def_dec);
            bool def_is_lvalue = def_dec->value_category == PP_VALUE_CATEGORY_LVALUE;
            if (needs_deep_init && def_is_lvalue && pp_type_needs_drop(field->field_type)) {
                val = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, field->default_initializer);
                field_src_addrs[i] = val;
            } else {
                val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, field->default_initializer);
                field_vals[i] = val;
            }
        } else {
            val = LLVMConstNull(llvm_field_type);
            field_vals[i] = val;
        }
        assert(val);
    }

    LLVMValueRef result = NULL;
    if (!cg->current_function_type) {
        result = LLVMConstNamedStruct(llvm_struct_type, field_vals, (unsigned)field_count);
    } else if (needs_deep_init) {
        LLVMValueRef tmp_slot = pp_code_generator_entry_alloca(cg, llvm_struct_type, "struct_init_tmp");
        for (size_t i = 0; i < field_count; i++) {
            LLVMValueRef field_ptr =
                LLVMBuildStructGEP2(cg->builder, llvm_struct_type, tmp_slot, (unsigned)i, "struct_init_field_ptr");
            if (field_src_addrs && field_src_addrs[i]) {
                pp_code_generator_emit_copy(cg, field_ptr, field_src_addrs[i], layout->fields[i].field_type);
            } else {
                LLVMBuildStore(cg->builder, field_vals[i], field_ptr);
            }
        }
        result = LLVMBuildLoad2(cg->builder, llvm_struct_type, tmp_slot, "struct_init_val");
    } else {
        result = LLVMGetUndef(llvm_struct_type);
        for (size_t i = 0; i < field_count; i++) {
            result = LLVMBuildInsertValue(cg->builder, result, field_vals[i], i, "struct_init_field");
        }
    }
    NULL_POINTER_ERROR_HANDLE(result);

    free(field_vals);
    free(field_src_addrs);
    return result;

error_cleanup:
    free(field_vals);
    free(field_src_addrs);
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
            // Remaining elements of an undersized initializer are zero-filled, per the initialization rules.
            LLVMValueRef result = LLVMConstNull(llvm_array_type);
            for (unsigned int i = 0; i < node->collection_initializer.element_count; i++) {
                LLVMValueRef elem_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b,
                                                                          node->collection_initializer.elements[i]);
                if (!elem_val) return NULL;
                result = LLVMBuildInsertValue(cg->builder, result, elem_val, i, "array_init_elem");
            }
            return result;
        }
    } else if (coll_type->kind == PP_TK_List) {
        if (!cg->current_function_type) {
            if (node->collection_initializer.element_count == 0) {
                // Empty list can be represented as a constant zeroed handle at global scope
                LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, coll_type);
                assert(llvm_list_type);
                LLVMValueRef empty_list = LLVMConstNull(llvm_list_type);
                return empty_list;
            }
            assert(0 && "Non-empty list initializer not supported at global scope");
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
        LLVMValueRef raw_ptr;
        if (n == 0) {
            raw_ptr = LLVMConstNull(i8ptr_type);
        } else {
            raw_ptr = LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &size_val, 1, "list_raw");
        }
        LLVMValueRef buf = LLVMBuildBitCast(cg->builder, raw_ptr, LLVMPointerTypeInContext(cg->ctx, 0), "list_buf");
        // Actually need pointer to elem type
        LLVMTypeRef elem_ptr_type = LLVMPointerType(llvm_elem_type, 0);
        buf = LLVMBuildBitCast(cg->builder, buf, elem_ptr_type, "list_buf_cast");

        for (unsigned int i = 0; i < n; i++) {
            const PP_AST_Node* elem_node = node->collection_initializer.elements[i];
            const PP_AST_Decoration* elem_dec = pp_ast_get_decoration(elem_node);
            assert(elem_dec);
            LLVMValueRef idx = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), i, 0);
            LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, llvm_elem_type, buf, &idx, 1, "list_elem_ptr");
            bool elem_is_lvalue = elem_dec->value_category == PP_VALUE_CATEGORY_LVALUE;
            if (elem_is_lvalue && !pp_type_is_reference(elem_type) && pp_type_needs_drop(elem_type)) {
                // Owning lvalue element: copy the payload instead of aliasing the source.
                LLVMValueRef src_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, elem_node);
                assert(src_ptr);
                pp_code_generator_emit_copy(cg, elem_ptr, src_ptr, elem_type);
            } else {
                LLVMValueRef elem_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, elem_node);
                assert(elem_val);
                LLVMBuildStore(cg->builder, elem_val, elem_ptr);
            }
        }
        LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, coll_type);
        assert(llvm_list_type);
        LLVMValueRef list_val = LLVMGetUndef(llvm_list_type);
        list_val = LLVMBuildInsertValue(cg->builder, list_val, buf, 0, "list_data");
        LLVMValueRef len_val = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), n, 0);
        list_val = LLVMBuildInsertValue(cg->builder, list_val, len_val, 1, "list_len");
        list_val = LLVMBuildInsertValue(cg->builder, list_val, len_val, 2, "list_cap");
        uint64_t sz = pp_code_generator_type_store_size(cg, llvm_elem_type);
        LLVMValueRef elem_size_field = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), sz, 0);
        list_val = LLVMBuildInsertValue(cg->builder, list_val, elem_size_field, 3, "list_elem_size");
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

    if (!pp_type_needs_drop(base_type)) {
        LLVMValueRef extracted = LLVMBuildExtractValue(cg->builder, base_val, (unsigned)field_idx, "struct_elt");
        assert(extracted);
        return extracted;
    }

    /* The aggregate owns its fields but has no owner of its own. Materialize it, move the accessed
     * field out, and let the remaining fields be released at the end of the statement. */
    LLVMTypeRef llvm_base_type = pp_environment_backend_lookup_type(cg, base_type);
    assert(llvm_base_type);
    LLVMValueRef base_slot = pp_code_generator_entry_alloca(cg, llvm_base_type, "member_base_tmp");
    LLVMBuildStore(cg->builder, base_val, base_slot);
    LLVMValueRef field_ptr =
        LLVMBuildStructGEP2(cg->builder, llvm_base_type, base_slot, (unsigned)field_idx, "member_field_ptr");
    const PPType* field_type = layout->fields[field_idx].field_type;
    LLVMTypeRef llvm_field_type = pp_environment_backend_lookup_type(cg, field_type);
    assert(llvm_field_type);
    LLVMValueRef field_val = LLVMBuildLoad2(cg->builder, llvm_field_type, field_ptr, "struct_elt");
    assert(field_val);
    if (pp_type_needs_drop(field_type) && !pp_type_is_reference(field_type)) {
        /* Mark the field as moved so the aggregate drop leaves it alone. */
        LLVMBuildStore(cg->builder, LLVMConstNull(llvm_field_type), field_ptr);
    }
    pp_code_generator_push_temp_drop(cg, base_slot, base_type);
    return field_val;
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
    LLVMValueRef llvm_identifier_ref = NULL;
    assert(cg);
    assert(symbol_table);
    assert(env_b);
    assert(node);
    assert(node->node_type == PP_AST_IdentifierNode);

    const PPSymbol* identifier_symbol = pp_symbol_table_lookup(symbol_table, node->identifier.identifier, true);
    assert(identifier_symbol);

    const PP_AST_Decoration* identifier_decoration = pp_ast_get_decoration(node);
    assert(identifier_decoration);
    assert(identifier_decoration->resolved_type);

    if (identifier_symbol->is_constant_decl) {
        /* Compile-time constant: fold to its analyzed initializer expression. The initializer is
         * constexpr, so the IR builder folds it to an LLVM constant and emits no instructions. */
        assert(identifier_symbol->constant_initializer);
        return pp_code_generator_generate_rvalue(cg, symbol_table, env_b, identifier_symbol->constant_initializer);
    }

    if (identifier_symbol->is_callable_decl) {
        /* Declaration name: the symbol value IS the code, there is no slot to load. */
        llvm_val = pp_environment_backend_lookup_val(env_b, identifier_symbol, true);
        assert(llvm_val);
        return llvm_val;
    }

    /* Everything else, including callable-typed variables, parameters and fields,
     * is storage: take the address and load the value out of it. */
    llvm_identifier_ref = pp_code_generator_generate_lvalue_identifier(cg, symbol_table, env_b, node);
    assert(llvm_identifier_ref);
    llvm_val = pp_code_generator_l_value_to_r_value(cg, identifier_decoration->resolved_type, llvm_identifier_ref);

    return llvm_val;
}

int pp_code_generator_target_machine_create_default(LLVMModuleRef module, LLVMTargetMachineRef* out_tm) {
    if (!out_tm) return 0;
    *out_tm = NULL;

    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    char* triple = LLVMGetDefaultTargetTriple();
    if (!triple) return 0;

    LLVMSetTarget(module, triple);

    LLVMTargetRef target;
    char* err = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        if (err) LLVMDisposeMessage(err);
        LLVMDisposeMessage(triple);
        return 0;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(target, triple, "generic", "", LLVMCodeGenLevelDefault,
                                                      LLVMRelocPIC, LLVMCodeModelDefault);
    LLVMDisposeMessage(triple);

    if (!tm) return 0;
    *out_tm = tm;
    return 1;
}

void pp_code_generator_target_machine_free(LLVMTargetMachineRef tm) {
    if (tm) LLVMDisposeTargetMachine(tm);
}

static PP_TypeCacheEntry* pp_code_generator_find_cache_entry(PP_CodeGenerator* cg, const PPType* type) {
    if (!cg || !type) return NULL;
    for (size_t i = 0; i < cg->type_cache_count; i++) {
        if (cg->type_cache[i].type == type) { return &cg->type_cache[i]; }
    }
    return NULL;
}

static PP_TypeCacheEntry* pp_code_generator_get_or_create_cache_entry(PP_CodeGenerator* cg, const PPType* type) {
    if (!cg || !type) return NULL;
    PP_TypeCacheEntry* entry = NULL;
    PP_TypeCacheEntry* new_buf = NULL;
    entry = pp_code_generator_find_cache_entry(cg, type);
    if (entry) return entry;
    if (cg->type_cache_count == cg->type_cache_capacity) {
        size_t new_cap = cg->type_cache_capacity ? cg->type_cache_capacity * 2 : 8;
        new_buf = (PP_TypeCacheEntry*)realloc(cg->type_cache, new_cap * sizeof(PP_TypeCacheEntry));
        if (!new_buf) goto error_cleanup;
        cg->type_cache = new_buf;
        cg->type_cache_capacity = new_cap;
    }
    entry = &cg->type_cache[cg->type_cache_count];
    cg->type_cache_count++;
    entry->type = type;
    entry->copy_fn = NULL;
    entry->drop_fn = NULL;
    return entry;
error_cleanup:
    return NULL;
}

static void pp_code_generator_gen_copy_body(PP_CodeGenerator* cg, LLVMValueRef func, const PPType* type) {
#warning "Deep copy of cyclic owned graphs is UNDEFINED BEHAVIOR. See comment below."
    // WARNING: Deep copy of cyclic owned graphs is undefined behavior.
    // Under exclusive ownership semantics lvalue source -> deep copy, rvalue source -> move.
    // With correct copy/drop emission the owned heap graph is always a tree and a cycle
    // cannot be built by user code. If infinite recursion is observed it indicates an
    // aliasing bug in the compiler, e.g. collection initializer storing lvalue elements
    // by plain store instead of copy_T, or indexed/member assignment skipping copy_T/drop_T.
    // Example of the bug that would trigger UB:
    //   struct Node { v :: int32; kids :: [Node]; }
    //   let n1 :: Node = Node{ .v = 1, .kids = [Node : 0u] };
    //   let n2 :: Node = Node{ .v = 2, .kids = [Node : 1u; n1] };
    //   n1.kids[0u] = n2;   // if copy is skipped, aliasing creates a cycle
    //   let copy :: Node = n1;     // deep copy -> infinite recursion / UB
    // Save current builder insertion point
    LLVMBasicBlockRef saved_bb = pp_code_generator_save_builder_insert_block(cg);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(func);
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    LLVMValueRef arg0 = LLVMGetParam(func, 0);
    LLVMValueRef arg1 = LLVMGetParam(func, 1);
    if (pp_type_is_trivially_copyable(type)) {
        LLVMTypeRef llvm_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_type) {
            const char* data_layout_str = LLVMGetDataLayout(cg->mod);
            LLVMTargetDataRef td = LLVMCreateTargetData(data_layout_str);
            unsigned size = LLVMABISizeOfType(td, llvm_type);
            LLVMDisposeTargetData(td);
            LLVMValueRef size_val = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), size, 0);
            pp_code_generator_emit_memcpy(cg, arg0, arg1, size_val, "");
        }
    } else if (type->kind == PP_TK_List) {
        const PPType* elem_type = type->type.pp_type_list.element_type;
        LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_list_type) {
            LLVMValueRef dst_ptr = LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_list_type, 0), "dst_list");
            LLVMValueRef src_ptr = LLVMBuildBitCast(cg->builder, arg1, LLVMPointerType(llvm_list_type, 0), "src_list");
            LLVMValueRef src_data_ptr_ptr =
                LLVMBuildStructGEP2(cg->builder, llvm_list_type, src_ptr, 0, "src_data_ptr");
            LLVMValueRef src_len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, src_ptr, 1, "src_len");
            LLVMValueRef src_cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, src_ptr, 2, "src_cap");
            LLVMValueRef dst_data_ptr_ptr =
                LLVMBuildStructGEP2(cg->builder, llvm_list_type, dst_ptr, 0, "dst_data_ptr");
            LLVMValueRef dst_len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, dst_ptr, 1, "dst_len");
            LLVMValueRef dst_cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, dst_ptr, 2, "dst_cap");
            LLVMValueRef src_data =
                LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), src_data_ptr_ptr, "src_data");
            LLVMValueRef src_len =
                LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), src_len_ptr, "src_len_val");
            LLVMValueRef src_cap =
                LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), src_cap_ptr, "src_cap_val");
            LLVMValueRef is_empty = LLVMBuildICmp(cg->builder, LLVMIntEQ, src_len,
                                                  LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), "list_is_empty");
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "list_copy_empty");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "list_copy_nonempty");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "list_copy_merge");
            LLVMBuildCondBr(cg->builder, is_empty, then_bb, else_bb);
            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            LLVMBuildStore(cg->builder, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), dst_data_ptr_ptr);
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), dst_len_ptr);
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), dst_cap_ptr);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            LLVMTypeRef elem_llvm_type = pp_environment_backend_lookup_type(cg, elem_type);
            LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
            const char* data_layout_str = LLVMGetDataLayout(cg->mod);
            LLVMTargetDataRef td = LLVMCreateTargetData(data_layout_str);
            unsigned elem_size = LLVMABISizeOfType(td, elem_llvm_type);
            LLVMDisposeTargetData(td);
            LLVMValueRef elem_size_val = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), elem_size, 0);
            LLVMValueRef bytes = LLVMBuildMul(cg->builder, src_len, elem_size_val, "list_copy_bytes");
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef i8ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
            LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr_type, &i64_type, 1, 0);
            LLVMValueRef new_data_raw = LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &bytes, 1, "list_new_data");
            LLVMValueRef new_data =
                LLVMBuildBitCast(cg->builder, new_data_raw, LLVMPointerType(elem_llvm_type, 0), "list_new_data_cast");
            LLVMBuildStore(cg->builder, new_data, dst_data_ptr_ptr);
            LLVMBuildStore(cg->builder, src_len, dst_len_ptr);
            LLVMBuildStore(cg->builder, src_cap, dst_cap_ptr);
            if (pp_type_is_trivially_copyable(elem_type)) {
                LLVMValueRef src_data_i8 = LLVMBuildBitCast(cg->builder, src_data, i8ptr_type, "src_data_i8");
                LLVMValueRef dst_data_i8 = LLVMBuildBitCast(cg->builder, new_data, i8ptr_type, "dst_data_i8");
                pp_code_generator_emit_memcpy(cg, dst_data_i8, src_data_i8, bytes, "list_memcpy");
            } else {
                PP_TypeCacheEntry* elem_entry = pp_code_generator_get_or_create_cache_entry(cg, elem_type);
                LLVMValueRef elem_copy_fn = pp_code_generator_ensure_copy_fn(cg, elem_entry);
                if (elem_copy_fn) {
                    LLVMBasicBlockRef loop_cond_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "list_copy_loop_cond");
                    LLVMBasicBlockRef loop_body_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "list_copy_loop_body");
                    LLVMBasicBlockRef loop_after_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "list_copy_loop_after");
                    LLVMValueRef i_alloca =
                        pp_code_generator_entry_alloca(cg, LLVMInt64TypeInContext(cg->ctx), "list_copy_i");
                    LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), i_alloca);
                    LLVMBuildBr(cg->builder, loop_cond_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
                    LLVMValueRef i_val =
                        LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), i_alloca, "list_copy_i_val");
                    LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, src_len, "list_copy_loop_cond");
                    LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
                    LLVMValueRef src_elem_ptr =
                        LLVMBuildGEP2(cg->builder, elem_llvm_type, src_data, &i_val, 1, "list_copy_src_elem");
                    LLVMValueRef dst_elem_ptr =
                        LLVMBuildGEP2(cg->builder, elem_llvm_type, new_data, &i_val, 1, "list_copy_dst_elem");
                    LLVMValueRef elem_dst_i8 =
                        LLVMBuildBitCast(cg->builder, dst_elem_ptr, LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                         "elem_dst_i8");
                    LLVMValueRef elem_src_i8 =
                        LLVMBuildBitCast(cg->builder, src_elem_ptr, LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                         "elem_src_i8");
                    LLVMValueRef elem_args[2] = {elem_dst_i8, elem_src_i8};
                    LLVMBuildCall2(cg->builder,
                                   LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                    (LLVMTypeRef[2]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                                                     LLVMPointerType(LLVMInt8TypeInContext(cg->ctx),
                                                                                     0)},
                                                    2, 0),
                                   elem_copy_fn, elem_args, 2, "");
                    LLVMValueRef i_next =
                        LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 1, 0),
                                     "list_copy_i_next");
                    LLVMBuildStore(cg->builder, i_next, i_alloca);
                    LLVMBuildBr(cg->builder, loop_cond_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
                }
            }
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
        }
    } else if (type->kind == PP_TK_String) {
        // Deep copy: a string owns its buffer, so the target gets its own clone.
        // The clone is always NUL-terminated, so `len` bytes plus one are allocated.
        LLVMTypeRef llvm_str_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_str_type) {
            LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
            LLVMTypeRef i8ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
            LLVMValueRef dst_str = LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_str_type, 0), "dst_str");
            LLVMValueRef src_str = LLVMBuildBitCast(cg->builder, arg1, LLVMPointerType(llvm_str_type, 0), "src_str");
            LLVMValueRef src_data_ptr_ptr =
                LLVMBuildStructGEP2(cg->builder, llvm_str_type, src_str, 0, "src_str_ptr_ptr");
            LLVMValueRef src_len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, src_str, 1, "src_str_len_ptr");
            LLVMValueRef dst_data_ptr_ptr =
                LLVMBuildStructGEP2(cg->builder, llvm_str_type, dst_str, 0, "dst_str_ptr_ptr");
            LLVMValueRef dst_len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, dst_str, 1, "dst_str_len_ptr");
            LLVMValueRef src_data = LLVMBuildLoad2(cg->builder, i8ptr_type, src_data_ptr_ptr, "src_str_data");
            LLVMValueRef src_len = LLVMBuildLoad2(cg->builder, i64_type, src_len_ptr, "src_str_len");
            LLVMValueRef is_null =
                LLVMBuildICmp(cg->builder, LLVMIntEQ, src_data, LLVMConstNull(i8ptr_type), "str_is_null");
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "str_copy_empty");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "str_copy_nonempty");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "str_copy_merge");
            LLVMBuildCondBr(cg->builder, is_null, then_bb, else_bb);
            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            // Empty string: nothing is owned, copy the descriptor and be done.
            LLVMBuildStore(cg->builder, LLVMConstNull(i8ptr_type), dst_data_ptr_ptr);
            LLVMBuildStore(cg->builder, src_len, dst_len_ptr);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            {
                LLVMValueRef malloc_fn = LLVMGetNamedFunction(cg->mod, "malloc");
                assert(malloc_fn);
                LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr_type, &i64_type, 1, 0);
                LLVMValueRef bytes = LLVMBuildAdd(cg->builder, src_len, LLVMConstInt(i64_type, 1, 0), "str_copy_bytes");
                LLVMValueRef new_data = LLVMBuildCall2(cg->builder, malloc_type, malloc_fn, &bytes, 1, "str_copy_data");
                // Copy the payload then terminate; `len` stays the payload length.
                pp_code_generator_emit_memcpy(cg, new_data, src_data, src_len, "str_copy_payload");
                LLVMValueRef term_ptr = LLVMBuildGEP2(cg->builder, LLVMInt8TypeInContext(cg->ctx), new_data, &src_len,
                                                      1, "str_copy_term_ptr");
                LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt8TypeInContext(cg->ctx), 0, 0), term_ptr);
                LLVMBuildStore(cg->builder, new_data, dst_data_ptr_ptr);
                LLVMBuildStore(cg->builder, src_len, dst_len_ptr);
            }
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
        }
    } else if (type->kind == PP_TK_Array) {
        const PPType* elem_type = type->type.pp_type_array.element_type;
        size_t arr_size = type->type.pp_type_array.size;
        LLVMTypeRef llvm_array_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_array_type) {
            LLVMValueRef dst_ptr =
                LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_array_type, 0), "dst_array");
            LLVMValueRef src_ptr =
                LLVMBuildBitCast(cg->builder, arg1, LLVMPointerType(llvm_array_type, 0), "src_array");
            if (pp_type_is_trivially_copyable(elem_type)) {
                LLVMTypeRef elem_llvm_type = pp_environment_backend_lookup_type(cg, elem_type);
                const char* data_layout_str = LLVMGetDataLayout(cg->mod);
                LLVMTargetDataRef td = LLVMCreateTargetData(data_layout_str);
                unsigned elem_size = LLVMABISizeOfType(td, elem_llvm_type);
                LLVMDisposeTargetData(td);
                LLVMValueRef elem_size_val = LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), elem_size, 0);
                LLVMValueRef total_size =
                    LLVMBuildMul(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), arr_size, 0), elem_size_val,
                                 "array_copy_bytes");
                LLVMValueRef src_i8 = LLVMBuildBitCast(cg->builder, src_ptr,
                                                       LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "src_i8");
                LLVMValueRef dst_i8 = LLVMBuildBitCast(cg->builder, dst_ptr,
                                                       LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "dst_i8");
                pp_code_generator_emit_memcpy(cg, dst_i8, src_i8, total_size, "");
            } else {
                LLVMTypeRef elem_llvm_type = pp_environment_backend_lookup_type(cg, elem_type);
                PP_TypeCacheEntry* elem_entry = pp_code_generator_get_or_create_cache_entry(cg, elem_type);
                LLVMValueRef elem_copy_fn = pp_code_generator_ensure_copy_fn(cg, elem_entry);
                if (elem_copy_fn && elem_llvm_type) {
                    LLVMBasicBlockRef loop_cond_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "array_copy_loop_cond");
                    LLVMBasicBlockRef loop_body_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "array_copy_loop_body");
                    LLVMBasicBlockRef loop_after_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "array_copy_loop_after");
                    LLVMValueRef i_alloca =
                        pp_code_generator_entry_alloca(cg, LLVMInt64TypeInContext(cg->ctx), "array_copy_i");
                    LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), i_alloca);
                    LLVMBuildBr(cg->builder, loop_cond_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
                    LLVMValueRef i_val =
                        LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), i_alloca, "array_copy_i_val");
                    LLVMValueRef cond =
                        LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val,
                                      LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), arr_size, 0), "array_copy_cond");
                    LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
                    LLVMValueRef indices[2] = {LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0), i_val};
                    LLVMValueRef src_elem_ptr =
                        LLVMBuildGEP2(cg->builder, llvm_array_type, src_ptr, indices, 2, "src_elem");
                    LLVMValueRef dst_elem_ptr =
                        LLVMBuildGEP2(cg->builder, llvm_array_type, dst_ptr, indices, 2, "dst_elem");
                    LLVMValueRef src_i8 = LLVMBuildBitCast(cg->builder, src_elem_ptr,
                                                           LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "");
                    LLVMValueRef dst_i8 = LLVMBuildBitCast(cg->builder, dst_elem_ptr,
                                                           LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "");
                    LLVMValueRef args[2] = {dst_i8, src_i8};
                    LLVMBuildCall2(cg->builder,
                                   LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                    (LLVMTypeRef[2]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                                                     LLVMPointerType(LLVMInt8TypeInContext(cg->ctx),
                                                                                     0)},
                                                    2, 0),
                                   elem_copy_fn, args, 2, "");
                    LLVMValueRef i_next =
                        LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 1, 0), "");
                    LLVMBuildStore(cg->builder, i_next, i_alloca);
                    LLVMBuildBr(cg->builder, loop_cond_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
                }
            }
        }
    } else if (type->kind == PP_TK_Struct) {
        const PP_StructLayout* layout = type->type.pp_type_struct.layout;
        if (layout) {
            LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, type);
            if (llvm_struct_type) {
                LLVMValueRef dst_ptr =
                    LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_struct_type, 0), "dst_struct");
                LLVMValueRef src_ptr =
                    LLVMBuildBitCast(cg->builder, arg1, LLVMPointerType(llvm_struct_type, 0), "src_struct");
                for (size_t i = 0; i < layout->field_count; i++) {
                    const PPType* field_type = layout->fields[i].field_type;
                    LLVMValueRef field_dst_ptr = LLVMBuildStructGEP2(cg->builder, llvm_struct_type, dst_ptr, i, "");
                    LLVMValueRef field_src_ptr = LLVMBuildStructGEP2(cg->builder, llvm_struct_type, src_ptr, i, "");
                    PP_TypeCacheEntry* field_entry = pp_code_generator_get_or_create_cache_entry(cg, field_type);
                    LLVMValueRef field_copy_fn = pp_code_generator_ensure_copy_fn(cg, field_entry);
                    if (field_copy_fn) {
                        LLVMValueRef field_dst_i8 =
                            LLVMBuildBitCast(cg->builder, field_dst_ptr,
                                             LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "");
                        LLVMValueRef field_src_i8 =
                            LLVMBuildBitCast(cg->builder, field_src_ptr,
                                             LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "");
                        LLVMValueRef field_args[2] = {field_dst_i8, field_src_i8};
                        LLVMBuildCall2(cg->builder,
                                       LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                        (LLVMTypeRef[2]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx),
                                                                                         0),
                                                                         LLVMPointerType(LLVMInt8TypeInContext(cg->ctx),
                                                                                         0)},
                                                        2, 0),
                                       field_copy_fn, field_args, 2, "");
                    }
                }
            }
        }
    }
    LLVMBuildRetVoid(cg->builder);
    pp_code_generator_restore_builder_insert_block(cg, saved_bb);
}

static void pp_code_generator_gen_drop_body(PP_CodeGenerator* cg, LLVMValueRef func, const PPType* type) {
#warning "Drop of cyclic owned graphs is UNDEFINED BEHAVIOR. See comment below."
    // WARNING: Drop of cyclic owned graphs is undefined behavior.
    // Under exclusive ownership semantics the owned heap graph must be a tree.
    // A cycle can only arise from an aliasing bug in the compiler e.g. missing copy_T/drop_T
    // on element stores. If infinite recursion is observed it is a compiler bug, not a
    // permitted user program. Do not add a runtime visited set to hide it.
    // Example of the bug that would trigger UB:
    //   struct Node { v :: int32; kids :: [Node]; }
    //   let n1 :: Node = Node{ .v = 1, .kids = [Node : 0u] };
    //   let n2 :: Node = Node{ .v = 2, .kids = [Node : 1u; n1] };
    //   n1.kids[0u] = n2;   // if copy is skipped, aliasing creates a cycle
    //   // dropping n1 will recurse infinitely / UB
    LLVMBasicBlockRef saved_bb = pp_code_generator_save_builder_insert_block(cg);
    LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(func);
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    LLVMValueRef arg0 = LLVMGetParam(func, 0);
    if (!pp_type_needs_drop(type)) {
        LLVMBuildRetVoid(cg->builder);
        pp_code_generator_restore_builder_insert_block(cg, saved_bb);
        return;
    }
    if (pp_type_is_handle(type)) {
        // Handle: load the underlying value and hand it to the bound drop function.
        const PPType* underlying = type->type.pp_type_newtype.underlying;
        const PPSymbol* drop_symbol = type->type.pp_type_newtype.drop_symbol;
        LLVMTypeRef llvm_underlying = underlying ? pp_environment_backend_lookup_type(cg, underlying) : NULL;
        LLVMValueRef drop_fn =
            drop_symbol ? pp_environment_backend_lookup_val(cg->global_env_b, drop_symbol, false) : NULL;
        if (llvm_underlying && drop_fn) {
            LLVMValueRef value_ptr =
                LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_underlying, 0), "handle_ptr");
            LLVMValueRef value = LLVMBuildLoad2(cg->builder, llvm_underlying, value_ptr, "handle_value");
            LLVMTypeRef drop_fn_type = LLVMGlobalGetValueType(drop_fn);

            // Pointer-backed handles skip the drop when the value is null.
            if (LLVMGetTypeKind(llvm_underlying) == LLVMPointerTypeKind) {
                LLVMValueRef is_null =
                    LLVMBuildICmp(cg->builder, LLVMIntEQ, value, LLVMConstNull(llvm_underlying), "handle_is_null");
                LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "handle_drop_call");
                LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "handle_drop_end");
                LLVMBuildCondBr(cg->builder, is_null, merge_bb, call_bb);
                LLVMPositionBuilderAtEnd(cg->builder, call_bb);
                LLVMBuildCall2(cg->builder, drop_fn_type, drop_fn, &value, 1, "");
                LLVMBuildBr(cg->builder, merge_bb);
                LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
            } else {
                LLVMBuildCall2(cg->builder, drop_fn_type, drop_fn, &value, 1, "");
            }
        }
    } else if (type->kind == PP_TK_List) {
        const PPType* elem_type = type->type.pp_type_list.element_type;
        LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_list_type) {
            LLVMValueRef list_ptr = LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_list_type, 0), "list_ptr");
            LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 0, "data_ptr_ptr");
            LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 1, "len_ptr");
            LLVMValueRef data_ptr =
                LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "data_ptr");
            LLVMValueRef len = LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), len_ptr, "len_val");
            LLVMValueRef is_null = LLVMBuildICmp(cg->builder, LLVMIntNE, data_ptr,
                                                 LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), "data_not_null");
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "list_drop_then");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "list_drop_else");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "list_drop_merge");
            LLVMBuildCondBr(cg->builder, is_null, then_bb, else_bb);
            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            if (pp_type_needs_drop(elem_type)) {
                LLVMTypeRef elem_llvm_type = pp_environment_backend_lookup_type(cg, elem_type);
                PP_TypeCacheEntry* elem_entry = pp_code_generator_get_or_create_cache_entry(cg, elem_type);
                LLVMValueRef elem_drop_fn = pp_code_generator_ensure_drop_fn(cg, elem_entry);
                if (elem_drop_fn && elem_llvm_type) {
                    LLVMBasicBlockRef loop_cond_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "list_drop_loop_cond");
                    LLVMBasicBlockRef loop_body_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "list_drop_loop_body");
                    LLVMBasicBlockRef loop_after_bb =
                        LLVMAppendBasicBlockInContext(cg->ctx, func, "list_drop_loop_after");
                    LLVMValueRef i_alloca =
                        pp_code_generator_entry_alloca(cg, LLVMInt64TypeInContext(cg->ctx), "list_drop_i");
                    LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), i_alloca);
                    LLVMBuildBr(cg->builder, loop_cond_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
                    LLVMValueRef i_val =
                        LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), i_alloca, "list_drop_i_val");
                    LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, len, "list_drop_loop_cond");
                    LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
                    LLVMValueRef elem_ptr =
                        LLVMBuildGEP2(cg->builder, elem_llvm_type, data_ptr, &i_val, 1, "list_drop_elem_ptr");
                    LLVMValueRef elem_i8 =
                        LLVMBuildBitCast(cg->builder, elem_ptr, LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                         "elem_i8");
                    LLVMBuildCall2(cg->builder,
                                   LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                    (LLVMTypeRef[1]){
                                                        LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)},
                                                    1, 0),
                                   elem_drop_fn, &elem_i8, 1, "");
                    LLVMValueRef i_next =
                        LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 1, 0),
                                     "list_drop_i_next");
                    LLVMBuildStore(cg->builder, i_next, i_alloca);
                    LLVMBuildBr(cg->builder, loop_cond_bb);
                    LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
                }
            }
            LLVMValueRef free_fn = LLVMGetNamedFunction(cg->mod, "free");
            assert(free_fn);
            {
                LLVMTypeRef free_type =
                    LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                     (LLVMTypeRef[1]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)}, 1, 0);
                LLVMValueRef free_args[1] = {data_ptr};
                LLVMBuildCall2(cg->builder, free_type, free_fn, free_args, 1, "");
            }
            LLVMBuildStore(cg->builder, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), data_ptr_ptr);
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), len_ptr);
            LLVMValueRef cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 2, "cap_ptr");
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), cap_ptr);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
        }
    } else if (type->kind == PP_TK_Array) {
        const PPType* elem_type = type->type.pp_type_array.element_type;
        size_t arr_size = type->type.pp_type_array.size;
        LLVMTypeRef llvm_array_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_array_type && pp_type_needs_drop(elem_type)) {
            LLVMTypeRef elem_llvm_type = pp_environment_backend_lookup_type(cg, elem_type);
            PP_TypeCacheEntry* elem_entry = pp_code_generator_get_or_create_cache_entry(cg, elem_type);
            LLVMValueRef elem_drop_fn = pp_code_generator_ensure_drop_fn(cg, elem_entry);
            if (elem_drop_fn && elem_llvm_type) {
                LLVMValueRef array_ptr =
                    LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_array_type, 0), "array_ptr");
                LLVMBasicBlockRef loop_cond_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "array_drop_loop_cond");
                LLVMBasicBlockRef loop_body_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "array_drop_loop_body");
                LLVMBasicBlockRef loop_after_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "array_drop_loop_after");
                LLVMValueRef i_alloca =
                    pp_code_generator_entry_alloca(cg, LLVMInt64TypeInContext(cg->ctx), "array_drop_i");
                LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), i_alloca);
                LLVMBuildBr(cg->builder, loop_cond_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
                LLVMValueRef i_val =
                    LLVMBuildLoad2(cg->builder, LLVMInt64TypeInContext(cg->ctx), i_alloca, "array_drop_i_val");
                LLVMValueRef cond =
                    LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val,
                                  LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), arr_size, 0), "array_drop_cond");
                LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
                LLVMValueRef indices[2] = {LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, 0), i_val};
                LLVMValueRef elem_ptr =
                    LLVMBuildGEP2(cg->builder, llvm_array_type, array_ptr, indices, 2, "array_elem_ptr");
                LLVMValueRef elem_i8 =
                    LLVMBuildBitCast(cg->builder, elem_ptr, LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "");
                LLVMBuildCall2(cg->builder,
                               LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                (LLVMTypeRef[1]){LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)}, 1,
                                                0),
                               elem_drop_fn, &elem_i8, 1, "");
                LLVMValueRef i_next =
                    LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 1, 0), "");
                LLVMBuildStore(cg->builder, i_next, i_alloca);
                LLVMBuildBr(cg->builder, loop_cond_bb);
                LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
            }
        }
    } else if (type->kind == PP_TK_Struct) {
        const PP_StructLayout* layout = type->type.pp_type_struct.layout;
        if (layout) {
            LLVMTypeRef llvm_struct_type = pp_environment_backend_lookup_type(cg, type);
            if (llvm_struct_type) {
                LLVMValueRef struct_ptr =
                    LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_struct_type, 0), "struct_ptr");
                for (size_t i = 0; i < layout->field_count; i++) {
                    const PPType* field_type = layout->fields[i].field_type;
                    if (pp_type_needs_drop(field_type)) {
                        PP_TypeCacheEntry* field_entry = pp_code_generator_get_or_create_cache_entry(cg, field_type);
                        LLVMValueRef field_drop_fn = pp_code_generator_ensure_drop_fn(cg, field_entry);
                        if (field_drop_fn) {
                            LLVMValueRef field_ptr =
                                LLVMBuildStructGEP2(cg->builder, llvm_struct_type, struct_ptr, i, "");
                            LLVMValueRef field_i8 =
                                LLVMBuildBitCast(cg->builder, field_ptr,
                                                 LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0), "");
                            LLVMBuildCall2(cg->builder,
                                           LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                            (LLVMTypeRef[1]){
                                                                LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)},
                                                            1, 0),
                                           field_drop_fn, &field_i8, 1, "");
                        }
                    }
                }
            }
        }
    } else if (type->kind == PP_TK_String) {
        LLVMTypeRef llvm_str_type = pp_environment_backend_lookup_type(cg, type);
        if (llvm_str_type) {
            LLVMValueRef str_ptr = LLVMBuildBitCast(cg->builder, arg0, LLVMPointerType(llvm_str_type, 0), "str_ptr");
            LLVMValueRef ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, str_ptr, 0, "str_ptr_ptr");
            LLVMValueRef ptr =
                LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), ptr_ptr, "str_ptr_val");
            LLVMValueRef is_null = LLVMBuildICmp(cg->builder, LLVMIntNE, ptr,
                                                 LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), "str_not_null");
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "str_drop_then");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "str_drop_else");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "str_drop_merge");
            LLVMBuildCondBr(cg->builder, is_null, then_bb, else_bb);
            LLVMPositionBuilderAtEnd(cg->builder, then_bb);
            LLVMValueRef free_fn = LLVMGetNamedFunction(cg->mod, "free");
            assert(free_fn);
            {
                LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx),
                                                         (LLVMTypeRef[1]){LLVMPointerTypeInContext(cg->ctx, 0)}, 1, 0);
                LLVMBuildCall2(cg->builder, free_type, free_fn, &ptr, 1, "");
            }
            LLVMBuildStore(cg->builder, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), ptr_ptr);
            LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_str_type, str_ptr, 1, "str_len_ptr");
            LLVMBuildStore(cg->builder, LLVMConstInt(LLVMInt64TypeInContext(cg->ctx), 0, 0), len_ptr);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, else_bb);
            LLVMBuildBr(cg->builder, merge_bb);
            LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
        }
    }
    LLVMBuildRetVoid(cg->builder);
    pp_code_generator_restore_builder_insert_block(cg, saved_bb);
}

static LLVMBasicBlockRef pp_code_generator_save_builder_insert_block(PP_CodeGenerator* cg) {
    return LLVMGetInsertBlock(cg->builder);
}

static void pp_code_generator_restore_builder_insert_block(PP_CodeGenerator* cg, LLVMBasicBlockRef block) {
    if (block) { LLVMPositionBuilderAtEnd(cg->builder, block); }
}

static LLVMValueRef pp_code_generator_ensure_copy_fn(PP_CodeGenerator* cg, PP_TypeCacheEntry* entry) {
    if (!cg || !entry) return NULL;
    if (entry->copy_fn) return entry->copy_fn;
    const PPType* type = entry->type;
    if (!type) return NULL;
    if (!pp_type_is_copyable(type)) {
        // Non-copyable types fall back to inline
        return NULL;
    }
    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "pp.copy.%p", (void*)type);
    LLVMTypeRef param_types[2] = {LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0),
                                  LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)};
    LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), param_types, 2, 0);
    LLVMValueRef func = LLVMAddFunction(cg->mod, name_buf, func_type);
    // Register early to allow recursive references
    entry->copy_fn = func;
    // Synthetic helpers are not emitted with debug info to avoid stepping into generated code
    // and to avoid DI verification issues with artificial subprograms.
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "entry");
    LLVMBasicBlockRef saved = pp_code_generator_save_builder_insert_block(cg);
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    pp_code_generator_gen_copy_body(cg, func, type);
    pp_code_generator_restore_builder_insert_block(cg, saved);
    return func;
}

static LLVMValueRef pp_code_generator_ensure_drop_fn(PP_CodeGenerator* cg, PP_TypeCacheEntry* entry) {
    if (!cg || !entry) return NULL;
    if (entry->drop_fn) return entry->drop_fn;
    const PPType* type = entry->type;
    if (!type) return NULL;
    // Generate drop helper for any type; gen_drop_body handles no-op case
    // if (pp_type_needs_drop(type)) {
    //     // Non-trivial drops not yet handled – fall back to inline
    //     return NULL;
    // }
    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "pp.drop.%p", (void*)type);
    LLVMTypeRef param_types[1] = {LLVMPointerType(LLVMInt8TypeInContext(cg->ctx), 0)};
    LLVMTypeRef func_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), param_types, 1, 0);
    LLVMValueRef func = LLVMAddFunction(cg->mod, name_buf, func_type);
    // Register early to allow recursive references
    entry->drop_fn = func;
    // Synthetic helpers are not emitted with debug info to avoid stepping into generated code
    // and to avoid DI verification issues.
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(cg->ctx, func, "entry");
    LLVMBasicBlockRef saved = pp_code_generator_save_builder_insert_block(cg);
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    pp_code_generator_gen_drop_body(cg, func, type);
    pp_code_generator_restore_builder_insert_block(cg, saved);
    return func;
}

static LLVMValueRef pp_code_generator_generate_rvalue_list_push(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                PP_Environment_Backend* env_b,
                                                                const PP_AST_Node* node) {
    assert(cg && symbol_table && env_b && node);
    assert(node->node_type == PP_AST_ListPushNode);
    LLVMValueRef list_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->list_push.list);
    const PP_AST_Decoration* push_value_dec = pp_ast_get_decoration(node->list_push.value);
    assert(push_value_dec);
    bool value_is_lvalue = push_value_dec->value_category == PP_VALUE_CATEGORY_LVALUE;
    LLVMValueRef elem_val = pp_code_generator_generate_rvalue(cg, symbol_table, env_b, node->list_push.value);
    const PP_AST_Decoration* list_dec = pp_ast_get_decoration(node->list_push.list);
    assert(list_dec);
    assert(list_dec->resolved_type);
    const PPType* list_type = pp_type_decay_cg(cg->gtt, list_dec->resolved_type);
    const PPType* elem_type = list_type->type.pp_type_list.element_type;
    LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, list_type);
    LLVMTypeRef llvm_elem_type = pp_backend_lower_type(cg, elem_type);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
    // Field pointers
    LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 0, "list_data_ptr");
    LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 1, "list_len_ptr");
    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 2, "list_cap_ptr");
    LLVMValueRef elem_size_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 3, "list_elem_size_ptr");
    LLVMValueRef data = LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data");
    LLVMValueRef len = LLVMBuildLoad2(cg->builder, i64_type, len_ptr, "list_len");
    LLVMValueRef cap = LLVMBuildLoad2(cg->builder, i64_type, cap_ptr, "list_cap");
    LLVMValueRef elem_size = LLVMBuildLoad2(cg->builder, i64_type, elem_size_ptr, "list_elem_size");
    // Ensure elem_size is set
    LLVMValueRef zero_i64 = LLVMConstInt(i64_type, 0, 0);
    LLVMValueRef elem_size_is_zero = LLVMBuildICmp(cg->builder, LLVMIntEQ, elem_size, zero_i64, "elem_size_is_zero");
    LLVMBasicBlockRef set_size_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "push_set_size");
    LLVMBasicBlockRef continue_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "push_continue");
    LLVMBuildCondBr(cg->builder, elem_size_is_zero, set_size_bb, continue_bb);
    LLVMPositionBuilderAtEnd(cg->builder, set_size_bb);
    {
        uint64_t sz = pp_code_generator_type_store_size(cg, llvm_elem_type);
        LLVMValueRef sz_val = LLVMConstInt(i64_type, sz, 0);
        LLVMBuildStore(cg->builder, sz_val, elem_size_ptr);
        elem_size = sz_val;
    }
    LLVMBuildBr(cg->builder, continue_bb);
    LLVMPositionBuilderAtEnd(cg->builder, continue_bb);
    // Grow if needed
    LLVMValueRef need_grow = LLVMBuildICmp(cg->builder, LLVMIntUGE, len, cap, "need_grow");
    LLVMBasicBlockRef grow_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)), "push_grow");
    LLVMBasicBlockRef no_grow_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "push_no_grow");
    LLVMBasicBlockRef after_grow_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "push_after_grow");
    LLVMBuildCondBr(cg->builder, need_grow, grow_bb, no_grow_bb);
    LLVMPositionBuilderAtEnd(cg->builder, grow_bb);
    {
        LLVMValueRef cap_is_zero = LLVMBuildICmp(cg->builder, LLVMIntEQ, cap, zero_i64, "cap_is_zero");
        LLVMValueRef four = LLVMConstInt(i64_type, 4, 0);
        LLVMValueRef cap_times_two = LLVMBuildMul(cg->builder, cap, LLVMConstInt(i64_type, 2, 0), "cap_times_two");
        LLVMValueRef new_cap = LLVMBuildSelect(cg->builder, cap_is_zero, four, cap_times_two, "new_cap");
        LLVMValueRef new_size = LLVMBuildMul(cg->builder, new_cap, elem_size, "new_size");
        LLVMValueRef realloc_fn = LLVMGetNamedFunction(cg->mod, "realloc");
        assert(realloc_fn);
        LLVMTypeRef realloc_type =
            LLVMFunctionType(LLVMPointerTypeInContext(cg->ctx, 0),
                             (LLVMTypeRef[]){LLVMPointerTypeInContext(cg->ctx, 0), i64_type}, 2, 0);
        LLVMValueRef args[2] = {data, new_size};
        LLVMValueRef new_data = LLVMBuildCall2(cg->builder, realloc_type, realloc_fn, args, 2, "list_realloc");
        LLVMBuildStore(cg->builder, new_data, data_ptr_ptr);
        LLVMBuildStore(cg->builder, new_cap, cap_ptr);
        data = new_data;
        cap = new_cap;
    }
    LLVMBuildBr(cg->builder, after_grow_bb);
    LLVMPositionBuilderAtEnd(cg->builder, no_grow_bb);
    LLVMBuildBr(cg->builder, after_grow_bb);
    LLVMPositionBuilderAtEnd(cg->builder, after_grow_bb);
    // Reload data pointer after possible reallocation
    data = LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data_after_grow");
    // Copy element
    LLVMValueRef elem_tmp = pp_code_generator_entry_alloca(cg, llvm_elem_type, "push_elem_tmp");
    LLVMBuildStore(cg->builder, elem_val, elem_tmp);
    LLVMValueRef byte_offset = LLVMBuildMul(cg->builder, len, elem_size, "push_byte_offset");
    LLVMValueRef dest_i8 =
        LLVMBuildGEP2(cg->builder, LLVMInt8TypeInContext(cg->ctx), data, &byte_offset, 1, "push_dest_i8");
    LLVMValueRef dest_ptr = LLVMBuildBitCast(cg->builder, dest_i8, LLVMPointerType(llvm_elem_type, 0), "push_dest");
    if (!pp_type_is_trivially_copyable(elem_type) && value_is_lvalue) {
        /* An l-value keeps its ownership, so the list gets its own copy. */
        LLVMValueRef src_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->list_push.value);
        assert(src_ptr);
        pp_code_generator_emit_copy(cg, dest_ptr, src_ptr, elem_type);
    } else {
        /* An r-value moves into the list: no copy, no drop of the source afterwards. */
        LLVMBuildMemCpy(cg->builder, dest_ptr, 8, elem_tmp, 8, elem_size);
    }
    // Increment len
    LLVMValueRef new_len = LLVMBuildAdd(cg->builder, len, LLVMConstInt(i64_type, 1, 0), "push_new_len");
    LLVMBuildStore(cg->builder, new_len, len_ptr);
    return LLVMGetUndef(LLVMVoidTypeInContext(cg->ctx));
}

static LLVMValueRef pp_code_generator_generate_rvalue_list_pop(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                               PP_Environment_Backend* env_b, const PP_AST_Node* node) {
    assert(cg && symbol_table && env_b && node);
    assert(node->node_type == PP_AST_ListPopNode);
    LLVMValueRef list_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->list_pop.list);
    const PP_AST_Decoration* list_dec = pp_ast_get_decoration(node->list_pop.list);
    assert(list_dec);
    assert(list_dec->resolved_type);
    const PPType* list_type = pp_type_decay_cg(cg->gtt, list_dec->resolved_type);
    const PPType* elem_type = list_type->type.pp_type_list.element_type;
    LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, list_type);
    LLVMTypeRef llvm_elem_type = pp_backend_lower_type(cg, elem_type);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
    // Field pointers
    LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 0, "list_data_ptr");
    LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 1, "list_len_ptr");
    LLVMValueRef elem_size_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 3, "list_elem_size_ptr");
    LLVMValueRef data = LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data");
    LLVMValueRef len = LLVMBuildLoad2(cg->builder, i64_type, len_ptr, "list_len");
    LLVMValueRef elem_size = LLVMBuildLoad2(cg->builder, i64_type, elem_size_ptr, "list_elem_size");
    // Abort if empty
    LLVMValueRef is_empty = LLVMBuildICmp(cg->builder, LLVMIntEQ, len, LLVMConstInt(i64_type, 0, 0), "list_is_empty");
    LLVMValueRef abort_fn = LLVMGetNamedFunction(cg->mod, "abort");
    if (!abort_fn) {
        LLVMTypeRef abort_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
        abort_fn = LLVMAddFunction(cg->mod, "abort", abort_type);
    }
    LLVMBasicBlockRef then_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)), "pop_abort");
    LLVMBasicBlockRef else_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)), "pop_ok");
    LLVMBasicBlockRef merge_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)), "pop_merge");
    LLVMBuildCondBr(cg->builder, is_empty, then_bb, else_bb);
    LLVMPositionBuilderAtEnd(cg->builder, then_bb);
    {
        LLVMTypeRef abort_type = LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), NULL, 0, 0);
        LLVMBuildCall2(cg->builder, abort_type, abort_fn, NULL, 0, "");
    }
    LLVMBuildUnreachable(cg->builder);
    LLVMPositionBuilderAtEnd(cg->builder, else_bb);
    // Compute index
    LLVMValueRef one = LLVMConstInt(i64_type, 1, 0);
    LLVMValueRef idx = LLVMBuildSub(cg->builder, len, one, "pop_idx");
    // Byte offset
    LLVMValueRef byte_offset = LLVMBuildMul(cg->builder, idx, elem_size, "pop_byte_offset");
    // Element pointer
    LLVMValueRef elem_ptr_i8 =
        LLVMBuildGEP2(cg->builder, LLVMInt8TypeInContext(cg->ctx), data, &byte_offset, 1, "pop_elem_ptr_i8");
    LLVMValueRef elem_ptr =
        LLVMBuildBitCast(cg->builder, elem_ptr_i8, LLVMPointerType(llvm_elem_type, 0), "pop_elem_ptr");
    // Copy out
    LLVMValueRef elem_tmp = pp_code_generator_entry_alloca(cg, llvm_elem_type, "pop_tmp");
    /* The popped slot leaves the live range (`len` is decremented below), so its value is moved out:
     * no copy is made and the list never drops it again. */
    {
        LLVMValueRef popped_val = LLVMBuildLoad2(cg->builder, llvm_elem_type, elem_ptr, "pop_elem");
        assert(popped_val);
        LLVMBuildStore(cg->builder, popped_val, elem_tmp);
    }
    // Update len
    LLVMValueRef new_len = LLVMBuildSub(cg->builder, len, one, "pop_new_len");
    LLVMBuildStore(cg->builder, new_len, len_ptr);
    // Optional drop of popped element if type needs drop and we don't transfer ownership? For simplicity, we keep
    // value. If type needs drop, we could drop the source? No, we returned it.
    LLVMBuildBr(cg->builder, merge_bb);
    LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
    return LLVMBuildLoad2(cg->builder, llvm_elem_type, elem_tmp, "pop_ret");
}

static LLVMValueRef pp_code_generator_generate_rvalue_list_clear(PP_CodeGenerator* cg, PPSymbolTable* symbol_table,
                                                                 PP_Environment_Backend* env_b,
                                                                 const PP_AST_Node* node) {
    assert(cg && symbol_table && env_b && node);
    assert(node->node_type == PP_AST_ListClearNode);
    LLVMValueRef list_ptr = pp_code_generator_generate_lvalue(cg, symbol_table, env_b, node->list_clear.list);
    const PP_AST_Decoration* list_dec = pp_ast_get_decoration(node->list_clear.list);
    assert(list_dec);
    assert(list_dec->resolved_type);
    const PPType* list_type = pp_type_decay_cg(cg->gtt, list_dec->resolved_type);
    const PPType* elem_type = list_type->type.pp_type_list.element_type;
    LLVMTypeRef llvm_list_type = pp_environment_backend_lookup_type(cg, list_type);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(cg->ctx);
    // Field pointers
    LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 0, "list_data_ptr");
    LLVMValueRef len_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 1, "list_len_ptr");
    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(cg->builder, llvm_list_type, list_ptr, 2, "list_cap_ptr");
    // Load data and len
    LLVMValueRef data = LLVMBuildLoad2(cg->builder, LLVMPointerTypeInContext(cg->ctx, 0), data_ptr_ptr, "list_data");
    LLVMValueRef len = LLVMBuildLoad2(cg->builder, i64_type, len_ptr, "list_len");
    // Drop elements if needed
    if (pp_type_needs_drop(elem_type)) {
        LLVMTypeRef llvm_elem_type = pp_backend_lower_type(cg, elem_type);
        LLVMValueRef is_null = LLVMBuildICmp(cg->builder, LLVMIntNE, data,
                                             LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), "list_data_not_null");
        LLVMBasicBlockRef then_bb =
            LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                          "clear_drop_then");
        LLVMBasicBlockRef else_bb =
            LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                          "clear_drop_else");
        LLVMBasicBlockRef merge_bb =
            LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                          "clear_drop_merge");
        LLVMBuildCondBr(cg->builder, is_null, then_bb, else_bb);
        LLVMPositionBuilderAtEnd(cg->builder, then_bb);
        // Loop over elements
        LLVMBasicBlockRef loop_cond_bb =
            LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                          "clear_loop_cond");
        LLVMBasicBlockRef loop_body_bb =
            LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                          "clear_loop_body");
        LLVMBasicBlockRef loop_after_bb =
            LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                          "clear_loop_after");
        LLVMValueRef i_alloca = pp_code_generator_entry_alloca(cg, i64_type, "clear_i");
        LLVMBuildStore(cg->builder, LLVMConstInt(i64_type, 0, 0), i_alloca);
        LLVMBuildBr(cg->builder, loop_cond_bb);
        LLVMPositionBuilderAtEnd(cg->builder, loop_cond_bb);
        LLVMValueRef i_val = LLVMBuildLoad2(cg->builder, i64_type, i_alloca, "clear_i_val");
        LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, i_val, len, "clear_loop_cond");
        LLVMBuildCondBr(cg->builder, cond, loop_body_bb, loop_after_bb);
        LLVMPositionBuilderAtEnd(cg->builder, loop_body_bb);
        LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, llvm_elem_type, data, &i_val, 1, "clear_elem_ptr");
        pp_code_generator_emit_drop(cg, elem_ptr, elem_type);
        LLVMValueRef i_next = LLVMBuildAdd(cg->builder, i_val, LLVMConstInt(i64_type, 1, 0), "clear_i_next");
        LLVMBuildStore(cg->builder, i_next, i_alloca);
        LLVMBuildBr(cg->builder, loop_cond_bb);
        LLVMPositionBuilderAtEnd(cg->builder, loop_after_bb);
        LLVMBuildBr(cg->builder, merge_bb);
        LLVMPositionBuilderAtEnd(cg->builder, else_bb);
        LLVMBuildBr(cg->builder, merge_bb);
        LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
    }
    // Free data if not null
    LLVMValueRef is_null = LLVMBuildICmp(cg->builder, LLVMIntNE, data,
                                         LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), "clear_free_not_null");
    LLVMBasicBlockRef free_then_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "clear_free_then");
    LLVMBasicBlockRef free_else_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "clear_free_else");
    LLVMBasicBlockRef free_merge_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, LLVMGetBasicBlockParent(LLVMGetInsertBlock(cg->builder)),
                                      "clear_free_merge");
    LLVMBuildCondBr(cg->builder, is_null, free_then_bb, free_else_bb);
    LLVMPositionBuilderAtEnd(cg->builder, free_then_bb);
    LLVMValueRef free_fn = LLVMGetNamedFunction(cg->mod, "free");
    assert(free_fn);
    LLVMTypeRef free_type =
        LLVMFunctionType(LLVMVoidTypeInContext(cg->ctx), (LLVMTypeRef[]){LLVMPointerTypeInContext(cg->ctx, 0)}, 1, 0);
    LLVMValueRef args[1] = {data};
    LLVMBuildCall2(cg->builder, free_type, free_fn, args, 1, "");
    LLVMBuildBr(cg->builder, free_merge_bb);
    LLVMPositionBuilderAtEnd(cg->builder, free_else_bb);
    LLVMBuildBr(cg->builder, free_merge_bb);
    LLVMPositionBuilderAtEnd(cg->builder, free_merge_bb);
    // Reset fields
    LLVMBuildStore(cg->builder, LLVMConstNull(LLVMPointerTypeInContext(cg->ctx, 0)), data_ptr_ptr);
    LLVMBuildStore(cg->builder, LLVMConstInt(i64_type, 0, 0), len_ptr);
    LLVMBuildStore(cg->builder, LLVMConstInt(i64_type, 0, 0), cap_ptr);
    return LLVMGetUndef(LLVMVoidTypeInContext(cg->ctx));
}
