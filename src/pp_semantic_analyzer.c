#include "pp_semantic_analyzer.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define SCOPE_LEVEL_GLOBAL (0)
#define SCOPE_LEVEL_FUNC_PARAMS (1)
#define SCOPE_LEVEL_FUNC_BODY (2)

typedef enum {
    PP_STRUCT_CYCLE_UNVISITED,
    PP_STRUCT_CYCLE_RESOLVING,
    PP_STRUCT_CYCLE_RESOLVED
} PP_StructRecursiveCycleState;

// TODO: There are two passes, and certain checks in pass 2 are unnecessary if pass 1 does not allow them.
// In which case, either push as error, or abort immediately (marking as "unreachable")

// TODO: Check building decoration again.

/*
 * Semantic Analysis Checks (Pass 1 and Pass 2)
 *
 * Pass 1 (Build Global Table):
 *   - Detect duplicate symbols for functions, structs, variables, typedefs, newtypes.
 *   - For functions: check for previous definition.
 *   - For structs: check for previous definition.
 *   - For variables: check for previous definition in the current scope.
 *                  Disallow reference types for global variables.
 *   - For typedefs:
 *        * Ensure base type is defined.
 *        * Ensure base type is not const or reference.
 *        * Check for duplicate typedef name.
 *   - For newtypes:
 *        * Ensure base type is defined.
 *        * Ensure base type is not const or reference.
 *        * Check for duplicate newtype name.
 *        * Detect cycles in newtype definition (e.g., newtype A = B; newtype B = A;).
 *   - General:
 *        * Maintain separate symbol tables per scope (to be implemented).
 *        * Better error handling for type resolution.
 *
 * Pass 2 (Full Analysis):
 *   - Structs:
 *        * Ensure struct is defined and of correct kind.
 *        * Detect duplicate field names.
 *        * Populate struct layout with fields.
 *        * Verify each field type is well-defined.
 *        * For fields with initializers:
 *             * Check expression validity and type resolution.
 *             * Require constexpr initializer.
 *             * Ensure type match between field and initializer (after decay).
 *             * If field is a reference, initializer must be an l-value.
 *             * Const-correctness: non-const reference cannot bind to const value.
 *             * Store default initializer for the field.
 *   - Global Variables:
 *        * Look up variable symbol.
 *        * If no initializer:
 *             * Error if type is const or reference.
 *             * For struct types, ensure all const/reference fields have default initializers.
 *        * Check expression validity and type resolution for initializer.
 *        * Require constexpr initializer for globals.
 *        * If variable is a reference:
 *             * Initializer must be an l-value.
 *             * Const-correctness: non-const reference cannot bind to const value.
 *        * Ensure type match between variable and initializer (after decay).
 *        * Update symbol's constness/constexpr flags.
 *   - Functions:
 *        * Look up function symbol and verify kind.
 *        * Set current function return type for checking.
 *        * If return type is non-void, verify all control paths return a value.
 *        * Create parameter scope.
 *        * Verify parameter count matches declaration.
 *        * Detect duplicate parameter names.
 *        * Create symbols for parameters with correct types and escapable reference flag.
 *        * Create function body scope.
 *        * Check statement list in the body.
 *        * Wire scopes to AST nodes.
 *   - Lambdas (similar to functions):
 *        * Look up lambda symbol and verify kind.
 *        * Set current return type.
 *        * If return type is non-void, verify all paths return.
 *        * Create parameter scope.
 *        * Verify parameter count and detect duplicate names.
 *        * Create parameter symbols.
 *        * Create body scope.
 *        * Check statement list in body.
 *        * Wire scopes to AST.
 *   - Expressions (checked during Pass 2):
 *        * Verify binary operations for type compatibility.
 *        * Verify unary operations for type compatibility.
 *        * Verify literal expressions.
 *        * Verify identifier expressions (variable lookup and type).
 *        * Verify struct access (field exists and access type matches).
 *        * Verify struct initialization (matches struct layout).
 *        * Verify function calls (argument types match parameter types).
 *        * Verify cast expressions.
 */

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

static int pp_array_size_from_simple_expr(PP_SemanticAnalyzer* sa, const PP_AST_Node* simple_expr_node, size_t* val);
static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node);
#define PP_TYPE_VISIT_MAX 128
static bool pp_type_contains_reference(const PPType* type);
static bool pp_type_contains_reference_impl(const PPType* t, const PPType** stack, int depth);
/** @brief True if a handle is reachable without crossing a reference. This is the ownership test:
 * `H` contains a handle, `H&` does not (a borrow carries no ownership). */
static bool pp_type_contains_handle(const PPType* type);
/** @brief True if a handle is reachable even through references. Used where a borrow of a handle is as
 * dangerous as the handle itself (underlying type, struct fields, collection elements). */
static bool pp_type_mentions_handle(const PPType* type);
static bool pp_type_handle_scan_impl(const PPType* t, bool through_references, const PPType** stack, int depth);
/** @brief Structural equality used to validate a drop function's parameter against a handle's underlying
 * type. Chained newtypes are compared by machine type, pointer types structurally (they are not interned). */
static bool pp_type_same_for_drop(const PPType* a, const PPType* b);
static bool pp_ast_node_returns_on_all_paths(const PP_AST_Node* node);
static int pp_type_validate_collection_element(PP_SemanticAnalyzer* sa, const PPType* elem_type, PP_SourceLoc loc);
static int pp_semantic_check_static_string_size_mismatch(PP_SemanticAnalyzer* sa, PP_SourceLoc loc, const char* context,
                                                         const PPType* lhs_type, const PPType* rhs_type);
/* Pass I */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa);
static int pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_operator_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_builtin_unary_supported(const char* op, PPTypeKind lk);
static int pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_extern_function_decl(PP_SemanticAnalyzer* sa,
                                                                        const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_extern_operator_decl(PP_SemanticAnalyzer* sa,
                                                                        const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_extern_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_struct_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_variable_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_constant_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_handle_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_enum_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_flag_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_eval_int_constant(PP_SemanticAnalyzer* sa, const PP_AST_Node* expr, PPTypeKind underlying,
                                         const PP_EnumMember* members, size_t member_count, unsigned long long* out);

/* Pass II */
static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa);
static int pp_semantic_analyzer_full_analyze_struct_decl_fields(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_handle_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_struct_cycle_detect(PP_SemanticAnalyzer* sa, const PPType* type,
                                                    PP_StructRecursiveCycleState* states);
static int pp_semantic_analyzer_full_analyze_struct_decl_initializers(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_function_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_pure_callable(PP_SemanticAnalyzer* sa, PP_AST_Node* node,
                                                           PPTypeKind expected_type_kind);
static int pp_semantic_analyzer_full_analyze_operator_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_lambda_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_global_variable_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_constant_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);

/* Function Bodies */
static int pp_semantic_analyzer_full_analyze_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                       PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_variable_declaration(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                                  PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_assignment_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                                  PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_return_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                              PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_if_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                          PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_for_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                           PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_while_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                             PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_statement_list(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                            PP_AST_Node* node);

/* Expression Checking */
static int pp_semantic_analyzer_check_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                 PP_AST_Node* expr);
static int pp_semantic_analyzer_check_function_call(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                    PP_AST_Node* expr);
static int pp_semantic_analyzer_check_binary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                        PP_AST_Node* expr);
static int pp_semantic_analyzer_check_unary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                       PP_AST_Node* expr);
static int pp_semantic_analyzer_check_literal_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                         PP_AST_Node* expr);
static int pp_semantic_analyzer_check_null_literal_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                              PP_AST_Node* expr);
static int pp_semantic_analyzer_check_identifier_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                            PP_AST_Node* expr);
static int pp_semantic_analyzer_check_array_list_access_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                                   PP_AST_Node* expr);
static int pp_semantic_analyzer_check_member_access_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                               PP_AST_Node* expr);
static int pp_semantic_analyzer_check_struct_initializer_expression(PP_SemanticAnalyzer* sa,
                                                                    PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_cast_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                      PP_AST_Node* expr);
static int pp_semantic_analyzer_check_qualified_constant_expression(PP_SemanticAnalyzer* sa, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_collection_initializer_expression(PP_SemanticAnalyzer* sa,
                                                                        PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_list_push(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                PP_AST_Node* expr);
static int pp_semantic_analyzer_check_list_pop(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_list_clear(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                 PP_AST_Node* expr);

static bool pp_type_is_top_level_const(const PPType* type);
static bool pp_type_can_assign(const PPType* target, const PPType* source);
static bool pp_type_is_callable(const PPType* type);
static bool pp_semantic_callable_signature_equal(const PPType* a, const PPType* b);

/**
 * @brief Creates semantic analyzer from parser
 */
PP_SemanticAnalyzer* pp_semantic_analyzer_create(PP_Parser* parser, PP_StringPool* sp) {
    PP_SemanticAnalyzer* sa = NULL;
    PP_GlobalTypeTable* gtt = NULL;
    PPSymbolTable* gst = NULL;

    if (!parser) {
        pp_error_list_push_error(sa->error_list, (PP_SourceLoc){.filename = NULL, .line = 0, .column = 0},
                                 "Parser is NULL");
        goto error_cleanup;
    }

    sa = (PP_SemanticAnalyzer*)calloc(1, sizeof(PP_SemanticAnalyzer));
    if (!sa) {
        pp_error_list_push_error(NULL, (PP_SourceLoc){.filename = 0, .line = 0, .column = 0},
                                 "Failed to allocate memory for semantic analyzer");
        goto error_cleanup;
    }

    /* Transfer ownership of parser resources */

    sa->code = parser->code;
    parser->code = NULL;
    sa->code_length = parser->code_length;

    sa->tokens = parser->tokens;
    parser->tokens = NULL;
    sa->n_tokens = parser->n_tokens;

    sa->program = parser->program;
    parser->program = NULL;

    sa->sp = sp;

    sa->error_list = parser->error_list;
    parser->error_list = NULL;

    sa->current_function_type = NULL;
    sa->current_loop_depth = 0;

    gtt = pp_global_type_table_create();
    if (!gtt) {
        pp_error_list_push_error(sa->error_list, (PP_SourceLoc){.filename = NULL, .line = 0, .column = 0},
                                 "Failed to create global type table");
        goto error_cleanup;
    }

    gst = pp_symbol_table_create(SCOPE_LEVEL_GLOBAL);
    if (!gst) {
        pp_error_list_push_error(sa->error_list, (PP_SourceLoc){.filename = NULL, .line = 0, .column = 0},
                                 "Failed to create symbol table");
        goto error_cleanup;
    }

    sa->gtt = gtt;
    gtt = NULL;

    sa->gst = gst;
    gst = NULL;

    /* Optionally pre-intern primitive types */
    {
        PPType* t = NULL;
        t = pp_type_create(PP_TK_Void);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "void", 4);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Int32);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "int32", 5);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_UInt32);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "uint32", 6);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Float);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "float", 5);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Bool);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "bool", 4);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Int8);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "int8", 4);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_UInt8);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "uint8", 5);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Int16);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "int16", 5);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_UInt16);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "uint16", 6);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Int64);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "int64", 5);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_UInt64);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "uint64", 6);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_Int128);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "int128", 6);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_UInt128);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "uint128", 7);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
        t = pp_type_create(PP_TK_String);
        if (t) {
            const char* type_name = pp_string_pool_push(sa->sp, "string", 6);
            NULL_POINTER_ERROR_HANDLE(type_name);
            const PPType* type_interned = pp_global_type_table_intern(sa->gtt, t);
            NULL_POINTER_ERROR_HANDLE(type_interned);
            if (!pp_global_type_table_register_alias(sa->gtt, type_name, type_interned)) goto error_cleanup;
            t = NULL;
        }
    }

    sa->operator_overloads = (PP_OperatorOverloadRegistry*)calloc(1, sizeof(PP_OperatorOverloadRegistry));
    if (!sa->operator_overloads) {
        pp_error_list_push_error(sa->error_list, (PP_SourceLoc){.filename = NULL, .line = 0, .column = 0},
                                 "Failed to allocate operator overload registry");
        goto error_cleanup;
    }

    return sa;
error_cleanup:
    if (sa) {
        free((char*)sa->code);
        free(sa->tokens);
        if (sa->error_list) { pp_error_list_free(sa->error_list); }
        pp_global_type_table_free(sa->gtt);
        if (sa->operator_overloads) free(sa->operator_overloads->entries);
        free(sa->operator_overloads);
        pp_symbol_table_free(sa->gst);
    }
    free(sa);
    return NULL;
}

/**
 * @brief Frees semantic analyzer and its components
 */
void pp_semantic_analyzer_free(PP_SemanticAnalyzer* sa) {
    if (!sa) return;

    free((char*)sa->code);
    free(sa->tokens);
    pp_ast_root_free(sa->program);
    /* String pool ownership is external; do not free */
    // pp_string_pool_free(sa->sp);
    pp_error_list_free(sa->error_list);
    pp_global_type_table_free(sa->gtt);
    pp_symbol_table_free(sa->gst);

    if (sa->operator_overloads) {
        free(sa->operator_overloads->entries);
        free(sa->operator_overloads);
    }

    free(sa);
}

/**
 * @brief Invoke semantic analyzer to analyze the AST.
 */
int pp_semantic_analyzer_analyze(PP_SemanticAnalyzer* sa) {
    if (!sa) return 0;

    if (!pp_semantic_analyzer_build_global_table(sa)) { return 0; }
    if (!pp_semantic_analyzer_full_analyze(sa)) { return 0; }

    return 1;
}

static bool pp_type_contains_reference(const PPType* t) {
    if (!t) return false;
    const PPType* stack[128] = {0};
    return pp_type_contains_reference_impl(t, stack, 0);
}

static bool pp_type_contains_reference_impl(const PPType* t, const PPType** stack, int depth) {
    if (!t) return false;
    if (depth > 0) {
        for (int i = 0; i < depth; i++) {
            if (stack[i] == t) return false;
        }
    }
    if (depth >= 128) {
        fprintf(stderr, "WARN: pp_type_contains_reference depth >= 128\n");
        return false;
    }
    stack[depth] = t;
    bool result = false;
    switch (t->kind) {
        case PP_TK_Reference:
            result = true;
            break;
        case PP_TK_Const:
            result = pp_type_contains_reference_impl(t->type.pp_type_const.const_of, stack, depth + 1);
            break;
        case PP_TK_Array:
            result = pp_type_contains_reference_impl(t->type.pp_type_array.element_type, stack, depth + 1);
            break;
        case PP_TK_List:
            result = pp_type_contains_reference_impl(t->type.pp_type_list.element_type, stack, depth + 1);
            break;
        case PP_TK_Newtype:
            result = pp_type_contains_reference_impl(t->type.pp_type_newtype.underlying, stack, depth + 1);
            break;
        case PP_TK_Struct:
            if (t->type.pp_type_struct.layout) {
                for (size_t i = 0; i < t->type.pp_type_struct.layout->field_count; i++) {
                    if (pp_type_contains_reference_impl(t->type.pp_type_struct.layout->fields[i].field_type, stack,
                                                        depth + 1)) {
                        result = true;
                        break;
                    }
                }
            }
            break;
        case PP_TK_Function:
        case PP_TK_Lambda:
        case PP_TK_Operator:
            /* A callable value is a code pointer; its signature is part of the
             * type identity only. References inside the signature are not
             * stored in the value. */
            result = false;
            break;
        default:
            result = false;
            break;
    }
    return result;
}

static bool pp_type_same_for_drop(const PPType* a, const PPType* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    const PPType* ra = pp_type_resolve_machine_type(a);
    const PPType* rb = pp_type_resolve_machine_type(b);
    if (ra == rb) return true;
    if (!ra || !rb || ra->kind != rb->kind) return false;

    if (ra->kind == PP_TK_Pointer) {
        return pp_type_same_for_drop(ra->type.pp_type_pointer.pointee, rb->type.pp_type_pointer.pointee);
    }

    return false;
}

static bool pp_type_contains_handle(const PPType* t) {
    if (!t) return false;
    const PPType* stack[PP_TYPE_VISIT_MAX] = {0};
    return pp_type_handle_scan_impl(t, false, stack, 0);
}

static bool pp_type_mentions_handle(const PPType* t) {
    if (!t) return false;
    const PPType* stack[PP_TYPE_VISIT_MAX] = {0};
    return pp_type_handle_scan_impl(t, true, stack, 0);
}

static bool pp_type_handle_scan_impl(const PPType* t, bool through_references, const PPType** stack, int depth) {
    if (!t) return false;
    if (depth > 0) {
        for (int i = 0; i < depth; i++) {
            if (stack[i] == t) return false;
        }
    }
    if (depth >= PP_TYPE_VISIT_MAX) {
        fprintf(stderr, "WARN: pp_type_handle_scan depth >= %d\n", PP_TYPE_VISIT_MAX);
        return false;
    }
    stack[depth] = t;

    if (pp_type_is_handle(t)) return true;

    switch (t->kind) {
        case PP_TK_Reference:
            if (!through_references) return false;
            return pp_type_handle_scan_impl(t->type.pp_type_reference.reference_to, through_references, stack,
                                            depth + 1);
        case PP_TK_Const:
            return pp_type_handle_scan_impl(t->type.pp_type_const.const_of, through_references, stack, depth + 1);
        case PP_TK_Array:
            return pp_type_handle_scan_impl(t->type.pp_type_array.element_type, through_references, stack, depth + 1);
        case PP_TK_List:
            return pp_type_handle_scan_impl(t->type.pp_type_list.element_type, through_references, stack, depth + 1);
        case PP_TK_Newtype:
            return pp_type_handle_scan_impl(t->type.pp_type_newtype.underlying, through_references, stack, depth + 1);
        case PP_TK_Struct: {
            const PP_StructLayout* layout = t->type.pp_type_struct.layout;
            if (!layout) return false;
            for (size_t i = 0; i < layout->field_count; i++) {
                if (pp_type_handle_scan_impl(layout->fields[i].field_type, through_references, stack, depth + 1)) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

static bool pp_ast_node_returns_on_all_paths(const PP_AST_Node* node) {
    if (!node) return false;
    switch (node->node_type) {
        case PP_AST_StatementListNode:
            for (unsigned int i = 0; i < node->statement_list.stmt_count; i++) {
                if (pp_ast_node_returns_on_all_paths(node->statement_list.stmts[i])) return true;
            }
            return false;
        case PP_AST_ReturnStatementNode:
            return true;
        case PP_AST_IfStatementNode: {
            bool then_ret = pp_ast_node_returns_on_all_paths(node->if_statement.then_branch);
            bool else_ret = node->if_statement.else_branch
                                ? pp_ast_node_returns_on_all_paths(node->if_statement.else_branch)
                                : false;
            return then_ret && else_ret;
        }
        case PP_AST_WhileStatementNode:
        case PP_AST_ForStatementNode:
            return false;
        default:
            return false;
    }
}

static int pp_semantic_check_static_string_size_mismatch(PP_SemanticAnalyzer* sa, PP_SourceLoc loc, const char* context,
                                                         const PPType* lhs_type, const PPType* rhs_type) {
    const PPType* dl = pp_type_decay(sa->gtt, lhs_type);
    const PPType* dr = pp_type_decay(sa->gtt, rhs_type);
    if (!dl || !dr) return 1;
    if (dl->kind != PP_TK_Array || dr->kind != PP_TK_Array) return 1;
    const PPType* lhs_elem = dl->type.pp_type_array.element_type;
    const PPType* rhs_elem = dr->type.pp_type_array.element_type;
    if (!lhs_elem || !rhs_elem) return 1;
    if (lhs_elem->kind != PP_TK_UInt8 || rhs_elem->kind != PP_TK_UInt8) return 1;
    size_t lhs_size = dl->type.pp_type_array.size;
    size_t rhs_size = dr->type.pp_type_array.size;
    if (lhs_size != rhs_size) {
        pp_error_list_push_error(sa->error_list, loc,
                                 "Static string literal size mismatch %s: target array size is %zu but literal "
                                 "requires %zu",
                                 context, lhs_size, rhs_size);
        return 0;
    }
    return 1;
}

/** @brief True when the (already decayed) type is a primitive integer kind.
 * Call only on a decayed type: const/reference are not unwrapped here, and enum/flag newtypes are
 * deliberately not integer primitives. */
static bool pp_semantic_type_is_decayed_integer_primitive(const PPType* type) {
    return type != NULL && pp_type_kind_is_integer(type->kind);
}

/** @brief Resolves `Type::Member` for an enum/flag type. Returns false when the type is unknown, is not an
 * enum/flag, or the member does not exist. `Flag::none` is synthetic and yields 0. */
static bool pp_semantic_lookup_qualified_constant_value(PP_SemanticAnalyzer* sa, const char* type_name,
                                                        const char* member, unsigned long long* out_value,
                                                        const PPType** out_type) {
    assert(sa);
    assert(type_name);
    assert(member);
    assert(out_value);

    const PPType* type = pp_global_type_table_find_by_name(sa->gtt, type_name);
    if (!type) type = pp_global_type_table_find_alias(sa->gtt, type_name);
    if (!type || !pp_type_is_enum_or_flag(type)) return false;
    if (out_type) *out_type = type;

    if (pp_type_is_flag(type) && strcmp(member, "none") == 0) {
        *out_value = 0;
        return true;
    }

    const PP_EnumMember* found = pp_type_find_enum_member(type, member);
    if (!found) return false;
    *out_value = found->value;
    return true;
}

static int pp_array_size_from_simple_expr(PP_SemanticAnalyzer* sa, const PP_AST_Node* simple_expr_node, size_t* val) {
    assert(sa);
    assert(simple_expr_node);
    assert(val);

    switch (simple_expr_node->node_type) {
        case PP_AST_LiteralNode: {
            if (simple_expr_node->literal.literal_type != PP_TT_INTEGER_LITERAL) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "For array size expression, literals can only be unsigned integer");
                goto error_cleanup;
            }
            const char* lex = simple_expr_node->literal.lexeme;
            PPTypeKind k = pp_type_parse_int_literal_suffix(lex, NULL);
            // Size literals must be one of the supported unsigned integer types (uint8 .. uint64); a signed
            // literal or an unsupported unsigned width such as uint128 fails this membership test.
            if (!pp_type_kind_is_unsigned_up_to_64(k)) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "For array size expression, literals must be uint8, uint16, uint32 or uint64");
                goto error_cleanup;
            }
            char numbuf[PP_INT_LITERAL_NUMBUF_SIZE] = {0};
            pp_type_parse_int_literal_suffix(lex, numbuf);
            char* endptr;
            errno = 0;
            unsigned long long parsed_val = strtoull(numbuf, &endptr, 10);
            if (errno != 0 || simple_expr_node->literal.lexeme == endptr) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc, "Could not parse literal to number");
                goto error_cleanup;
            }
            if (parsed_val > SIZE_MAX) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "Array size literal out of range for size_t");
                goto error_cleanup;
            }
            *val = (size_t)parsed_val;
            return 1;
        } break;
        case PP_AST_QualifiedConstantNode: {
            unsigned long long constant_value = 0;
            const PPType* constant_type = NULL;
            if (!pp_semantic_lookup_qualified_constant_value(sa, simple_expr_node->qualified_constant.type_name,
                                                             simple_expr_node->qualified_constant.member,
                                                             &constant_value, &constant_type)) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "Array size expression '%s::%s' is not a known enum or flag constant",
                                         simple_expr_node->qualified_constant.type_name,
                                         simple_expr_node->qualified_constant.member);
                goto error_cleanup;
            }
            // A size is non-negative by construction: a constant of a signed enum could name a negative value,
            // so only flags and enums with an unsigned underlying type are usable as sizes.
            {
                const PPType* constant_underlying = (constant_type && constant_type->kind == PP_TK_Newtype)
                                                        ? constant_type->type.pp_type_newtype.underlying
                                                        : NULL;
                if (!constant_underlying || pp_type_kind_is_signed(constant_underlying->kind)) {
                    pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                             "Array size constant '%s::%s' must have an unsigned underlying type",
                                             simple_expr_node->qualified_constant.type_name,
                                             simple_expr_node->qualified_constant.member);
                    goto error_cleanup;
                }
            }
            if (constant_value > SIZE_MAX) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "Array size constant out of range for size_t");
                goto error_cleanup;
            }
            *val = (size_t)constant_value;
            return 1;
        } break;
        case PP_AST_IdentifierNode: {
            const PPSymbol* constant_symbol =
                pp_symbol_table_lookup(sa->gst, simple_expr_node->identifier.identifier, true);
            if (!constant_symbol || !constant_symbol->is_constant_decl) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "Array size identifier '%s' is not a constant",
                                         simple_expr_node->identifier.identifier);
                goto error_cleanup;
            }
            if (!pp_type_kind_is_unsigned_up_to_64(constant_symbol->type->kind)) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "Array size constant '%s' must have an unsigned integer type "
                                         "(uint8, uint16, uint32 or uint64)",
                                         simple_expr_node->identifier.identifier);
                goto error_cleanup;
            }
            if (!constant_symbol->constant_initializer) {
                pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                         "Constant '%s' must be declared before it is used as an array size",
                                         simple_expr_node->identifier.identifier);
                goto error_cleanup;
            }
            return pp_array_size_from_simple_expr(sa, constant_symbol->constant_initializer, val);
        } break;
        case PP_AST_BinaryExpressionNode: {
            switch (simple_expr_node->binary_expression.op) {
                case PP_TT_PLUS:
                case PP_TT_STAR:
                case PP_TT_SLASH:
                case PP_TT_PERCENT:
                case PP_TT_LESS_LESS:
                case PP_TT_GREATER_GREATER:
                    break;
                default:
                    pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                             "The only binary operator allowed in array size are \'+\', \'*\', \'/\', "
                                             "\'%\', \'<<\', and \'>>\'");
                    goto error_cleanup;
            }

            size_t left_val;
            size_t right_val;
            if (!pp_array_size_from_simple_expr(sa, simple_expr_node->binary_expression.left, &left_val)) {
                goto error_cleanup;
            }
            if (!pp_array_size_from_simple_expr(sa, simple_expr_node->binary_expression.right, &right_val)) {
                goto error_cleanup;
            }

            switch (simple_expr_node->binary_expression.op) {
                case PP_TT_PLUS:
                    if (left_val > SIZE_MAX - right_val) {
                        pp_error_list_push_error(sa->error_list, simple_expr_node->loc, "Array size addition overflow");
                        goto error_cleanup;
                    }
                    *val = left_val + right_val;
                    break;
                case PP_TT_STAR:
                    if (left_val != 0 && right_val > SIZE_MAX / left_val) {
                        pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                                 "Array size multiplication overflow");
                        goto error_cleanup;
                    }
                    *val = left_val * right_val;
                    break;
                case PP_TT_SLASH:
                    if (right_val == 0) {
                        pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                                 "Division by zero in array size expression");
                        goto error_cleanup;
                    }
                    *val = left_val / right_val;
                    break;
                case PP_TT_PERCENT:
                    if (right_val == 0) {
                        pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                                 "Modulo by zero in array size expression");
                        goto error_cleanup;
                    }
                    *val = left_val % right_val;
                    break;
                case PP_TT_LESS_LESS:
                    if (right_val >= sizeof(size_t) * 8) {
                        pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                                 "Shift amount too large in array size expression");
                        goto error_cleanup;
                    }
                    *val = left_val << right_val;
                    break;
                case PP_TT_GREATER_GREATER:
                    if (right_val >= sizeof(size_t) * 8) {
                        pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                                 "Shift amount too large in array size expression");
                        goto error_cleanup;
                    }
                    *val = left_val >> right_val;
                    break;
                default:
                    pp_error_list_push_error(sa->error_list, simple_expr_node->loc,
                                             "Unsupported operator in array size expression");
                    goto error_cleanup;
            }
            return 1;
        } break;
        default:
            goto error_cleanup;
    }

error_cleanup:
    return 0;
}

static int pp_type_validate_collection_element(PP_SemanticAnalyzer* sa, const PPType* elem_type, PP_SourceLoc loc) {
    if (!elem_type) {
        pp_error_list_push_error(sa->error_list, loc, "Element type is null");
        return 0;
    }
    if (pp_type_is_reference(elem_type)) {
        pp_error_list_push_error(sa->error_list, loc, "element type cannot be a reference");
        return 0;
    }
    if (elem_type->kind == PP_TK_Void) {
        pp_error_list_push_error(sa->error_list, loc, "Array/List element type cannot be void");
        return 0;
    }
    if (pp_type_contains_reference(elem_type)) {
        pp_error_list_push_error(sa->error_list, loc,
                                 "Array/List element type cannot transitively contain a reference");
        return 0;
    }
    if (pp_type_mentions_handle(elem_type)) {
        pp_error_list_push_error(sa->error_list, loc, "Array/List element type cannot contain a handle");
        return 0;
    }
    return 1;
}

static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node) {
    assert(sa);
    assert(type_node);
    assert(type_node->node_type == PP_AST_TypeExpressionNode);

    PPType* temp_type = NULL;                 /* owned – can be freed on error */
    const PPType* gtt_lookedup_type = NULL;   /* For alias and named types */
    const PPType* gtt_registered_type = NULL; /* borrowed from GTT – must not be freed here */

    // Resolve base type via new recursive AST or legacy fields
    const PP_AST_Node* inner = type_node->type_expression.type_node;
    assert(inner);
    // TypeExpression must have type_node after migration
    PP_AST_TypeKind base_kind = PP_AST_TYPE_KIND_IDENTIFIER;
    const char* base_identifier = NULL;

    if (inner->node_type == PP_AST_TypeBaseNode) {
        base_kind = inner->type_base.kind;
        base_identifier = inner->type_base.identifier_name;
    } else if (inner->node_type != PP_AST_TypeArrayNode && inner->node_type != PP_AST_TypeListNode &&
               inner->node_type != PP_AST_TypePointerNode && inner->node_type != PP_AST_TypeCallableNode) {
        pp_error_list_push_error(sa->error_list, type_node->loc, "Unsupported type node");
        goto error_cleanup;
    }

    // Handle collection / pointer / callable types from new AST
    if (inner->node_type == PP_AST_TypeArrayNode || inner->node_type == PP_AST_TypeListNode ||
        inner->node_type == PP_AST_TypePointerNode || inner->node_type == PP_AST_TypeCallableNode) {
        if (inner->node_type == PP_AST_TypePointerNode) {
            const PPType* pointee_type = pp_type_from_type_node(sa, inner->type_pointer.pointee_type);
            if (!pointee_type) goto error_cleanup;
            // Disallow mixing pointer and reference
            if (pp_type_is_reference(pointee_type) || pp_type_contains_reference(pointee_type)) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Pointer cannot contain reference");
                goto error_cleanup;
            }
            // Pointers to `func[...]` are supported: a code pointer is an ordinary pointer.
            // Pointers to `lambda[...]` remain banned.
            {
                const PPType* decayed_pointee = pp_type_decay(sa->gtt, pointee_type);
                if (pp_type_is_callable(decayed_pointee) && decayed_pointee->kind != PP_TK_Function) {
                    pp_error_list_push_error(sa->error_list, type_node->loc, "Pointer cannot point to a lambda type");
                    goto error_cleanup;
                }
            }
            temp_type = pp_type_create(PP_TK_Pointer);
            if (!temp_type) goto error_cleanup;
            temp_type->type.pp_type_pointer.pointee = pointee_type;
            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            temp_type = NULL;
            if (!gtt_registered_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to intern pointer type");
                goto error_cleanup;
            }
        } else if (inner->node_type == PP_AST_TypeArrayNode || inner->node_type == PP_AST_TypeListNode) {
            const PPType* elem_type = NULL;
            if (inner->node_type == PP_AST_TypeArrayNode) {
                elem_type = pp_type_from_type_node(sa, inner->type_array.element_type);
            } else {
                elem_type = pp_type_from_type_node(sa, inner->type_list.element_type);
            }
            if (!elem_type) goto error_cleanup;
            if (!pp_type_validate_collection_element(sa, elem_type, type_node->loc)) goto error_cleanup;
            if (inner->node_type == PP_AST_TypeArrayNode) {
                size_t sz = 0;
                if (!pp_array_size_from_simple_expr(sa, inner->type_array.size_expr, &sz)) {
                    pp_error_list_push_error(sa->error_list, type_node->loc, "Could not compute array size");
                    goto error_cleanup;
                }
                if (sz == 0) {
                    pp_error_list_push_error(sa->error_list, type_node->loc, "Array size cannot be zero");
                    goto error_cleanup;
                }
                temp_type = pp_type_create(PP_TK_Array);
                if (!temp_type) goto error_cleanup;
                temp_type->type.pp_type_array.element_type = elem_type;
                temp_type->type.pp_type_array.size = sz;
            } else {
                temp_type = pp_type_create(PP_TK_List);
                if (!temp_type) goto error_cleanup;
                temp_type->type.pp_type_list.element_type = elem_type;
            }
            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            temp_type = NULL;
            if (!gtt_registered_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to intern array/list");
                goto error_cleanup;
            }
        } else {
            PP_AST_CallableKind callable_kind = inner->type_callable.kind;
            PPTypeKind callable_type_kind = (callable_kind == PP_CK_Lambda) ? PP_TK_Lambda : PP_TK_Function;
            unsigned int i = 0;

            assert(callable_kind == PP_CK_Function || callable_kind == PP_CK_Lambda);

            if (!inner->type_callable.return_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc,
                                         "Callable type must have an explicit return type");
                goto error_cleanup;
            }

            temp_type = pp_type_create(callable_type_kind);
            if (!temp_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to create callable type");
                goto error_cleanup;
            }

            for (i = 0; i < inner->type_callable.param_count; i++) {
                const PPType* param_type = pp_type_from_type_node(sa, inner->type_callable.param_types[i]);
                if (!param_type) {
                    pp_error_list_push_error(sa->error_list, type_node->loc, "Invalid parameter type in callable type");
                    goto error_cleanup;
                }
                if (param_type->kind == PP_TK_Void) {
                    pp_error_list_push_error(sa->error_list, type_node->loc, "Callable type parameter cannot be void");
                    goto error_cleanup;
                }
                if (!pp_type_push_param(temp_type, param_type)) {
                    pp_error_list_push_error(sa->error_list, type_node->loc,
                                             "Failed to add parameter type to callable type");
                    goto error_cleanup;
                }
            }

            const PPType* ret_type = pp_type_from_type_node(sa, inner->type_callable.return_type);
            if (!ret_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Invalid return type in callable type");
                goto error_cleanup;
            }
            if (callable_type_kind == PP_TK_Function) {
                temp_type->type.pp_type_function.return_type = ret_type;
            } else {
                temp_type->type.pp_type_lambda.return_type = ret_type;
            }

            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            if (!gtt_registered_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to intern callable type");
                goto error_cleanup;
            }
            temp_type = NULL;
        }
    } else {
        switch (base_kind) {
            case PP_AST_TYPE_KIND_VOID:
                temp_type = pp_type_create(PP_TK_Void);
                break;
            case PP_AST_TYPE_KIND_INT8:
                temp_type = pp_type_create(PP_TK_Int8);
                break;
            case PP_AST_TYPE_KIND_UINT8:
                temp_type = pp_type_create(PP_TK_UInt8);
                break;
            case PP_AST_TYPE_KIND_INT16:
                temp_type = pp_type_create(PP_TK_Int16);
                break;
            case PP_AST_TYPE_KIND_UINT16:
                temp_type = pp_type_create(PP_TK_UInt16);
                break;
            case PP_AST_TYPE_KIND_INT32:
                temp_type = pp_type_create(PP_TK_Int32);
                break;
            case PP_AST_TYPE_KIND_UINT32:
                temp_type = pp_type_create(PP_TK_UInt32);
                break;
            case PP_AST_TYPE_KIND_INT64:
                temp_type = pp_type_create(PP_TK_Int64);
                break;
            case PP_AST_TYPE_KIND_UINT64:
                temp_type = pp_type_create(PP_TK_UInt64);
                break;
            case PP_AST_TYPE_KIND_INT128:
                temp_type = pp_type_create(PP_TK_Int128);
                break;
            case PP_AST_TYPE_KIND_UINT128:
                temp_type = pp_type_create(PP_TK_UInt128);
                break;
            case PP_AST_TYPE_KIND_FLOAT:
                temp_type = pp_type_create(PP_TK_Float);
                break;
            case PP_AST_TYPE_KIND_BOOL:
                temp_type = pp_type_create(PP_TK_Bool);
                break;
            case PP_AST_TYPE_KIND_STRING:
                temp_type = pp_type_create(PP_TK_String);
                break;
            case PP_AST_TYPE_KIND_IDENTIFIER:
                gtt_lookedup_type = pp_global_type_table_find_by_name(sa->gtt, base_identifier);
                if (gtt_lookedup_type) { break; }
                gtt_lookedup_type = pp_global_type_table_find_alias(sa->gtt, base_identifier);
                if (gtt_lookedup_type) { break; }
                break;
            default:
                abort();
                break;
        }
        if (!temp_type && !gtt_lookedup_type) {
            pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to create type");
            goto error_cleanup;
        }

        // gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type ? temp_type : gtt_lookedup_type);
        gtt_registered_type = temp_type ? pp_global_type_table_intern(sa->gtt, temp_type) : gtt_lookedup_type;
        temp_type = NULL;
        if (!gtt_registered_type) {
            pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to intern type");
            goto error_cleanup;
        }
    }

    if (type_node->type_expression.is_const) {
        // Canonicalize const for arrays/lists by pushing const inward
        if (gtt_registered_type &&
            (gtt_registered_type->kind == PP_TK_Array || gtt_registered_type->kind == PP_TK_List)) {
            // Build a new collection with const element type
            const PPType* elem = NULL;
            size_t sz = 0;
            if (gtt_registered_type->kind == PP_TK_Array) {
                elem = gtt_registered_type->type.pp_type_array.element_type;
                sz = gtt_registered_type->type.pp_type_array.size;
            } else {
                elem = gtt_registered_type->type.pp_type_list.element_type;
            }
            // Create const element type
            PPType* const_elem = pp_type_create(PP_TK_Const);
            if (!const_elem) goto error_cleanup;
            const_elem->type.pp_type_const.const_of = elem;
            const PPType* const_elem_interned = pp_global_type_table_intern(sa->gtt, const_elem);
            const_elem = NULL;
            if (!const_elem_interned) goto error_cleanup;
            // Rebuild collection with const element
            temp_type =
                (gtt_registered_type->kind == PP_TK_Array) ? pp_type_create(PP_TK_Array) : pp_type_create(PP_TK_List);
            if (!temp_type) goto error_cleanup;
            if (gtt_registered_type->kind == PP_TK_Array) {
                temp_type->type.pp_type_array.element_type = const_elem_interned;
                temp_type->type.pp_type_array.size = sz;
            } else {
                temp_type->type.pp_type_list.element_type = const_elem_interned;
            }
            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            temp_type = NULL;
            if (!gtt_registered_type) goto error_cleanup;
        } else {
            // Regular const wrapping for non-collection types
            temp_type = pp_type_create(PP_TK_Const);
            if (!temp_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to create const type");
                goto error_cleanup;
            }
            temp_type->type.pp_type_const.const_of = gtt_registered_type;
            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            temp_type = NULL;
            if (!gtt_registered_type) {
                pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to intern const type");
                goto error_cleanup;
            }
        }
    }

    if (type_node->type_expression.is_reference) {
        // Disallow mixing pointer and reference
        if (pp_type_is_pointer(gtt_registered_type) || pp_type_contains_pointer(gtt_registered_type)) {
            pp_error_list_push_error(sa->error_list, type_node->loc, "Reference cannot contain pointer");
            goto error_cleanup;
        }
        temp_type = pp_type_create(PP_TK_Reference);
        if (!temp_type) {
            pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to create reference type");
            goto error_cleanup;
        }
        temp_type->type.pp_type_reference.reference_to = gtt_registered_type;
        gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
        temp_type = NULL;
        if (!gtt_registered_type) {
            pp_error_list_push_error(sa->error_list, type_node->loc, "Failed to intern reference type");
            goto error_cleanup;
        }
    }

    return gtt_registered_type;

error_cleanup:
    pp_type_free(temp_type);
    return NULL;
}

/* --- Shell stubs for internal passes --- */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa) {
    NULL_POINTER_ERROR_HANDLE(sa);

    NULL_POINTER_ERROR_HANDLE(sa->program);

    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        const PP_AST_Node* decl = sa->program->program.decls[i];
        switch (decl->node_type) {
            case PP_AST_CallableDeclarationNode:
                switch (decl->callable_declaration.kind) {
                    case PP_CK_Function:
                        pp_semantic_analyzer_build_global_table_function_decl(sa, decl);
                        break;
                    case PP_CK_Operator:
                        pp_semantic_analyzer_build_global_table_operator_decl(sa, decl);
                        break;
                    case PP_CK_Lambda:
                        pp_semantic_analyzer_build_global_table_lambda_decl(sa, decl);
                        break;
                }
                break;
            case PP_AST_ExternCallableDeclarationNode:
                switch (decl->extern_callable_declaration.kind) {
                    case PP_CK_Function:
                        pp_semantic_analyzer_build_global_table_extern_function_decl(sa, decl);
                        break;
                    case PP_CK_Operator:
                        pp_semantic_analyzer_build_global_table_extern_operator_decl(sa, decl);
                        break;
                    case PP_CK_Lambda:
                        pp_semantic_analyzer_build_global_table_extern_lambda_decl(sa, decl);
                        break;
                }
                break;
            case PP_AST_StructDeclarationNode:
                pp_semantic_analyzer_build_global_table_struct_decl(sa, decl);
                break;
            case PP_AST_VariableDeclarationNode:
                pp_semantic_analyzer_build_global_table_variable_decl(sa, decl);
                break;
            case PP_AST_ConstantDeclarationNode:
                pp_semantic_analyzer_build_global_table_constant_decl(sa, decl);
                break;
            case PP_AST_TypedefDeclarationNode:
                pp_semantic_analyzer_build_global_table_typedef_decl(sa, decl);
                break;
            case PP_AST_NewtypeDeclarationNode:
                pp_semantic_analyzer_build_global_table_newtype_decl(sa, decl);
                break;
            case PP_AST_HandleDeclarationNode:
                pp_semantic_analyzer_build_global_table_handle_decl(sa, decl);
                break;
            case PP_AST_EnumDeclarationNode:
                pp_semantic_analyzer_build_global_table_enum_decl(sa, decl);
                break;
            case PP_AST_FlagDeclarationNode:
                pp_semantic_analyzer_build_global_table_flag_decl(sa, decl);
                break;
            default:
                assert(0 && "Semantic Analyzer Pass 1 default case. This should have been handled at parser.");
                abort();
        }
    }

    return 1;

error_cleanup:
    return 0;
}

static bool pp_semantic_analyzer_is_operator_symbol(const char* name) {
    if (!name) return false;
    const char* const operator_symbols[] = {"+",  "-",  "*",  "/", "%", "&",  "|",  "^", "<<",
                                            ">>", "==", "!=", "<", ">", "<=", ">=", "~"};
    for (size_t i = 0; i < sizeof(operator_symbols) / sizeof(operator_symbols[0]); i++) {
        if (strcmp(name, operator_symbols[i]) == 0) return true;
    }
    return false;
}

/* Pass 1 - Function Declaration Checklist:
 * [x] Detect duplicate symbol
 * [x] Build function type and intern in GTT
 * [x] Create symbol and register in global symbol table
 */
/*
 * Handle Signature Checklist:
 * [x] A handle may not be passed by value anywhere.
 * [x] A handle behind a reference (Handle& / const Handle&) is a borrow: allowed in function and operator
 *     signatures.
 * [x] Lambdas cannot mention a handle at all - not as a parameter, not as a return type - because a lambda
 *     cannot own a handle and its body cannot drop one.
 */
static int pp_validate_signature_handles(PP_SemanticAnalyzer* sa, PP_SourceLoc loc, const char* kind_str,
                                         const char* name, const PPType* return_type, PP_AST_Node* const* params,
                                         unsigned int param_count, bool is_lambda) {
    if (is_lambda && return_type && pp_type_mentions_handle(return_type)) {
        pp_error_list_push_error(sa->error_list, loc, "%s '%s' cannot use a handle type as its return type", kind_str,
                                 name);
        return 0;
    }

    for (unsigned int i = 0; i < param_count; i++) {
        const PPType* param_type = pp_type_from_type_node(sa, params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, loc, "Invalid parameter type for %s '%s'", kind_str, name);
            return 0;
        }
        if (is_lambda) {
            if (pp_type_mentions_handle(param_type)) {
                pp_error_list_push_error(sa->error_list, loc, "%s '%s' cannot use a handle type as a parameter type",
                                         kind_str, name);
                return 0;
            }
        } else if (pp_type_contains_handle(param_type)) {
            pp_error_list_push_error(sa->error_list, loc,
                                     "%s '%s' cannot take a handle by value; pass it by reference ('Handle&')",
                                     kind_str, name);
            return 0;
        }
    }

    return 1;
}

static int pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(node->callable_declaration.kind == PP_CK_Function);
    PPType* tmp_func = NULL;
    const PPType* func_type = NULL;
    PPSymbol* func_symbol = NULL;

    tmp_func = pp_type_create(PP_TK_Function);
    if (!tmp_func) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate memory for function type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->callable_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid return type for function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    if (!pp_validate_signature_handles(sa, node->loc, "Function",
                                       node->callable_declaration.name ? node->callable_declaration.name : "<unknown>",
                                       return_type, node->callable_declaration.params,
                                       node->callable_declaration.param_count, false)) {
        goto error_cleanup;
    }
    // Assign read-only return_type pointer.
    tmp_func->type.pp_type_function.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
        const PPType* param_type =
            pp_type_from_type_node(sa, node->callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, node->loc, "Invalid parameter type for function '%s'",
                                     node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_func, param_type)) { goto error_cleanup; }
    }

    // Add to type table
    func_type = pp_global_type_table_intern(sa->gtt, tmp_func);
    if (!func_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern function type for function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    tmp_func = NULL;

    // Duplicate handling with forward declaration support
    const PPSymbol* existing = pp_symbol_table_lookup(sa->gst, node->callable_declaration.name, false);
    if (existing) {
        if (existing->type == func_type) {
            if (existing->is_definition) {
                // Duplicate definition
                pp_error_list_push_error(sa->error_list, node->loc, "Symbol '%s' is previously defined.",
                                         node->callable_declaration.name ? node->callable_declaration.name
                                                                         : "<unknown>");
                goto error_cleanup;
            }
            // Existing is a forward declaration, identical type → allow upgrade
            return 1;
        } else {
            pp_error_list_push_error(sa->error_list, node->loc, "Conflicting types for symbol '%s'",
                                     node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
            goto error_cleanup;
        }
    }

    // Create symbol
    func_symbol = pp_symbol_create(func_type, false, false, true, true, true);
    if (!func_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Register to symbol table.
    if (!pp_symbol_table_push(sa->gst, node->callable_declaration.name, func_symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    func_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_func);
    pp_symbol_free(func_symbol);
    return 0;
}

static int pp_builtin_binary_supported(const char* op, PPTypeKind lk, PPTypeKind rk) {
    if (!op) return 0;
    if (strcmp(op, "+") == 0) {
        if (lk == PP_TK_Float && rk == PP_TK_Float) return 1;
        if (lk == PP_TK_String && rk == PP_TK_String) return 1;
        for (PPTypeKind k = PP_TK_Int8; k <= PP_TK_UInt128; k += 1) {
            /* only matching int kinds; fall through discrete below */
        }
        /* explicit integer kinds */
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "-") == 0 || strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
        if (lk == PP_TK_Float && rk == PP_TK_Float) return 1;
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "%") == 0) {
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
        if (lk == PP_TK_Bool && rk == PP_TK_Bool) return 1;
        if (lk == PP_TK_Float && rk == PP_TK_Float) return 1;
        if (lk == PP_TK_String && rk == PP_TK_String) return 1;
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
        if (lk == PP_TK_Float && rk == PP_TK_Float) return 1;
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0) {
        const PPTypeKind kinds[] = {PP_TK_Int8,   PP_TK_UInt8, PP_TK_Int16,  PP_TK_UInt16, PP_TK_Int32,
                                    PP_TK_UInt32, PP_TK_Int64, PP_TK_UInt64, PP_TK_Int128, PP_TK_UInt128};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i] && rk == kinds[i]) return 1;
        }
        return 0;
    }
    return 0;
}

static int pp_builtin_unary_supported(const char* op, PPTypeKind lk) {
    if (!op) return 0;
    if (strcmp(op, "-") == 0) {
        const PPTypeKind kinds[] = {PP_TK_Int8, PP_TK_Int16, PP_TK_Int32, PP_TK_Int64, PP_TK_Int128, PP_TK_Float};
        for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
            if (lk == kinds[i]) return 1;
        }
        return 0;
    }
    if (strcmp(op, "!") == 0) { return lk == PP_TK_Bool; }
    if (strcmp(op, "~") == 0) { return pp_type_kind_is_integer(lk); }
    return 0;
}

static int pp_semantic_analyzer_build_global_table_operator_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(node->callable_declaration.kind == PP_CK_Operator);
    const char* original_spelling = node->callable_declaration.name;
    PPType* tmp_func = NULL;
    const PPType* func_type = NULL;
    PPSymbol* func_symbol = NULL;

    tmp_func = pp_type_create(PP_TK_Operator);
    if (!tmp_func) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate memory for function type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->callable_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid return type for function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    if (!pp_validate_signature_handles(sa, node->loc, "Operator",
                                       node->callable_declaration.name ? node->callable_declaration.name : "<unknown>",
                                       return_type, node->callable_declaration.params,
                                       node->callable_declaration.param_count, false)) {
        goto error_cleanup;
    }
    // Assign read-only return_type pointer.
    tmp_func->type.pp_type_function.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
        const PPType* param_type =
            pp_type_from_type_node(sa, node->callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, node->loc, "Invalid parameter type for function '%s'",
                                     node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_func, param_type)) { goto error_cleanup; }
    }

    if (pp_semantic_analyzer_is_operator_symbol(node->callable_declaration.name)) {
        for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
            const PPType* param_type = tmp_func->type.pp_type_function.param_types[i];
            if (!pp_type_is_const(param_type) || !pp_type_is_reference(param_type)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Operator '%s' parameters must be const references.",
                                         node->callable_declaration.name);
                goto error_cleanup;
            }
        }
        if (pp_type_is_reference(return_type)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Operator '%s' return type must be non-reference.",
                                     node->callable_declaration.name);
            goto error_cleanup;
        }
    }
    if (pp_semantic_analyzer_is_operator_symbol(node->callable_declaration.name)) {
        // operator parameters already constrained by syntax; structurally enforce here
        for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
            const PPType* param_type = tmp_func->type.pp_type_function.param_types[i];
            if (!pp_type_is_const(param_type) || !pp_type_is_reference(param_type)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Operator '%s' parameters must be const references.",
                                         node->callable_declaration.name);
                goto error_cleanup;
            }
        }
        if (pp_type_is_reference(return_type)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Operator '%s' return type must be non-reference.",
                                     node->callable_declaration.name);
            goto error_cleanup;
        }
    }

    // Name mangling for overloaded operators to avoid symbol-table collisions.
    // Identity is based on decayed/interned parameter type pointers, which remain stable.
    // Return type is intentionally excluded from the key to avoid hiding pass-mode overload conflicts.
    const PPType* decayed_p[2] = {NULL, NULL};
    for (size_t i = 0; i < node->callable_declaration.param_count && i < 2; i++) {
        const PPType* param_type = tmp_func->type.pp_type_function.param_types[i];
        const PPType* decayed = pp_type_decay(sa->gtt, param_type);
        if (!decayed) goto error_cleanup;
        decayed_p[i] = decayed;
    }

    // Enum and flag types are closed sets: their operators are built in, so a user operator may not mention
    // them at all. Rejecting at declaration also prevents silently overriding built-in ==/!=/|/&/^/~ through the
    // decoration's resolved_operator_callable lookup.
    if (pp_semantic_analyzer_is_operator_symbol(node->callable_declaration.name) &&
        ((decayed_p[0] && pp_type_is_enum_or_flag(decayed_p[0])) ||
         (decayed_p[1] && pp_type_is_enum_or_flag(decayed_p[1])))) {
        pp_error_list_push_error(sa->error_list, node->loc, "Operator '%s' cannot be declared for enum or flag types.",
                                 node->callable_declaration.name);
        goto error_cleanup;
    }

    if (pp_semantic_analyzer_is_operator_symbol(node->callable_declaration.name) &&
        node->callable_declaration.param_count >= 2 && decayed_p[0] && decayed_p[1]) {
        if (pp_builtin_binary_supported(node->callable_declaration.name, decayed_p[0]->kind, decayed_p[1]->kind)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Operator '%s' cannot redefine supported built-in operator pair.",
                                     node->callable_declaration.name);
            goto error_cleanup;
        }
    }

    if (pp_semantic_analyzer_is_operator_symbol(node->callable_declaration.name) &&
        node->callable_declaration.param_count == 1 && decayed_p[0]) {
        const bool redefines_builtin_unary =
            pp_builtin_unary_supported(node->callable_declaration.name, decayed_p[0]->kind);
        if (redefines_builtin_unary) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Operator '%s' cannot redefine supported built-in unary operator.",
                                     node->callable_declaration.name);
            goto error_cleanup;
        }
    }

    char mangle_buf[128];
    size_t pos = 0;
    int written = snprintf(mangle_buf, sizeof(mangle_buf), "__pp@op@%s@n%zu", node->callable_declaration.name,
                           (size_t)node->callable_declaration.param_count);
    if (written < 0 || (size_t)written >= sizeof(mangle_buf)) goto error_cleanup;
    pos = (size_t)written;
    for (size_t i = 0; i < node->callable_declaration.param_count && i < 2; i++) {
        int appended = snprintf(mangle_buf + pos, sizeof(mangle_buf) - pos, "@%p", (void*)decayed_p[i]);
        if (appended < 0 || (size_t)(pos + appended) >= sizeof(mangle_buf)) goto error_cleanup;
        pos += (size_t)appended;
    }
    const char* mangled_name = pp_string_pool_push(sa->sp, mangle_buf, pos);
    if (!mangled_name) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern mangled name for operator '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Intern type before publishing the mangled symbol name.
    func_type = pp_global_type_table_intern(sa->gtt, tmp_func);
    if (!func_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern function type for function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    tmp_func = NULL;

    ((PP_AST_Node*)node)->callable_declaration.name = mangled_name;

    mangled_name = NULL;

    // Duplicate handling with forward declaration support (uses the mangled key)
    const PPSymbol* existing = pp_symbol_table_lookup(sa->gst, node->callable_declaration.name, false);
    if (existing) {
        if (existing->type == func_type) {
            if (existing->is_definition) {
                pp_error_list_push_error(sa->error_list, node->loc, "Symbol '%s' is previously defined.",
                                         node->callable_declaration.name ? node->callable_declaration.name
                                                                         : "<unknown>");
                goto error_cleanup;
            }
            return 1;
        } else {
            pp_error_list_push_error(sa->error_list, node->loc, "Conflicting types for symbol '%s'",
                                     node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
            goto error_cleanup;
        }
    }

    // Create symbol

    func_symbol = pp_symbol_create(func_type, false, false, true, true, true);
    if (!func_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Register to symbol table (name already mangled above).
    if (!pp_symbol_table_push(sa->gst, node->callable_declaration.name, func_symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register function '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    // Populate overloaded operator registry for binary operators.
    if (pp_semantic_analyzer_is_operator_symbol(original_spelling) && node->callable_declaration.param_count >= 1 &&
        node->callable_declaration.param_count <= 2) {
        PP_OperatorOverloadRegistry* reg = sa->operator_overloads;
        if (reg->count == reg->capacity) {
            size_t new_cap = reg->capacity ? reg->capacity * 2 : 8;
            PP_OperatorOverloadEntry* ne = realloc(reg->entries, new_cap * sizeof(PP_OperatorOverloadEntry));
            if (!ne) goto error_cleanup;
            reg->entries = ne;
            reg->capacity = new_cap;
        }
        reg->entries[reg->count].spelling = original_spelling;
        reg->entries[reg->count].arity = node->callable_declaration.param_count;
        reg->entries[reg->count].lhs_type = decayed_p[0];
        reg->entries[reg->count].rhs_type = (node->callable_declaration.param_count == 2) ? decayed_p[1] : NULL;
        reg->entries[reg->count].callable = func_symbol;
        reg->count++;
    }

    func_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_func);
    pp_symbol_free(func_symbol);
    return 0;
}

/* Pass 1 - Lambda Declaration Checklist:
 * [x] Detect duplicate symbol
 * [x] Build lambda type and intern in GTT
 * [x] Create symbol and register in global symbol table
 */
static int pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(node->callable_declaration.kind == PP_CK_Lambda);
    PPType* tmp_lambda = NULL;
    const PPType* lambda_type = NULL;
    PPSymbol* lambda_symbol = NULL;

    tmp_lambda = pp_type_create(PP_TK_Lambda);
    if (!tmp_lambda) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate memory for lambda type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->callable_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid return type for lambda");
        goto error_cleanup;
    }
    if (!pp_validate_signature_handles(sa, node->loc, "Lambda",
                                       node->callable_declaration.name ? node->callable_declaration.name : "<unknown>",
                                       return_type, node->callable_declaration.params,
                                       node->callable_declaration.param_count, true)) {
        goto error_cleanup;
    }

    // Assign read-only return_type pointer.
    tmp_lambda->type.pp_type_lambda.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
        const PPType* param_type =
            pp_type_from_type_node(sa, node->callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, node->loc, "Invalid parameter type for lambda '%s'",
                                     node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_lambda, param_type)) { goto error_cleanup; }
    }

    // Add to type table
    lambda_type = pp_global_type_table_intern(sa->gtt, tmp_lambda);
    if (!lambda_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern lambda type for lambda '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    tmp_lambda = NULL;

    // Duplicate handling with forward declaration support
    const PPSymbol* existing = pp_symbol_table_lookup(sa->gst, node->callable_declaration.name, false);
    if (existing) {
        if (existing->type == lambda_type) {
            if (existing->is_definition) {
                pp_error_list_push_error(sa->error_list, node->loc, "Symbol '%s' is previously defined.",
                                         node->callable_declaration.name ? node->callable_declaration.name
                                                                         : "<unknown>");
                goto error_cleanup;
            }
            return 1;
        } else {
            pp_error_list_push_error(sa->error_list, node->loc, "Conflicting types for symbol '%s'",
                                     node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
            goto error_cleanup;
        }
    }

    // Create symbol
    lambda_symbol = pp_symbol_create(lambda_type, false, false, true, true, true);
    if (!lambda_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for lambda '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Register to symbol table.
    if (!pp_symbol_table_push(sa->gst, node->callable_declaration.name, lambda_symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register lambda '%s'",
                                 node->callable_declaration.name ? node->callable_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    lambda_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_lambda);
    pp_symbol_free(lambda_symbol);
    return 0;
}

/* Pass 1 - Extern Function Declaration Checklist: */
static int pp_semantic_analyzer_build_global_table_extern_function_decl(PP_SemanticAnalyzer* sa,
                                                                        const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_ExternCallableDeclarationNode);
    assert(node->extern_callable_declaration.kind == PP_CK_Function);
    PPType* tmp_func = NULL;
    const PPType* func_type = NULL;
    PPSymbol* func_symbol = NULL;

    tmp_func = pp_type_create(PP_TK_Function);
    if (!tmp_func) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate memory for function type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->extern_callable_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid return type for function '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        if (!pp_validate_signature_handles(sa, node->loc, "Function",
                                           node->extern_callable_declaration.name
                                               ? node->extern_callable_declaration.name
                                               : "<unknown>",
                                           return_type, node->extern_callable_declaration.params,
                                           node->extern_callable_declaration.param_count, false)) {
            goto error_cleanup;
        }
        goto error_cleanup;
    }

    tmp_func->type.pp_type_function.return_type = return_type;

    for (size_t i = 0; i < node->extern_callable_declaration.param_count; i++) {
        const PPType* param_type =
            pp_type_from_type_node(sa, node->extern_callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, node->loc, "Invalid parameter type for function '%s'",
                                     node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                            : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_func, param_type)) { goto error_cleanup; }
    }

    if (pp_semantic_analyzer_is_operator_symbol(node->extern_callable_declaration.name)) {
        for (size_t i = 0; i < node->extern_callable_declaration.param_count; i++) {
            const PPType* param_type = tmp_func->type.pp_type_function.param_types[i];
            if (!pp_type_is_const(param_type) || !pp_type_is_reference(param_type)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Operator '%s' parameters must be const references.",
                                         node->extern_callable_declaration.name);
                goto error_cleanup;
            }
        }
        if (pp_type_is_reference(return_type)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Operator '%s' return type must be non-reference.",
                                     node->extern_callable_declaration.name);
            goto error_cleanup;
        }
    }

    func_type = pp_global_type_table_intern(sa->gtt, tmp_func);
    if (!func_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern function type for function '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }
    tmp_func = NULL;

    // Duplicate handling with forward declaration support
    const PPSymbol* existing = pp_symbol_table_lookup(sa->gst, node->extern_callable_declaration.name, false);
    if (existing) {
        if (existing->type == func_type) {
            return 1;
        } else {
            pp_error_list_push_error(sa->error_list, node->loc, "Conflicting types for symbol '%s'",
                                     node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                            : "<unknown>");
            goto error_cleanup;
        }
    }

    func_symbol = pp_symbol_create(func_type, false, false, true, false, true);
    if (!func_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for function '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }

    if (!pp_symbol_table_push(sa->gst, node->extern_callable_declaration.name, func_symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register function '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }
    func_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_func);
    pp_symbol_free(func_symbol);
    return 0;
}

/* Pass 1 - Extern Operator Declaration Checklist: */
static int pp_semantic_analyzer_build_global_table_extern_operator_decl(PP_SemanticAnalyzer* sa,
                                                                        const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_ExternCallableDeclarationNode);
    assert(node->extern_callable_declaration.kind == PP_CK_Operator);
    PPType* tmp_func = NULL;
    const PPType* func_type = NULL;
    PPSymbol* func_symbol = NULL;

    tmp_func = pp_type_create(PP_TK_Operator);
    if (!tmp_func) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate memory for operator type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->extern_callable_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid return type for operator '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }

    if (!pp_validate_signature_handles(sa, node->loc, "Extern operator",
                                       node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                              : "<unknown>",
                                       return_type, node->extern_callable_declaration.params,
                                       node->extern_callable_declaration.param_count, false)) {
        goto error_cleanup;
    }

    tmp_func->type.pp_type_operator.return_type = return_type;

    for (size_t i = 0; i < node->extern_callable_declaration.param_count; i++) {
        const PPType* param_type =
            pp_type_from_type_node(sa, node->extern_callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, node->loc, "Invalid parameter type for operator '%s'",
                                     node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                            : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_func, param_type)) { goto error_cleanup; }
    }

    // Operator-specific validation - enforced in Pass 1
    for (size_t i = 0; i < node->extern_callable_declaration.param_count; i++) {
        const PPType* param_type = tmp_func->type.pp_type_operator.param_types[i];
        if (!pp_type_is_const(param_type) || !pp_type_is_reference(param_type)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Operator '%s' parameters must be const references.",
                                     node->extern_callable_declaration.name);
            goto error_cleanup;
        }
        // Enum and flag operators are built in, so an extern operator may not mention them either.
        const PPType* decayed_param = pp_type_decay(sa->gtt, param_type);
        if (!decayed_param) goto error_cleanup;
        if (pp_type_is_enum_or_flag(decayed_param)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Operator '%s' cannot be declared for enum or flag types.",
                                     node->extern_callable_declaration.name);
            goto error_cleanup;
        }
    }
    // Builtin operator pairs are reserved: an extern operator may not silently take them over through the
    // decoration's resolved_operator_callable lookup, which would bypass the generated builtin semantics.
    if (node->extern_callable_declaration.param_count == 2) {
        const PPType* decayed_p0 = pp_type_decay(sa->gtt, tmp_func->type.pp_type_operator.param_types[0]);
        if (!decayed_p0) goto error_cleanup;
        const PPType* decayed_p1 = pp_type_decay(sa->gtt, tmp_func->type.pp_type_operator.param_types[1]);
        if (!decayed_p1) goto error_cleanup;
        if (pp_builtin_binary_supported(node->extern_callable_declaration.name, decayed_p0->kind, decayed_p1->kind)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Operator '%s' cannot redefine supported built-in operator pair.",
                                     node->extern_callable_declaration.name);
            goto error_cleanup;
        }
    }
    if (pp_type_is_reference(return_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Operator '%s' return type must be non-reference.",
                                 node->extern_callable_declaration.name);
        goto error_cleanup;
    }

    func_type = pp_global_type_table_intern(sa->gtt, tmp_func);
    if (!func_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern function type for operator '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }
    tmp_func = NULL;

    const PPSymbol* existing = pp_symbol_table_lookup(sa->gst, node->extern_callable_declaration.name, false);
    if (existing) {
        if (existing->type == func_type) {
            return 1;
        } else {
            pp_error_list_push_error(sa->error_list, node->loc, "Conflicting types for symbol '%s'",
                                     node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                            : "<unknown>");
            goto error_cleanup;
        }
    }

    func_symbol = pp_symbol_create(func_type, false, false, true, false, true);
    if (!func_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for operator '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }

    if (!pp_symbol_table_push(sa->gst, node->extern_callable_declaration.name, func_symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register operator '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }
    func_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_func);
    pp_symbol_free(func_symbol);
    return 0;
}

/* Pass 1 - Extern Lambda Declaration Checklist: */
static int pp_semantic_analyzer_build_global_table_extern_lambda_decl(PP_SemanticAnalyzer* sa,
                                                                      const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_ExternCallableDeclarationNode);
    assert(node->extern_callable_declaration.kind == PP_CK_Lambda);
    PPType* tmp_lambda = NULL;
    const PPType* lambda_type = NULL;
    PPSymbol* lambda_symbol = NULL;

    tmp_lambda = pp_type_create(PP_TK_Lambda);
    if (!tmp_lambda) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate memory for lambda type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->extern_callable_declaration.return_type);
    if (!pp_validate_signature_handles(sa, node->loc, "Extern lambda",
                                       node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                              : "<unknown>",
                                       return_type, node->extern_callable_declaration.params,
                                       node->extern_callable_declaration.param_count, true)) {
        goto error_cleanup;
    }
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid return type for lambda");
        goto error_cleanup;
    }

    tmp_lambda->type.pp_type_lambda.return_type = return_type;

    for (size_t i = 0; i < node->extern_callable_declaration.param_count; i++) {
        const PPType* param_type =
            pp_type_from_type_node(sa, node->extern_callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, node->loc, "Invalid parameter type for lambda '%s'",
                                     node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                            : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_lambda, param_type)) { goto error_cleanup; }
    }

    lambda_type = pp_global_type_table_intern(sa->gtt, tmp_lambda);
    if (!lambda_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern lambda type for lambda '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }
    tmp_lambda = NULL;

    // Duplicate handling with forward declaration support
    const PPSymbol* existing = pp_symbol_table_lookup(sa->gst, node->extern_callable_declaration.name, false);
    if (existing) {
        if (existing->type == lambda_type) {
            return 1;
        } else {
            pp_error_list_push_error(sa->error_list, node->loc, "Conflicting types for symbol '%s'",
                                     node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                            : "<unknown>");
            goto error_cleanup;
        }
    }

    lambda_symbol = pp_symbol_create(lambda_type, false, false, true, false, true);
    if (!lambda_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for lambda '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }

    if (!pp_symbol_table_push(sa->gst, node->extern_callable_declaration.name, lambda_symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register lambda '%s'",
                                 node->extern_callable_declaration.name ? node->extern_callable_declaration.name
                                                                        : "<unknown>");
        goto error_cleanup;
    }
    lambda_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_lambda);
    pp_symbol_free(lambda_symbol);
    return 0;
}

/* Pass 1 - Struct Declaration Checklist:
 * [x] Detect duplicate type name in GTT
 * [x] Create struct type with NULL layout (defer field processing to Pass 2)
 * [x] Intern struct type in GTT
 */
static int pp_semantic_analyzer_build_global_table_struct_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_StructDeclarationNode);
    PPType* tmp_struct = NULL;
    const PPType* struct_type = NULL;
    PP_StructLayout* struct_layout = NULL;

    // Duplicate type name check in GTT (named types and aliases)
    if (pp_global_type_table_find_by_name(sa->gtt, node->struct_declaration.name) != NULL ||
        pp_global_type_table_find_alias(sa->gtt, node->struct_declaration.name) != NULL) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type '%s' previously defined.",
                                 node->struct_declaration.name);
        goto error_cleanup;
    }

    // Field processing deferred to Pass 2 - layout stays NULL in Pass 1

    tmp_struct = pp_type_create(PP_TK_Struct);
    if (!tmp_struct) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate struct type for struct '%s'",
                                 node->struct_declaration.name);
        goto error_cleanup;
    }

    /* Assign */
    tmp_struct->type.pp_type_struct.layout = NULL; /* deferred to Pass 2 */
    tmp_struct->type.pp_type_struct.name = node->struct_declaration.name;

    /* Add to type table */
    struct_type = pp_global_type_table_intern(sa->gtt, tmp_struct);
    if (!struct_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern struct type for struct '%s'",
                                 node->struct_declaration.name);
        goto error_cleanup;
    }
    tmp_struct = NULL;

    if (!pp_global_type_table_register_named(sa->gtt, node->struct_declaration.name, struct_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register struct type for struct '%s'",
                                 node->struct_declaration.name);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    pp_type_free(tmp_struct);
    pp_struct_layout_free(struct_layout);
    return 0;
}

/* Pass 1 - Variable Declaration Checklist:
 * [x] Detect duplicate symbol in current scope
 * [x] Disallow reference types for global variables
 * [x] Create symbol and register in global symbol table
 */
static int pp_semantic_analyzer_build_global_table_variable_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_VariableDeclarationNode);
    assert(node->variable_declaration.var_decl->node_type == PP_AST_ParamDeclarationNode);
    assert(node->variable_declaration.var_decl->param_declaration.identifier);
    PPSymbol* symbol = NULL;

    // Duplicate symbol check
    if (pp_symbol_table_lookup(sa->gst, node->variable_declaration.var_decl->param_declaration.identifier, false)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Symbol '%s' previously defined.",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    const PPType* var_type =
        pp_type_from_type_node(sa, node->variable_declaration.var_decl->param_declaration.type_expr);
    if (!var_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid type for variable '%s'",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }
    if (pp_type_is_reference(var_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Global variable '%s' cannot be a reference",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    // A handle is owned by the scope that declares it, so it cannot outlive a function body.
    if (pp_type_mentions_handle(var_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Global variable '%s' cannot have a handle type",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    symbol = pp_symbol_create(var_type, false, false, true, true, false);
    if (!symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for variable '%s'",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    if (!pp_symbol_table_push(sa->gst, node->variable_declaration.var_decl->param_declaration.identifier, symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register variable '%s'",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }
    symbol = NULL;

    return 1;
error_cleanup:
    pp_symbol_free(symbol);
    return 0;
}

/* Pass 1 - Constant Declaration Checklist:
 * [x] Validate inputs
 * [x] Detect duplicate symbol in the global scope
 * [x] The declared type must be a plain integer, float, or bool primitive
 * [x] Initializer must be present (also enforced by the parser)
 * [x] Create a constexpr global symbol marked is_constant_decl
 * [x] Store the non-owning initializer pointer for array-size folding
 */
static int pp_semantic_analyzer_build_global_table_constant_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    PPSymbol* symbol = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_ConstantDeclarationNode);
    assert(node->constant_declaration.identifier);
    assert(node->constant_declaration.type_expr);

    const char* name = node->constant_declaration.identifier;

    if (pp_symbol_table_lookup(sa->gst, name, false)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Symbol '%s' previously defined.", name);
        goto error_cleanup;
    }

    if (!node->constant_declaration.initializer) {
        pp_error_list_push_error(sa->error_list, node->loc, "Constant '%s' requires an initializer.", name);
        goto error_cleanup;
    }

    const PPType* constant_type = pp_type_from_type_node(sa, node->constant_declaration.type_expr);
    if (!constant_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid type for constant '%s'", name);
        goto error_cleanup;
    }

    if (!pp_type_is_numerical(constant_type) && constant_type->kind != PP_TK_Bool) {
        pp_error_list_push_error(sa->error_list, node->loc, "Constant '%s' must have an integer, float, or bool type",
                                 name);
        goto error_cleanup;
    }

    symbol = pp_symbol_create(constant_type, true, false, true, true, false);
    if (!symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate symbol for constant '%s'", name);
        goto error_cleanup;
    }
    symbol->is_constant_decl = true;
    symbol->constant_initializer = node->constant_declaration.initializer;

    if (!pp_symbol_table_push(sa->gst, name, symbol)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register constant '%s'", name);
        goto error_cleanup;
    }
    symbol = NULL;

    return 1;
error_cleanup:
    pp_symbol_free(symbol);
    return 0;
}

/* Pass 1 - Typedef Declaration Checklist:
 * [x] Ensure base type is defined
 * [x] Ensure base type is not const or reference
 * [x] Detect duplicate typedef name
 * [-] Detect cycles in typedef definition  -> Dead code (though might need to be supported if later supporting forward
 * declaration.) [x] Register alias in GTT
 */
static int pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_TypedefDeclarationNode);

    if (pp_global_type_table_find_alias(sa->gtt, node->typedef_declaration.identifier) ||
        pp_global_type_table_find_by_name(sa->gtt, node->typedef_declaration.identifier)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type '%s' previously defined",
                                 node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    const PPType* base_type = pp_type_from_type_node(sa, node->typedef_declaration.base_type);
    if (!base_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid base type for typedef '%s'",
                                 node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    if (pp_type_is_const(base_type) || pp_type_is_reference(base_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Base type for typedef '%s' cannot be const or reference",
                                 node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    if (!pp_global_type_table_register_alias(sa->gtt, node->typedef_declaration.identifier, base_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register typedef alias '%s'",
                                 node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    return 0;
}

/** @brief True when a flag value is a valid composite: a single bit, or the OR of the earlier members that
 * intersect it. Keeps composite members (e.g. `ReadWrite = Read | Write`) legal while rejecting arbitrary
 * values. */
static bool pp_semantic_flag_value_is_combination(const PP_EnumMember* members, size_t member_count,
                                                  unsigned long long value) {
    if (value == 0) return false;
    if ((value & (value - 1)) == 0) return true;

    unsigned long long combined = 0;
    for (size_t i = 0; i < member_count; i++) {
        if ((members[i].value & value) != 0) combined |= members[i].value;
    }
    return combined == value;
}

/** @brief Name of an integer primitive kind, for member-value diagnostics. */
static const char* pp_semantic_integer_kind_name(PPTypeKind kind) {
    switch (kind) {
        case PP_TK_Int8:
            return "int8";
        case PP_TK_UInt8:
            return "uint8";
        case PP_TK_Int16:
            return "int16";
        case PP_TK_UInt16:
            return "uint16";
        case PP_TK_Int32:
            return "int32";
        case PP_TK_UInt32:
            return "uint32";
        case PP_TK_Int64:
            return "int64";
        case PP_TK_UInt64:
            return "uint64";
        case PP_TK_Int128:
            return "int128";
        case PP_TK_UInt128:
            return "uint128";
        default:
            return "non-integer";
    }
}

/** @brief Bit width of an integer primitive kind (0 when the kind is not an integer primitive). */
static unsigned int pp_semantic_integer_kind_width(PPTypeKind kind) {
    switch (kind) {
        case PP_TK_Int8:
        case PP_TK_UInt8:
            return 8;
        case PP_TK_Int16:
        case PP_TK_UInt16:
            return 16;
        case PP_TK_Int32:
        case PP_TK_UInt32:
            return 32;
        case PP_TK_Int64:
        case PP_TK_UInt64:
            return 64;
        default:
            return 0;
    }
}

/** @brief Mask of the value bits of a kind. Member values are kept inside this range, so the operations on them
 * behave like arithmetic in the underlying type (modular for unsigned, two's complement for signed). */
static unsigned long long pp_semantic_integer_value_mask(PPTypeKind kind) {
    unsigned int width = pp_semantic_integer_kind_width(kind);
    if (width == 0) return 0;
    if (width >= 64) return UINT64_MAX;
    return (1ULL << width) - 1;
}

/** @brief Interpret the low `width` bits of `bits` as a signed value of `kind`. */
static long long pp_semantic_signed_value(PPTypeKind kind, unsigned long long bits) {
    unsigned int width = pp_semantic_integer_kind_width(kind);
    if (width == 0 || width >= 64) return (long long)bits;
    unsigned long long sign_bit = 1ULL << (width - 1);
    unsigned long long mask = (1ULL << width) - 1;
    return (bits & sign_bit) ? (long long)(bits | ~mask) : (long long)bits;
}

/** @brief Largest value representable by the signed kind `kind`. */
static long long pp_semantic_signed_max(PPTypeKind kind) {
    unsigned int width = pp_semantic_integer_kind_width(kind);
    if (width == 0 || width >= 64) return INT64_MAX;
    return (1LL << (width - 1)) - 1;
}

/** @brief Compare two member values in the value domain of `kind` (signed kinds compare as signed). */
static int pp_semantic_int_value_compare(PPTypeKind kind, unsigned long long a, unsigned long long b) {
    if (pp_type_kind_is_signed(kind)) {
        long long sa = pp_semantic_signed_value(kind, a);
        long long sb = pp_semantic_signed_value(kind, b);
        return (sa < sb) ? -1 : (sa > sb ? 1 : 0);
    }
    return (a < b) ? -1 : (a > b ? 1 : 0);
}

/** @brief Whether a non-negative literal value fits the value range of `kind`. A literal is checked before it is
 * masked, so `300uss` (uint8) is an error rather than a silently wrapped 44. */
static bool pp_semantic_int_literal_fits(PPTypeKind kind, unsigned long long value) {
    switch (kind) {
        case PP_TK_Int8:
            return value <= INT8_MAX;
        case PP_TK_UInt8:
            return value <= UINT8_MAX;
        case PP_TK_Int16:
            return value <= INT16_MAX;
        case PP_TK_UInt16:
            return value <= UINT16_MAX;
        case PP_TK_Int32:
            return value <= INT32_MAX;
        case PP_TK_UInt32:
            return value <= UINT32_MAX;
        case PP_TK_Int64:
            return value <= (unsigned long long)INT64_MAX;
        case PP_TK_UInt64:
            return true;
        default:
            return false;
    }
}

/**
 * Enum/Flag constant evaluation checklist (typed):
 * [x] Every explicit member value is a constant expression whose type is exactly the underlying type.
 * [x] An integer literal must carry the suffix of the underlying type; a char literal is a uint8 constant and
 *     therefore requires a uint8 underlying type.
 * [x] Unary '-' and '~', binary + - * / % << >> | & ^ with zero-division and shift-range checks.
 * [x] Results are masked to the width of the underlying type, i.e. modular/two's-complement arithmetic.
 * [x] Negative values are representable when the underlying type is signed.
 * [x] Anything else is rejected (earlier members of the same flag body are the only identifiers).
 */
static int pp_semantic_eval_int_constant(PP_SemanticAnalyzer* sa, const PP_AST_Node* expr, PPTypeKind underlying,
                                         const PP_EnumMember* members, size_t member_count, unsigned long long* out) {
    assert(sa);
    assert(expr);
    assert(out);

    const unsigned long long value_mask = pp_semantic_integer_value_mask(underlying);
    const unsigned int width = pp_semantic_integer_kind_width(underlying);
    const bool is_signed = pp_type_kind_is_signed(underlying);

    switch (expr->node_type) {
        case PP_AST_LiteralNode: {
            if (expr->literal.literal_type == PP_TT_CHAR_LITERAL) {
                // Character literals are uint8 constants, so only a uint8-backed enum or flag can use them.
                if (underlying != PP_TK_UInt8) {
                    pp_error_list_push_error(sa->error_list, expr->loc,
                                             "Character literal member values require a uint8 underlying type, not "
                                             "'%s'",
                                             pp_semantic_integer_kind_name(underlying));
                    goto error_cleanup;
                }
                const char* lexeme = expr->literal.lexeme;
                if (!lexeme || lexeme[0] != '\'' || strlen(lexeme) < 3) {
                    pp_error_list_push_error(sa->error_list, expr->loc, "Invalid char literal");
                    goto error_cleanup;
                }
                unsigned long long char_value = 0;
                if (lexeme[1] == '\\') {
                    switch (lexeme[2]) {
                        case 'n':
                            char_value = '\n';
                            break;
                        case 't':
                            char_value = '\t';
                            break;
                        case 'r':
                            char_value = '\r';
                            break;
                        case '0':
                            char_value = 0;
                            break;
                        case '\'':
                            char_value = '\'';
                            break;
                        case '\\':
                            char_value = '\\';
                            break;
                        case 'x': {
                            if (strlen(lexeme) < 5) {
                                pp_error_list_push_error(sa->error_list, expr->loc, "Invalid escape in char literal");
                                goto error_cleanup;
                            }
                            char hex[3] = {lexeme[3], lexeme[4], 0};
                            char* hex_end = NULL;
                            long hex_value = strtol(hex, &hex_end, 16);
                            if (hex_end == hex) {
                                pp_error_list_push_error(sa->error_list, expr->loc, "Invalid escape in char literal");
                                goto error_cleanup;
                            }
                            char_value = (unsigned long long)(unsigned char)hex_value;
                            break;
                        }
                        default:
                            char_value = (unsigned long long)(unsigned char)lexeme[2];
                            break;
                    }
                } else {
                    char_value = (unsigned long long)(unsigned char)lexeme[1];
                }
                *out = char_value;
                return 1;
            }

            if (expr->literal.literal_type != PP_TT_INTEGER_LITERAL) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Value must be a constant integer expression");
                goto error_cleanup;
            }

            char numbuf[PP_INT_LITERAL_NUMBUF_SIZE] = {0};
            PPTypeKind literal_kind = pp_type_parse_int_literal_suffix(expr->literal.lexeme, numbuf);
            if (literal_kind != underlying) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Member value type '%s' does not match the underlying type '%s'",
                                         pp_semantic_integer_kind_name(literal_kind),
                                         pp_semantic_integer_kind_name(underlying));
                goto error_cleanup;
            }

            char* endptr = NULL;
            errno = 0;
            // PP has no octal literals: only the 0x/0X prefix selects base 16, a leading zero stays decimal.
            const int base = (numbuf[0] == '0' && (numbuf[1] == 'x' || numbuf[1] == 'X')) ? 16 : 10;
            unsigned long long value = strtoull(numbuf, &endptr, base);
            if (errno != 0 || endptr == numbuf || (endptr && *endptr != '\0')) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Could not parse constant integer literal");
                goto error_cleanup;
            }
            if (!pp_semantic_int_literal_fits(underlying, value)) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Member value is out of range for the underlying type '%s'",
                                         pp_semantic_integer_kind_name(underlying));
                goto error_cleanup;
            }
            *out = value;
            return 1;
        } break;
        case PP_AST_IdentifierNode: {
            // Earlier members of the same flag body are visible, so composites such as
            // `ReadWrite = Read | Write` are expressible.
            const char* name = expr->identifier.identifier;
            for (size_t i = 0; i < member_count; i++) {
                if (members[i].name && name && strcmp(members[i].name, name) == 0) {
                    *out = members[i].value;
                    return 1;
                }
            }
            pp_error_list_push_error(sa->error_list, expr->loc, "Value must be a constant integer expression");
            goto error_cleanup;
        } break;
        case PP_AST_UnaryExpressionNode: {
            unsigned long long operand = 0;
            if (!pp_semantic_eval_int_constant(sa, expr->unary_expression.operand, underlying, members, member_count,
                                               &operand))
                goto error_cleanup;
            switch (expr->unary_expression.op) {
                case PP_TT_MINUS:
                    *out = (0ULL - operand) & value_mask;
                    return 1;
                case PP_TT_TILDE:
                    *out = (~operand) & value_mask;
                    return 1;
                default:
                    pp_error_list_push_error(sa->error_list, expr->loc, "Value must be a constant integer expression");
                    goto error_cleanup;
            }
        } break;
        case PP_AST_BinaryExpressionNode: {
            unsigned long long left = 0;
            unsigned long long right = 0;
            if (!pp_semantic_eval_int_constant(sa, expr->binary_expression.left, underlying, members, member_count,
                                               &left))
                goto error_cleanup;
            if (!pp_semantic_eval_int_constant(sa, expr->binary_expression.right, underlying, members, member_count,
                                               &right))
                goto error_cleanup;

            unsigned long long result = 0;
            switch (expr->binary_expression.op) {
                case PP_TT_PLUS:
                    result = left + right;
                    break;
                case PP_TT_MINUS:
                    result = left - right;
                    break;
                case PP_TT_STAR:
                    result = left * right;
                    break;
                case PP_TT_SLASH:
                    if (right == 0) {
                        pp_error_list_push_error(sa->error_list, expr->loc, "Division by zero in constant value");
                        goto error_cleanup;
                    }
                    if (is_signed) {
                        result = (unsigned long long)(pp_semantic_signed_value(underlying, left) /
                                                      pp_semantic_signed_value(underlying, right));
                    } else {
                        result = left / right;
                    }
                    break;
                case PP_TT_PERCENT:
                    if (right == 0) {
                        pp_error_list_push_error(sa->error_list, expr->loc, "Modulo by zero in constant value");
                        goto error_cleanup;
                    }
                    if (is_signed) {
                        result = (unsigned long long)(pp_semantic_signed_value(underlying, left) %
                                                      pp_semantic_signed_value(underlying, right));
                    } else {
                        result = left % right;
                    }
                    break;
                case PP_TT_LESS_LESS:
                    if (right >= width) {
                        pp_error_list_push_error(sa->error_list, expr->loc, "Shift amount too large in constant value");
                        goto error_cleanup;
                    }
                    result = left << right;
                    break;
                case PP_TT_GREATER_GREATER:
                    if (right >= width) {
                        pp_error_list_push_error(sa->error_list, expr->loc, "Shift amount too large in constant value");
                        goto error_cleanup;
                    }
                    if (is_signed) {
                        result = (unsigned long long)(pp_semantic_signed_value(underlying, left) >> right);
                    } else {
                        result = left >> right;
                    }
                    break;
                case PP_TT_BAR:
                    result = left | right;
                    break;
                case PP_TT_AMPERSAND:
                    result = left & right;
                    break;
                case PP_TT_CARET:
                    result = left ^ right;
                    break;
                default:
                    pp_error_list_push_error(sa->error_list, expr->loc, "Value must be a constant integer expression");
                    goto error_cleanup;
            }
            *out = result & value_mask;
            return 1;
        } break;
        default:
            pp_error_list_push_error(sa->error_list, expr->loc, "Value must be a constant integer expression");
            goto error_cleanup;
    }

error_cleanup:
    return 0;
}

/* Pass 1 - Newtype Declaration Checklist:
 * [x] Ensure base type is defined
 * [x] Ensure base type is not const or reference
 * [x] Detect duplicate newtype name
 * [-] Detect cycles in newtype definition  -> See comments for pass 1 typedef
 * [x] Register named type in GTT
 * [x] Do not allow function or lambda types as newtype.
 */
static int pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    PPType* nt = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_NewtypeDeclarationNode);

    if (pp_global_type_table_find_alias(sa->gtt, node->newtype_declaration.identifier) ||
        pp_global_type_table_find_by_name(sa->gtt, node->newtype_declaration.identifier)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type '%s' previously defined",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    const PPType* base_type = pp_type_from_type_node(sa, node->newtype_declaration.base_type);
    if (!base_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid base type for newtype '%s'",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    if (pp_type_is_const(base_type) || pp_type_is_reference(base_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Base type for newtype '%s' cannot be const or reference",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    if (base_type->kind == PP_TK_Function || base_type->kind == PP_TK_Lambda) {
        pp_error_list_push_error(sa->error_list, node->loc, "Base type for newtype \%s cannot be function or lambda",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    // Create a distinct newtype node instead of aliasing the base type.
    nt = pp_type_create(PP_TK_Newtype);
    if (!nt) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate newtype node for '%s'",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }
    nt->type.pp_type_newtype.name = node->newtype_declaration.identifier;
    nt->type.pp_type_newtype.underlying = base_type;

    const PPType* interned = pp_global_type_table_intern(sa->gtt, nt);
    if (!interned) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern newtype '%s'",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }
    nt = NULL;  // ownership transferred to GTT
    // Register the newtype as a named type.
    if (!pp_global_type_table_register_named(sa->gtt, node->newtype_declaration.identifier, interned)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register newtype '%s'",
                                 node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    pp_type_free(nt);
    return 0;
}

/*
 * Enum Declaration Checklist (Pass 1):
 * [x] Reject duplicate type name (aliases and named types share the namespace).
 * [x] Resolve the explicit underlying type; it must be an integer primitive.
 * [x] Reject const / reference underlying.
 * [x] Reject duplicate member names.
 * [x] Evaluate explicit member values as integer constant expressions.
 * [x] Auto values: first 0, then previous + 1.
 * [x] Values must be strictly increasing and fit the underlying type.
 * [x] Intern a PP_TK_Newtype tagged PP_ENUM_KIND_ENUM and register it as a named type.
 */
static int pp_semantic_analyzer_build_global_table_enum_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    PPType* nt = NULL;
    PP_EnumMember* members = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_EnumDeclarationNode);

    const char* enum_name = node->enum_declaration.identifier;
    unsigned int member_count = node->enum_declaration.member_count;

    if (pp_global_type_table_find_alias(sa->gtt, enum_name) || pp_global_type_table_find_by_name(sa->gtt, enum_name)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type '%s' previously defined", enum_name);
        goto error_cleanup;
    }

    const PPType* underlying = pp_type_from_type_node(sa, node->enum_declaration.underlying_type);
    if (!underlying) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid underlying type for enum '%s'", enum_name);
        goto error_cleanup;
    }

    if (pp_type_is_const(underlying) || pp_type_is_reference(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for enum '%s' cannot be const or a reference", enum_name);
        goto error_cleanup;
    }

    if (!pp_type_kind_is_integer(underlying->kind)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Underlying type for enum '%s' must be an integer type",
                                 enum_name);
        goto error_cleanup;
    }

    // Member values are evaluated as 64-bit unsigned quantities, so a wider underlying type would be
    // nominally supported only.
    if (underlying->kind == PP_TK_Int128 || underlying->kind == PP_TK_UInt128) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for enum '%s' must be a 64-bit-or-smaller integer type", enum_name);
        goto error_cleanup;
    }

    if (member_count > 0) {
        members = (PP_EnumMember*)calloc(member_count, sizeof(PP_EnumMember));
        if (!members) {
            pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate members for enum '%s'", enum_name);
            goto error_cleanup;
        }
    }

    unsigned long long previous_value = 0;
    bool has_previous = false;
    unsigned long long value = 0;

    for (unsigned int i = 0; i < member_count; i++) {
        const char* member_name = node->enum_declaration.member_names[i];
        for (unsigned int j = 0; j < i; j++) {
            if (strcmp(members[j].name, member_name) == 0) {
                pp_error_list_push_error(sa->error_list, node->loc, "Duplicate enum member '%s' in enum '%s'",
                                         member_name, enum_name);
                goto error_cleanup;
            }
        }

        if (node->enum_declaration.member_value_exprs[i]) {
            if (!pp_semantic_eval_int_constant(sa, node->enum_declaration.member_value_exprs[i], underlying->kind,
                                               members, i, &value)) {
                goto error_cleanup;
            }
        } else if (!has_previous) {
            value = 0;
        } else if (pp_type_kind_is_signed(underlying->kind)) {
            long long previous_signed = pp_semantic_signed_value(underlying->kind, previous_value);
            if (previous_signed == pp_semantic_signed_max(underlying->kind)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Enum member '%s' value overflows the underlying type", member_name);
                goto error_cleanup;
            }
            value = pp_semantic_integer_value_mask(underlying->kind) & (unsigned long long)(previous_signed + 1);
        } else {
            unsigned long long max_unsigned = pp_semantic_integer_value_mask(underlying->kind);
            if (previous_value == max_unsigned) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Enum member '%s' value overflows the underlying type", member_name);
                goto error_cleanup;
            }
            value = previous_value + 1;
        }

        if (has_previous && pp_semantic_int_value_compare(underlying->kind, value, previous_value) <= 0) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Enum member '%s' value must be strictly greater than the previous member",
                                     member_name);
            goto error_cleanup;
        }

        members[i].name = member_name;
        members[i].value = value;
        previous_value = value;
        has_previous = true;
    }

    nt = pp_type_create(PP_TK_Newtype);
    if (!nt) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate type for enum '%s'", enum_name);
        goto error_cleanup;
    }
    nt->type.pp_type_newtype.name = enum_name;
    nt->type.pp_type_newtype.underlying = underlying;
    nt->type.pp_type_newtype.enum_kind = PP_ENUM_KIND_ENUM;
    nt->type.pp_type_newtype.members = members;
    nt->type.pp_type_newtype.member_count = member_count;
    members = NULL;  // ownership transferred to the type

    const PPType* interned = pp_global_type_table_intern(sa->gtt, nt);
    if (!interned) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern enum '%s'", enum_name);
        goto error_cleanup;
    }
    nt = NULL;  // ownership transferred to GTT

    if (!pp_global_type_table_register_named(sa->gtt, enum_name, interned)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register enum '%s'", enum_name);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    free(members);
    pp_type_free(nt);
    return 0;
}

/*
 * Flag Declaration Checklist (Pass 1):
 * [x] Reject duplicate type name (aliases and named types share the namespace).
 * [x] Resolve the explicit underlying type; it must be an unsigned integer primitive.
 * [x] Reject const / reference underlying.
 * [x] Reject duplicate member names and the reserved member name 'none'.
 * [x] Evaluate explicit member values as integer constant expressions.
 * [x] Auto values: first 1, then the next power of two greater than the previous member.
 * [x] Explicit 0 is rejected; composites must be a single bit or the OR of earlier members.
 * [x] Values must be strictly increasing and fit the underlying type.
 * [x] Intern a PP_TK_Newtype tagged PP_ENUM_KIND_FLAG and register it as a named type.
 */
static int pp_semantic_analyzer_build_global_table_flag_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    PPType* nt = NULL;
    PP_EnumMember* members = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_FlagDeclarationNode);

    const char* flag_name = node->flag_declaration.identifier;
    unsigned int member_count = node->flag_declaration.member_count;

    if (pp_global_type_table_find_alias(sa->gtt, flag_name) || pp_global_type_table_find_by_name(sa->gtt, flag_name)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type '%s' previously defined", flag_name);
        goto error_cleanup;
    }

    const PPType* underlying = pp_type_from_type_node(sa, node->flag_declaration.underlying_type);
    if (!underlying) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid underlying type for flag '%s'", flag_name);
        goto error_cleanup;
    }

    if (pp_type_is_const(underlying) || pp_type_is_reference(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for flag '%s' cannot be const or a reference", flag_name);
        goto error_cleanup;
    }

    if (!pp_type_kind_is_integer(underlying->kind)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Underlying type for flag '%s' must be an integer type",
                                 flag_name);
        goto error_cleanup;
    }

    if (!pp_type_kind_is_unsigned(underlying->kind)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for flag '%s' must be an unsigned integer type", flag_name);
        goto error_cleanup;
    }

    // Member values are evaluated as 64-bit unsigned quantities, so a wider underlying type would be
    // nominally supported only.
    if (underlying->kind == PP_TK_UInt128) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for flag '%s' must be a 64-bit-or-smaller integer type", flag_name);
        goto error_cleanup;
    }

    if (member_count > 0) {
        members = (PP_EnumMember*)calloc(member_count, sizeof(PP_EnumMember));
        if (!members) {
            pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate members for flag '%s'", flag_name);
            goto error_cleanup;
        }
    }

    unsigned long long previous_value = 0;
    bool has_previous = false;
    unsigned long long value = 0;
    bool is_explicit = false;

    for (unsigned int i = 0; i < member_count; i++) {
        const char* member_name = node->flag_declaration.member_names[i];
        if (strcmp(member_name, "none") == 0) {
            pp_error_list_push_error(sa->error_list, node->loc, "Flag member name 'none' is reserved in flag '%s'",
                                     flag_name);
            goto error_cleanup;
        }
        for (unsigned int j = 0; j < i; j++) {
            if (strcmp(members[j].name, member_name) == 0) {
                pp_error_list_push_error(sa->error_list, node->loc, "Duplicate flag member '%s' in flag '%s'",
                                         member_name, flag_name);
                goto error_cleanup;
            }
        }

        is_explicit = node->flag_declaration.member_value_exprs[i] != NULL;
        if (is_explicit) {
            if (!pp_semantic_eval_int_constant(sa, node->flag_declaration.member_value_exprs[i], underlying->kind,
                                               members, i, &value)) {
                goto error_cleanup;
            }
        } else if (!has_previous) {
            value = 1;
        } else {
            if (previous_value >= (UINT64_MAX >> 1) + 1) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Flag member '%s' value overflows the underlying type", member_name);
                goto error_cleanup;
            }
            unsigned long long next_value = 1;
            while (next_value <= previous_value) next_value <<= 1;
            value = next_value;
        }

        if (!pp_semantic_int_literal_fits(underlying->kind, value)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Flag member '%s' value is out of range for the underlying type", member_name);
            goto error_cleanup;
        }

        if (has_previous && pp_semantic_int_value_compare(underlying->kind, value, previous_value) <= 0) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Flag member '%s' value must be strictly greater than the previous member",
                                     member_name);
            goto error_cleanup;
        }

        if (is_explicit && !pp_semantic_flag_value_is_combination(members, i, value)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Flag member '%s' value must be a single bit or a combination of earlier "
                                     "members",
                                     member_name);
            goto error_cleanup;
        }

        members[i].name = member_name;
        members[i].value = value;
        previous_value = value;
        has_previous = true;
    }

    nt = pp_type_create(PP_TK_Newtype);
    if (!nt) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate type for flag '%s'", flag_name);
        goto error_cleanup;
    }
    nt->type.pp_type_newtype.name = flag_name;
    nt->type.pp_type_newtype.underlying = underlying;
    nt->type.pp_type_newtype.enum_kind = PP_ENUM_KIND_FLAG;
    nt->type.pp_type_newtype.members = members;
    nt->type.pp_type_newtype.member_count = member_count;
    members = NULL;  // ownership transferred to the type

    const PPType* interned = pp_global_type_table_intern(sa->gtt, nt);
    if (!interned) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern flag '%s'", flag_name);
        goto error_cleanup;
    }
    nt = NULL;  // ownership transferred to GTT

    if (!pp_global_type_table_register_named(sa->gtt, flag_name, interned)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register flag '%s'", flag_name);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    free(members);
    pp_type_free(nt);
    return 0;
}

/*
 * Handle Declaration Checklist (Pass 1):
 * [x] Reject duplicate name (aliases and named types share the namespace).
 * [x] Resolve the underlying type; it must already be declared (pass 1 is sequential).
 * [x] Reject const / reference underlying (ownership must be a plain value).
 * [x] Reject function / lambda / operator underlying (a code pointer is not a resource).
 * [x] Reject an underlying that mentions a reference anywhere (a borrow cannot be owned).
 * [x] Reject an underlying that mentions a handle anywhere (no nested handles).
 * [x] Intern a PP_TK_Newtype carrying drop_func_name and register it as a named type.
 */
static int pp_semantic_analyzer_build_global_table_handle_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    PPType* nt = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_HandleDeclarationNode);

    const char* handle_name = node->handle_declaration.identifier;
    const char* drop_name = node->handle_declaration.drop_func_name;

    if (pp_global_type_table_find_alias(sa->gtt, handle_name) ||
        pp_global_type_table_find_by_name(sa->gtt, handle_name)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type '%s' previously defined", handle_name);
        goto error_cleanup;
    }

    const PPType* underlying = pp_type_from_type_node(sa, node->handle_declaration.underlying_type);
    if (!underlying) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid underlying type for handle '%s'", handle_name);
        goto error_cleanup;
    }

    if (pp_type_is_const(underlying) || pp_type_is_reference(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for handle '%s' cannot be const or a reference", handle_name);
        goto error_cleanup;
    }

    if (underlying->kind == PP_TK_Function || underlying->kind == PP_TK_Lambda || underlying->kind == PP_TK_Operator) {
        pp_error_list_push_error(sa->error_list, node->loc, "Underlying type for handle '%s' cannot be a callable type",
                                 handle_name);
        goto error_cleanup;
    }

    if (pp_type_contains_reference(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for handle '%s' cannot contain a reference", handle_name);
        goto error_cleanup;
    }

    if (pp_type_mentions_handle(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Underlying type for handle '%s' cannot contain a handle",
                                 handle_name);
        goto error_cleanup;
    }

    nt = pp_type_create(PP_TK_Newtype);
    if (!nt) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate type for handle '%s'", handle_name);
        goto error_cleanup;
    }
    nt->type.pp_type_newtype.name = handle_name;
    nt->type.pp_type_newtype.underlying = underlying;
    nt->type.pp_type_newtype.drop_func_name = drop_name;
    nt->type.pp_type_newtype.drop_symbol = NULL;

    const PPType* interned = pp_global_type_table_intern(sa->gtt, nt);
    if (!interned) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to intern handle '%s'", handle_name);
        goto error_cleanup;
    }
    nt = NULL;  // ownership transferred to GTT

    if (!pp_global_type_table_register_named(sa->gtt, handle_name, interned)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to register handle '%s'", handle_name);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    pp_type_free(nt);
    return 0;
}

/*
 * Handle Declaration Checklist (Pass 2):
 * [x] Re-run the recursive underlying guards: struct layouts are only complete after Pass 2 Step 1, so a
 *     handle over a struct that mentions a reference or a handle is only detectable here.
 * [x] Resolve drop_func_name in the value namespace.
 * [x] Require a callable *declaration* (not a func-typed variable, lambda or operator).
 * [x] Require signature func[(underlying) -> void]: exactly one parameter, matching type, void return.
 * [x] Store the resolved symbol on the interned handle type.
 */
static int pp_semantic_analyzer_full_analyze_handle_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_HandleDeclarationNode);

    const char* handle_name = node->handle_declaration.identifier;
    const char* drop_name = node->handle_declaration.drop_func_name;

    PPType* handle_type = pp_global_type_table_find_by_name_mut(sa->gtt, handle_name);
    if (!handle_type || !pp_type_is_handle(handle_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Handle '%s' was not registered in pass 1", handle_name);
        goto error_cleanup;
    }

    const PPType* underlying = handle_type->type.pp_type_newtype.underlying;
    if (!underlying) {
        pp_error_list_push_error(sa->error_list, node->loc, "Handle '%s' has no underlying type", handle_name);
        goto error_cleanup;
    }

    if (pp_type_contains_reference(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Underlying type for handle '%s' cannot contain a reference", handle_name);
        goto error_cleanup;
    }

    if (pp_type_mentions_handle(underlying)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Underlying type for handle '%s' cannot contain a handle",
                                 handle_name);
        goto error_cleanup;
    }

    PPSymbol* drop_symbol = pp_symbol_table_lookup_mut(sa->gst, drop_name, false);
    if (!drop_symbol) {
        pp_error_list_push_error(sa->error_list, node->loc, "Drop function '%s' for handle '%s' is not declared",
                                 drop_name, handle_name);
        goto error_cleanup;
    }

    // The drop function must be a real callable declaration, in one of exactly two shapes:
    //   * `func name :: (underlying) -> void { ... }`  - a PP definition (bodies are mandatory in PP), or
    //   * `extern func name :: (underlying) -> void;`  - implemented elsewhere (C), resolved by the linker.
    // Anything else is rejected: a lambda, an operator, or a func-typed variable that merely holds a value.
    if (!drop_symbol->is_callable_decl) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Drop function '%s' for handle '%s' must be declared with 'func' or 'extern "
                                 "func', not as a callable value",
                                 drop_name, handle_name);
        goto error_cleanup;
    }

    if (drop_symbol->type->kind != PP_TK_Function) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Drop function '%s' for handle '%s' must be declared with 'func'", drop_name,
                                 handle_name);
        goto error_cleanup;
    }

    {
        const PPType* drop_type = drop_symbol->type;
        const PPType* drop_return = drop_type->type.pp_type_function.return_type;
        const size_t drop_param_count = drop_type->type.pp_type_function.param_count;

        if (drop_param_count != 1) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Drop function '%s' for handle '%s' must take exactly one parameter", drop_name,
                                     handle_name);
            goto error_cleanup;
        }

        const PPType* drop_param = drop_type->type.pp_type_function.param_types[0];

        // The drop function receives the underlying value, never a handle (which would be a different nominal
        // type sharing the same machine representation).
        if (pp_type_mentions_handle(drop_param)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Drop function '%s' for handle '%s' must take the underlying type, not a "
                                     "handle",
                                     drop_name, handle_name);
            goto error_cleanup;
        }

        if (!pp_type_same_for_drop(drop_param, underlying)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Drop function '%s' for handle '%s' must take the underlying type as its "
                                     "parameter",
                                     drop_name, handle_name);
            goto error_cleanup;
        }

        if (!drop_return || drop_return->kind != PP_TK_Void) {
            pp_error_list_push_error(sa->error_list, node->loc, "Drop function '%s' for handle '%s' must return void",
                                     drop_name, handle_name);
            goto error_cleanup;
        }
    }

    handle_type->type.pp_type_newtype.drop_symbol = drop_symbol;
    return 1;

error_cleanup:
    return 0;
}

static ssize_t pp_find_type_index(PP_GlobalTypeTable* gtt, const PPType* type) {
    if (!gtt || !type) return -1;
    for (size_t i = 0; i < gtt->type_count; i++) {
        if (gtt->types[i] == type) { return (ssize_t)i; }
    }
    return -1;
}

static int pp_semantic_analyzer_struct_cycle_detect(PP_SemanticAnalyzer* sa, const PPType* type,
                                                    PP_StructRecursiveCycleState* states) {
    if (!type) return 1;
    if (type->kind != PP_TK_Struct) return 1;

    ssize_t idx = pp_find_type_index(sa->gtt, type);
    if (idx == -1) { return 1; }

    if (states[(size_t)idx] == PP_STRUCT_CYCLE_RESOLVING) {
        const char* name = type->type.pp_type_struct.name ? type->type.pp_type_struct.name : "<unnamed>";
        for (size_t i = 0; i < sa->program->program.decl_count; i++) {
            PP_AST_Node* decl = sa->program->program.decls[i];
            if (decl->node_type == PP_AST_StructDeclarationNode && decl->struct_declaration.name &&
                strcmp(decl->struct_declaration.name, name) == 0) {
                pp_error_list_push_error(sa->error_list, decl->loc, "Struct '%s' contains recursive cycle", name);
                break;
            }
        }
        return 0;
    }

    if (states[(size_t)idx] == PP_STRUCT_CYCLE_RESOLVED) { return 1; }

    states[(size_t)idx] = PP_STRUCT_CYCLE_RESOLVING;

    const PP_StructLayout* layout = type->type.pp_type_struct.layout;
    if (!layout) return 1;
    for (size_t i = 0; i < layout->field_count; i++) {
        const PPType* field_type = layout->fields[i].field_type;
        if (!field_type) continue;
        const PPType* t = field_type;
        while (t) {
            switch (t->kind) {
                case PP_TK_Const:
                    t = t->type.pp_type_const.const_of;
                    continue;
                case PP_TK_Reference:
                case PP_TK_List:
                    /* Non-by-value edge: stop following */
                    t = NULL;
                    break;
                case PP_TK_Array:
                    t = t->type.pp_type_array.element_type;
                    continue;
                case PP_TK_Newtype:
                    t = t->type.pp_type_newtype.underlying;
                    continue;
                case PP_TK_Struct:
                    if (!pp_semantic_analyzer_struct_cycle_detect(sa, t, states)) { return 0; }
                    t = NULL;
                    break;
                default:
                    t = NULL;
                    break;
            }
            if (!t) break;
        }
    }

    states[(size_t)idx] = PP_STRUCT_CYCLE_RESOLVED;
    return 1;
}

static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa) {
    NULL_POINTER_ERROR_HANDLE(sa);
    NULL_POINTER_ERROR_HANDLE(sa->program);

    /*
        1. Handle only struct declarations (Ignore default initializers for now)
        2. Cycle check
        3. Go through struct declarations again, but type check default initializers.
        4. Non-struct globals.
    */

    // Step 1: handle struct declarations fields only, ignore initializers
    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        PP_AST_Node* decl = sa->program->program.decls[i];
        if (decl->node_type == PP_AST_StructDeclarationNode) {
            pp_semantic_analyzer_full_analyze_struct_decl_fields(sa, decl);
        }
    }

    // Step 2: Struct cycle check
    {
        size_t type_count = sa->gtt ? sa->gtt->type_count : 0;
        PP_StructRecursiveCycleState* states = NULL;
        if (type_count > 0) {
            states = calloc(type_count, sizeof(PP_StructRecursiveCycleState));
            if (!states) {
                pp_error_list_push_error(sa->error_list, (PP_SourceLoc){.filename = NULL, .line = 0, .column = 0},
                                         "Failed to allocate cycle detection states");
                goto error_cleanup;
            }
        }
        for (size_t i = 0; i < sa->program->program.decl_count; i++) {
            PP_AST_Node* decl = sa->program->program.decls[i];
            if (decl->node_type == PP_AST_StructDeclarationNode) {
                PPType* struct_type = pp_global_type_table_find_by_name_mut(sa->gtt, decl->struct_declaration.name);
                if (struct_type) {
                    ssize_t idx = pp_find_type_index(sa->gtt, struct_type);
                    if (idx != -1 && states[(size_t)idx] == PP_STRUCT_CYCLE_RESOLVED) { continue; }
                    pp_semantic_analyzer_struct_cycle_detect(sa, struct_type, states);
                }
            }
        }
        free(states);
    }

    // Step 3: struct default initializer checking
    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        PP_AST_Node* decl = sa->program->program.decls[i];
        if (decl->node_type == PP_AST_StructDeclarationNode) {
            pp_semantic_analyzer_full_analyze_struct_decl_initializers(sa, decl);
        }
    }

    // Step 3.5: constants, in source order. Each constant may reference only constants declared
    // earlier, which keeps folding deterministic and prevents cycles. Later steps therefore see a
    // fully resolved set of constants.
    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        PP_AST_Node* decl = sa->program->program.decls[i];
        if (decl->node_type == PP_AST_ConstantDeclarationNode) {
            pp_semantic_analyzer_full_analyze_constant_decl(sa, decl);
        }
    }

    // Step 4: functions, lambdas, globals
    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        PP_AST_Node* decl = sa->program->program.decls[i];
        switch (decl->node_type) {
            case PP_AST_CallableDeclarationNode:
                switch (decl->callable_declaration.kind) {
                    case PP_CK_Function:
                        pp_semantic_analyzer_full_analyze_function_decl(sa, decl);
                        break;
                    case PP_CK_Operator:
                        pp_semantic_analyzer_full_analyze_operator_decl(sa, decl);
                        break;
                    case PP_CK_Lambda:
                        pp_semantic_analyzer_full_analyze_lambda_decl(sa, decl);
                        break;
                }
                break;
            case PP_AST_VariableDeclarationNode:
                pp_semantic_analyzer_full_analyze_global_variable_decl(sa, decl);
                break;
            case PP_AST_ConstantDeclarationNode:
                // Already handled in step 3.5
                break;
            case PP_AST_StructDeclarationNode:
                // Already handled in steps 1 and 3
                break;
            case PP_AST_TypedefDeclarationNode:
                // Nothing to do
                break;
            case PP_AST_NewtypeDeclarationNode:
                // Nothing to do
                break;
            case PP_AST_HandleDeclarationNode:
                pp_semantic_analyzer_full_analyze_handle_decl(sa, decl);
                break;
            case PP_AST_EnumDeclarationNode:
                // Nothing to do
                break;
            case PP_AST_FlagDeclarationNode:
                // Nothing to do
                break;
            case PP_AST_ExternCallableDeclarationNode:
                // Skip extern declarations in Pass 2
                break;
            default:
                assert(0 && "Semantic Analyzer Pass 2 default case. This should have been handled at parser.");
                abort();
        }
    }

    return 1;

error_cleanup:
    return 0;
}

/* Pass 2 - Function Declaration Checklist (based on cz_semantic_analyzer_check_function_body):
 * [x] Look up function symbol from global table and verify kind is FUNCTION.
 * [x] Set sa->current_function_type (or return type) for checking inside body.
 * [x] If return type is non-void, verify all control paths return a value (cz_ast_node_returns_on_all_paths).
 * [x] Create parameter scope (SCOPE_LEVEL_FUNC_PARAMS) with parent = global table.
 * [x] Verify parameter count in AST matches parameter count in function type.
 * [x] Detect duplicate parameter names within parameter scope.
 * [x] Create symbols for parameters with correct types and escapable reference flag (is_escapable_ref =
 * pp_type_is_reference). [x] Push parameter symbols into parameter scope. [x] Create function body scope
 * (SCOPE_LEVEL_FUNC_BODY) with parent = parameter scope. [x] Check statement list in the body using full analyze. [x]
 * Wire scopes to AST nodes: param_symbol_table and body symbol table. [x] Reset sa->current_function_type after
 * processing.
 */

static int pp_semantic_analyzer_full_analyze_pure_callable(PP_SemanticAnalyzer* sa, PP_AST_Node* node,
                                                           PPTypeKind expected_type_kind) {
    PPSymbolTable* param_symbol_table = NULL;
    PPSymbol* param_symbol = NULL;
    PPSymbolTable* body_symbol_table = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(sa->current_function_type == NULL);

    const char* name = node->callable_declaration.name;
    assert(name);

    const PPSymbol* sym = pp_symbol_table_lookup(sa->gst, name, false);
    if (!sym) {
        pp_error_list_push_error(sa->error_list, node->loc, "Symbol '%s' not found", name);
        goto error_cleanup;
    }

    if (sym->type->kind != expected_type_kind) {
        const char* kind_str = (expected_type_kind == PP_TK_Lambda) ? "lambda" : "operator";
        pp_error_list_push_error(sa->error_list, node->loc, "\"%s\" is not a %s", name, kind_str);
        goto error_cleanup;
    }

    sa->current_function_type = sym->type;

    {
        const PPType* ret_type = (expected_type_kind == PP_TK_Lambda)
                                     ? sa->current_function_type->type.pp_type_lambda.return_type
                                     : sa->current_function_type->type.pp_type_operator.return_type;
        if (ret_type && ret_type->kind != PP_TK_Void) {
            if (!pp_ast_node_returns_on_all_paths(node->callable_declaration.body)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "%s \"%s\" returns non-void, but there may exist a path that it exits without "
                                         "returning something.",
                                         (expected_type_kind == PP_TK_Lambda) ? "Lambda" : "Operator", name);
                goto error_cleanup;
            }
        }
    }

    param_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_PARAMS);
    if (!param_symbol_table) goto error_cleanup;
    param_symbol_table->parent = sa->gst;

    size_t type_param_count = (expected_type_kind == PP_TK_Lambda)
                                  ? sa->current_function_type->type.pp_type_lambda.param_count
                                  : sa->current_function_type->type.pp_type_operator.param_count;
    if (type_param_count != node->callable_declaration.param_count) {
        pp_error_list_push_error(sa->error_list, node->loc, "%s \"%s\" has wrong number of types.",
                                 (expected_type_kind == PP_TK_Lambda) ? "Lambda" : "Operator", name);
        goto error_cleanup;
    }

    for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
        assert(node->callable_declaration.params[i]->node_type == PP_AST_ParamDeclarationNode);
        const char* param_name = node->callable_declaration.params[i]->param_declaration.identifier;
        if (pp_symbol_table_lookup(param_symbol_table, param_name, false)) {
            pp_error_list_push_error(sa->error_list, node->callable_declaration.params[i]->loc,
                                     "Duplicate parameter \"%s\" detected", param_name);
            goto error_cleanup;
        }
        const PPType* param_type =
            pp_type_from_type_node(sa, node->callable_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) goto error_cleanup;
        param_symbol = pp_symbol_create(param_type, false, pp_type_is_reference(param_type), false, true, false);
        if (!pp_symbol_table_push(param_symbol_table, param_name, param_symbol)) {
            pp_error_list_push_error(sa->error_list, node->callable_declaration.params[i]->loc,
                                     "Could not push parameter \"%s\" to parameter scope", param_name);
            goto error_cleanup;
        }
        param_symbol = NULL;
    }

    body_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_BODY);
    if (!body_symbol_table) goto error_cleanup;
    body_symbol_table->parent = param_symbol_table;

    pp_semantic_analyzer_full_analyze_statement_list(sa, body_symbol_table, node->callable_declaration.body);

    node->callable_declaration.body->statement_list.symbol_table = body_symbol_table;
    body_symbol_table = NULL;
    node->callable_declaration.param_symbol_table = param_symbol_table;
    param_symbol_table = NULL;

    sa->current_function_type = NULL;
    return 1;

error_cleanup:
    pp_symbol_table_free(param_symbol_table);
    pp_symbol_free(param_symbol);
    pp_symbol_table_free(body_symbol_table);
    sa->current_function_type = NULL;
    return 0;
}

static int pp_semantic_analyzer_full_analyze_operator_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(node->callable_declaration.kind == PP_CK_Operator);
    return pp_semantic_analyzer_full_analyze_pure_callable(sa, node, PP_TK_Operator);
}

static int pp_semantic_analyzer_full_analyze_function_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    PPSymbolTable* func_param_symbol_table = NULL;
    PPSymbol* param_symbol = NULL;
    PPSymbolTable* func_body_symbol_table = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(node->callable_declaration.kind == PP_CK_Function);
    assert(sa->current_function_type == NULL);

    // Name
    const char* func_name = node->callable_declaration.name;
    assert(func_name);

    // Get Symbol
    const PPSymbol* func_symbol = pp_symbol_table_lookup(sa->gst, func_name, false);
    NULL_POINTER_ERROR_HANDLE(func_symbol);

    // Check if the type is actually function
    // Since newtype does not allow function types and lambda types, this is justified.
    if (func_symbol->type->kind != PP_TK_Function && func_symbol->type->kind != PP_TK_Operator) {
        pp_error_list_push_error(sa->error_list, node->loc, "\"%s\" is not a function", func_name);
        goto error_cleanup;
    }

    // Set the current function type
    sa->current_function_type = func_symbol->type;

    // 1.5 If not void function, all exit paths must return a value
    {
        const PPType* ret_type = sa->current_function_type->type.pp_type_function.return_type;
        if (ret_type && ret_type->kind != PP_TK_Void) {
            if (!pp_ast_node_returns_on_all_paths(node->callable_declaration.body)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Function \"%s\" returns non-void, but there may exist a path that it exits "
                                         "without returning something.",
                                         func_name);
                goto error_cleanup;
            }
        }
    }

    // Create parameter scope.
    func_param_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_PARAMS);
    NULL_POINTER_ERROR_HANDLE(func_param_symbol_table);
    func_param_symbol_table->parent = sa->gst;

    // Check number of parameters in declaration = number of parameters in symbol type.
    // TODO: This might actually be a dead code. Check this later.
    if (func_symbol->type->type.pp_type_function.param_count != node->callable_declaration.param_count) {
        pp_error_list_push_error(sa->error_list, node->loc, "Function \"%s\" has wrong number of types.", func_name);
        goto error_cleanup;
    }

    // Fill in the parameter scope, checking for duplicate symbol.
    for (size_t i = 0; i < node->callable_declaration.param_count; i++) {
        assert(node->callable_declaration.params[i]->node_type == PP_AST_ParamDeclarationNode);
        const char* param_name = node->callable_declaration.params[i]->param_declaration.identifier;
        if (pp_symbol_table_lookup(func_param_symbol_table, param_name, false)) {
            pp_error_list_push_error(sa->error_list, node->callable_declaration.params[i]->loc,
                                     "Duplicate parameter \"%s\" detected", param_name);
            goto error_cleanup;
        }

        const PPType* param_type =
            pp_type_from_type_node(sa, node->callable_declaration.params[i]->param_declaration.type_expr);
        NULL_POINTER_ERROR_HANDLE(param_type);

        // Move-only list check v1: disallow by-value List parameters
        // NOTE: lifted – copy semantics now handled by codegen
        // {
        //     const PPType* decayed_param_type = pp_type_decay(sa->gtt, param_type);
        //     if (decayed_param_type->kind == PP_TK_List && !pp_type_is_reference(param_type)) {
        //         pp_error_list_push_error(sa->error_list, (PP_SourceLoc){.filename=NULL,
        //         .line=node->callable_declaration.params[i]->loc.line, .column=//
        //         node->callable_declaration.params[i]->loc.column},
        //                                  "By-value List parameter is not allowed in move-only v1; use a reference");
        //         goto error_cleanup;
        //     }
        // }

        param_symbol = pp_symbol_create(param_type, false, pp_type_is_reference(param_type), false, true, false);
        if (!pp_symbol_table_push(func_param_symbol_table, param_name, param_symbol)) {
            pp_error_list_push_error(sa->error_list, node->callable_declaration.params[i]->loc,
                                     "Could not push parameter \"%s\" to parameter scope", param_name);
            goto error_cleanup;
        }
        param_symbol = NULL;
    }

    // Create body environment
    func_body_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_BODY);
    NULL_POINTER_ERROR_HANDLE(func_body_symbol_table);
    func_body_symbol_table->parent = func_param_symbol_table;

    // TODO: Handle error.
    pp_semantic_analyzer_full_analyze_statement_list(sa, func_body_symbol_table, node->callable_declaration.body);

    // Wire everything up.
    node->callable_declaration.body->statement_list.symbol_table = func_body_symbol_table;
    func_body_symbol_table = NULL;
    node->callable_declaration.param_symbol_table = func_param_symbol_table;
    func_param_symbol_table = NULL;

    // Nullify current function type.
    sa->current_function_type = NULL;
    return 1;
error_cleanup:
    pp_symbol_table_free(func_param_symbol_table);
    pp_symbol_free(param_symbol);
    pp_symbol_table_free(func_body_symbol_table);
    sa->current_function_type = NULL;
    return 0;
}

/* Pass 2 - Lambda Declaration Checklist (based on cz_semantic_analyzer_check_function_body):
 * [x] Look up lambda symbol from global table and verify kind is LAMBDA.
 * [x] Set sa->current_function_type to lambda type for checking inside body.
 * [x] If return type is non-void, verify all control paths return a value (cz_ast_node_returns_on_all_paths).
 * [x] Create parameter scope (SCOPE_LEVEL_FUNC_PARAMS) with parent = global table.
 * [x] Verify parameter count in AST matches parameter count in lambda type.
 * [x] Detect duplicate parameter names within parameter scope.
 * [x] Create symbols for parameters with correct types and escapable reference flag.
 * [x] Push parameter symbols into parameter scope.
 * [x] Create body scope (SCOPE_LEVEL_FUNC_BODY) with parent = parameter scope.
 * [x] Check statement list in the body using full analyze.
 * [x] Wire scopes to AST nodes: param_symbol_table and body symbol table.
 * [x] Reset sa->current_function_type after processing.
 */
static int pp_semantic_analyzer_full_analyze_lambda_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_CallableDeclarationNode);
    assert(node->callable_declaration.kind == PP_CK_Lambda);
    return pp_semantic_analyzer_full_analyze_pure_callable(sa, node, PP_TK_Lambda);
}

/* Pass 2 - Struct Declaration Checklist (based on cz_semantic_analyzer_check_struct_fields):
 * [x] Ensure struct is defined in GTT and of correct kind STRUCT.
 * [x] Detect duplicate field names while populating layout.
 * [x] Populate struct layout with fields from AST.
 * [x] Verify each field type is well-defined in GTT.
 * [x] For fields with initializers:
 *      - [x] Check expression validity and type resolution via check_expression.
 *      - [x] Require constexpr initializer.
 *      - [x] Ensure type match between field and initializer after decay.
 *      - [-] If field is reference: initializer must be l-value, const-correctness (non-const ref cannot bind to
 * const).
 *             -> Reference fields cannot have default initializers at global scope per PP design;
 * notes/global_statement_types.md
 *      - [x] Store default initializer in struct layout field.
 * [-] Detect struct recursive cycles via separate pass -> Not needed in PP (forward refs disallowed)
 * (cz_semantic_analyzer_struct_cycle_detect)
 */
static int pp_semantic_analyzer_full_analyze_struct_decl_fields(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    PP_StructLayout* struct_layout = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_StructDeclarationNode);

    const char* struct_name = node->struct_declaration.name;
    NULL_POINTER_ERROR_HANDLE(struct_name);

    // Get mutable reference so that fields can be filled.
    // Note that this is not an allocation.
    PPType* struct_type = pp_global_type_table_find_by_name_mut(sa->gtt, struct_name);
    if (!struct_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Could not find struct '%s' from type table from pass 1.",
                                 node->struct_declaration.name);
        goto error_cleanup;
    }

    // Create layout and push fields.
    struct_layout = pp_struct_layout_create();
    if (!struct_layout) {
        pp_error_list_push_error(sa->error_list, node->loc, "Failed to allocate struct layout for struct '%s'",
                                 node->struct_declaration.name);
        goto error_cleanup;
    }

    for (size_t i = 0; i < node->struct_declaration.field_count; i++) {
        const PP_AST_StructField* field = &node->struct_declaration.fields[i];
        assert(field->field->node_type == PP_AST_ParamDeclarationNode);

        // Duplicate field check
        for (size_t j = 0; j < i; j++) {
            if (struct_layout->fields[j].field_name == field->field->param_declaration.identifier) {
                pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                         "Struct field '%s' is duplicate", field->field->param_declaration.identifier);
                goto error_cleanup;
            }
        }

        const PPType* field_type = pp_type_from_type_node(sa, field->field->param_declaration.type_expr);
        if (!field_type) {
            pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                     "Failed to create type for struct field '%s'",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // A handle is owned by the scope that declares it. A struct field would outlive that scope (or hold a
        // borrow of it), so handles are banned from struct fields entirely.
        if (pp_type_mentions_handle(field_type)) {
            pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                     "Struct field '%s' cannot have a handle type",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // If initializer = NULL, it stores NULL, and if initializer exists, it stores the initializer.
        if (!pp_struct_layout_push_field(struct_layout, field->field->param_declaration.identifier, field_type,
                                         field->initializer)) {
            pp_error_list_push_error(sa->error_list, field->field->loc, "Failed to add struct field '%s'",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }
    }

    // Link the struct layout to the type.
    // Free any existing layout first to avoid leaking on duplicate struct declarations
    // or re-analysis of the same type. In valid code this path is never taken,
    // but on error paths we still walk the tree and must keep memory clean.
    if (struct_type->type.pp_type_struct.layout != NULL) {
        pp_struct_layout_free((PP_StructLayout*)struct_type->type.pp_type_struct.layout);
    }
    struct_type->type.pp_type_struct.layout = struct_layout;
    struct_layout = NULL;

    return 1;
error_cleanup:
    pp_struct_layout_free(struct_layout);
    return 0;
}

static int pp_semantic_analyzer_full_analyze_struct_decl_initializers(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_StructDeclarationNode);

    const char* struct_name = node->struct_declaration.name;
    NULL_POINTER_ERROR_HANDLE(struct_name);

    PPType* struct_type = pp_global_type_table_find_by_name_mut(sa->gtt, struct_name);
    if (!struct_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Could not find struct '%s' from type table.", struct_name);
        goto error_cleanup;
    }

    const PP_StructLayout* layout = struct_type->type.pp_type_struct.layout;
    if (!layout) {
        pp_error_list_push_error(sa->error_list, node->loc, "Struct '%s' layout not initialized.", struct_name);
        goto error_cleanup;
    }

    for (size_t i = 0; i < node->struct_declaration.field_count; i++) {
        const PP_AST_StructField* field = &node->struct_declaration.fields[i];
        if (!field->initializer) { continue; }

        // Expression check
        if (!pp_semantic_analyzer_check_expression(sa, sa->gst, field->initializer)) {
            pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                     "Initializer for struct field '%s' is invalid",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        const PP_AST_Decoration* initializer_decoration = pp_ast_get_decoration(field->initializer);
        assert(initializer_decoration);

        // Require constexpr
        if (!initializer_decoration->is_constexpr) {
            pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                     "Global scope initializer for struct field '%s' should be constexpr",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // Use layout field type
        const PPType* field_type = layout->fields[i].field_type;
        if (pp_type_is_reference(field_type)) {
            pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                     "Struct field '%s' is a reference and cannot have a default initializer at global "
                                     "scope",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // Decay/type equality check
        const PPType* decayed_field_type = pp_type_decay(sa->gtt, field_type);
        const PPType* decayed_initializer_type = pp_type_decay(sa->gtt, initializer_decoration->resolved_type);

        // Static string literal size check for [uint8 : N] field initializer
        if (field->initializer && field->initializer->node_type == PP_AST_LiteralNode &&
            field->initializer->literal.literal_type == PP_TT_STATIC_STRING_LITERAL) {
            char context[256];
            snprintf(context, sizeof(context), "for struct field '%s'", field->field->param_declaration.identifier);
            if (!pp_semantic_check_static_string_size_mismatch(sa, field->field->param_declaration.type_expr->loc,
                                                               context, field_type,
                                                               initializer_decoration->resolved_type)) {
                goto error_cleanup;
            }
        }

        if (!pp_type_equal(decayed_field_type, decayed_initializer_type)) {
            pp_error_list_push_error(sa->error_list, field->field->param_declaration.type_expr->loc,
                                     "Initializer for struct field '%s' does not match the field type",
                                     field->field->param_declaration.identifier);
            goto error_cleanup;
        }
    }

    return 1;
error_cleanup:
    return 0;
}

/* Pass 2 - Constant Declaration Checklist:
 * [x] Look up the constant symbol in the global table
 * [x] Initializer must be present (also enforced by parser/Pass 1)
 * [x] Check expression validity in the global scope
 * [x] Require a constexpr initializer
 * [x] Require the initializer type to match the declared type after decay
 * [x] Re-confirm the non-owning initializer pointer used for codegen folding
 */
static int pp_semantic_analyzer_full_analyze_constant_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_ConstantDeclarationNode);

    const char* name = node->constant_declaration.identifier;
    assert(name);

    PPSymbol* constant_symbol = pp_symbol_table_lookup_mut(sa->gst, name, false);
    NULL_POINTER_ERROR_HANDLE(constant_symbol);
    assert(constant_symbol->is_constant_decl);

    if (!node->constant_declaration.initializer) {
        pp_error_list_push_error(sa->error_list, node->loc, "Constant '%s' requires an initializer.", name);
        goto error_cleanup;
    }

    if (!pp_semantic_analyzer_check_expression(sa, sa->gst, node->constant_declaration.initializer)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Constant '%s' has an invalid initializer.", name);
        goto error_cleanup;
    }

    const PP_AST_Decoration* initializer_decoration = pp_ast_get_decoration(node->constant_declaration.initializer);
    NULL_POINTER_ERROR_HANDLE(initializer_decoration);

    if (!initializer_decoration->is_constexpr) {
        pp_error_list_push_error(sa->error_list, node->loc, "Constant '%s' has a non constexpr initializer.", name);
        goto error_cleanup;
    }

    const PPType* decayed_constant_type = pp_type_decay(sa->gtt, constant_symbol->type);
    const PPType* decayed_initializer_type = pp_type_decay(sa->gtt, initializer_decoration->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_constant_type);
    NULL_POINTER_ERROR_HANDLE(decayed_initializer_type);
    if (!pp_type_equal(decayed_constant_type, decayed_initializer_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Type mismatch during constant declaration '%s'.", name);
        goto error_cleanup;
    }

    constant_symbol->constant_initializer = node->constant_declaration.initializer;
    constant_symbol->constant_value_available = true;

    return 1;

error_cleanup:
    return 0;
}

/* Pass 2 - Variable Declaration Checklist (based on cz_semantic_analyzer_check_global_var_init):
 * [x] Look up variable symbol mutably from global table.
 * [x] Global variables cannot be references – enforced in Pass 1 and re-checked here. Reference handling from CZ is N/A
 * in PP. [x] If struct type, ensure it does not contain reference fields (global rule). [x] If no initializer:
 *      - Error if type is const. Reference is disallowed for globals.
 *      - Return early.
 * [x] Check expression validity and type resolution for initializer via check_expression.
 * [x] Require constexpr initializer for globals.
 * [x] Ensure type match between variable and initializer after decay.
 * [x] Update symbol's constexpr flag for const variables: is_constexpr = type is const && initializer is constexpr.
 * [x] Mark global variable as escapable.
 */
static int pp_semantic_analyzer_full_analyze_global_variable_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_VariableDeclarationNode);
    assert(node->variable_declaration.var_decl->node_type == PP_AST_ParamDeclarationNode);

    PPSymbol* variable_symbol =
        pp_symbol_table_lookup_mut(sa->gst, node->variable_declaration.var_decl->param_declaration.identifier, false);
    NULL_POINTER_ERROR_HANDLE(variable_symbol);

    // If struct type, it should not contain any reference fields (as this is a global variable.)
    if (variable_symbol->type->kind == PP_TK_Struct) {
        const PP_StructLayout* struct_layout = variable_symbol->type->type.pp_type_struct.layout;
        if (!struct_layout) {
            pp_error_list_push_error(sa->error_list, node->loc, "Struct \"%s\" could not be previously analyzed.",
                                     node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }
        for (size_t i = 0; i < struct_layout->field_count; i++) {
            if (pp_type_is_reference(struct_layout->fields[i].field_type)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Global variable \"%s\" is a struct, but global variable cannot contain any "
                                         "references.",
                                         node->variable_declaration.var_decl->param_declaration.identifier);
                goto error_cleanup;
            }
        }
    }

    // Global variable cannot be reference.
    if (pp_type_is_reference(variable_symbol->type)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Global variable \"%s\" declared as reference, which is not allowed.",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    // Check for initializer.
    if (!node->variable_declaration.initializer) {
        if (pp_type_is_const(variable_symbol->type)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Variable \"%s\" declared as const must have initializer.",
                                     node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }
        // Callable values have no null representation in v1.
        if (pp_type_is_callable(pp_type_decay(sa->gtt, variable_symbol->type))) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Global variable \"%s\" has a callable type, but initializer does not exist.",
                                     node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }
    } else {
        // Initializer exists here.
        if (!pp_semantic_analyzer_check_expression(sa, sa->gst, node->variable_declaration.initializer)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Variable \"%s\" has an invalid initializer.",
                                     node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }

        // Require constexpr
        const PP_AST_Decoration* decor = pp_ast_get_decoration(node->variable_declaration.initializer);
        NULL_POINTER_ERROR_HANDLE(decor);
        // Explicitly reject non-empty list initializers at global scope
        const PPType* decayed_type = pp_type_decay(sa->gtt, variable_symbol->type);
        NULL_POINTER_ERROR_HANDLE(decayed_type);
        if (decayed_type->kind == PP_TK_List &&
            node->variable_declaration.initializer->node_type == PP_AST_CollectionInitializerNode &&
            node->variable_declaration.initializer->collection_initializer.element_count > 0) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Global list initialization with non-empty initializer is not allowed; only empty "
                                     "list is permitted at global scope.");
            goto error_cleanup;
        }
        if (decayed_type->kind == PP_TK_String &&
            node->variable_declaration.initializer->node_type == PP_AST_LiteralNode &&
            node->variable_declaration.initializer->literal.literal_type == PP_TT_STRING_LITERAL) {
            const char* lexeme = node->variable_declaration.initializer->literal.lexeme;
            assert(lexeme);
            if (strlen(lexeme) > 2) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Global string initialization with non-empty initializer is not allowed; only "
                                         "empty "
                                         "string is permitted at global scope.");
                goto error_cleanup;
            }
        }
        if (!decor->is_constexpr) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Global variable \"%s\" has a non constexpr initializer.",
                                     node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }

        // Static string literal size check for [uint8 : N] initialization
        if (node->variable_declaration.initializer &&
            node->variable_declaration.initializer->node_type == PP_AST_LiteralNode &&
            node->variable_declaration.initializer->literal.literal_type == PP_TT_STATIC_STRING_LITERAL) {
            char context[256];
            snprintf(context, sizeof(context), "for variable '%s'",
                     node->variable_declaration.var_decl->param_declaration.identifier);
            if (!pp_semantic_check_static_string_size_mismatch(sa, node->loc, context, variable_symbol->type,
                                                               decor->resolved_type)) {
                goto error_cleanup;
            }
        }

        // Check if types match
        const PPType* decayed_lhs_type = pp_type_decay(sa->gtt, variable_symbol->type);
        const PPType* decayed_rhs_type = pp_type_decay(sa->gtt, decor->resolved_type);
        NULL_POINTER_ERROR_HANDLE(decayed_lhs_type);
        NULL_POINTER_ERROR_HANDLE(decayed_rhs_type);
        if (!pp_type_equal(decayed_lhs_type, decayed_rhs_type)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Type mismatch during variable declaration.");
            goto error_cleanup;
        }

        // For const variable, track constexpr.
        if (pp_type_is_const(variable_symbol->type)) { variable_symbol->is_constexpr = true; }

        // Global variable is always escapable.
        variable_symbol->is_escapable_ref = true;
    }

    return 1;

error_cleanup:
    return 0;
}

static int pp_semantic_analyzer_full_analyze_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                       PP_AST_Node* node) {
    PPSymbolTable* inner_symbol_table = NULL;
    assert(sa);
    assert(symbol_table);
    assert(node);

    switch (node->node_type) {
        case PP_AST_VariableDeclarationNode:
            if (!pp_semantic_analyzer_full_analyze_variable_declaration(sa, symbol_table, node)) { goto error_cleanup; }
            break;
        case PP_AST_AssignmentNode:
            /* Assignment Statement Checklist (based on cz_semantic_analyzer_check_assignment_statement) */
            if (!pp_semantic_analyzer_full_analyze_assignment_statement(sa, symbol_table, node)) { goto error_cleanup; }
            break;
        case PP_AST_ReturnStatementNode:
            /* Return Statement Checklist (based on cz_semantic_analyzer_check_return_statement) */
            if (!pp_semantic_analyzer_full_analyze_return_statement(sa, symbol_table, node)) { goto error_cleanup; }
            break;
        case PP_AST_BreakStatementNode:
            if (sa->current_loop_depth == 0) {
                pp_error_list_push_error(sa->error_list, node->loc, "break statement outside of loop");
                goto error_cleanup;
            }
            break;
        case PP_AST_ContinueStatementNode:
            if (sa->current_loop_depth == 0) {
                pp_error_list_push_error(sa->error_list, node->loc, "continue statement outside of loop");
                goto error_cleanup;
            }
            break;
        case PP_AST_IfStatementNode:
            /* If Statement Checklist (based on cz_semantic_analyzer_check_if_statement) */
            if (!pp_semantic_analyzer_full_analyze_if_statement(sa, symbol_table, node)) { goto error_cleanup; }
            break;
        case PP_AST_ForStatementNode:
            /* For Statement Checklist (based on cz_semantic_analyzer_check_for_statement) */
            if (!pp_semantic_analyzer_full_analyze_for_statement(sa, symbol_table, node)) { goto error_cleanup; }
            break;
        case PP_AST_WhileStatementNode:
            /* While Statement Checklist (based on cz_semantic_analyzer_check_while_statement) */
            if (!pp_semantic_analyzer_full_analyze_while_statement(sa, symbol_table, node)) { goto error_cleanup; }
            break;
        case PP_AST_StatementListNode:
            inner_symbol_table = pp_symbol_table_create(symbol_table->scope_level + 1);
            NULL_POINTER_ERROR_HANDLE(inner_symbol_table);

            inner_symbol_table->parent = symbol_table;
            if (!pp_semantic_analyzer_full_analyze_statement_list(sa, inner_symbol_table, node)) { goto error_cleanup; }

            node->statement_list.symbol_table = inner_symbol_table;
            inner_symbol_table = NULL;
            break;
        default:
            /* Expression statement: check expression for side-effects */
            if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node)) { goto error_cleanup; }
            break;
    }

    return 1;

error_cleanup:
    pp_symbol_table_free(inner_symbol_table);
    return 0;
}

/* Variable Declaration Statement Checklist (based on cz_semantic_analyzer_check_variable_declaration_statement):
 * [x] Lookup existing symbol; prevent shadowing function parameters and redeclaration in same scope. -> Shadowing
 * banned for all parent scopes. [x] Resolve type from type node. [x] If type is reference or const, initializer must
 * exist. [ ] If type contains reference (direct or via struct field), initializer must exist; no zero-init / default.
 * [ ] If struct type contains reference fields:
 *     [ ] Each reference field must be explicitly initialized from l-value with const-correctness
 *     [ ] Reference fields cannot use default initializers
 * [x] If initializer exists: check expression validity.
 * [x] If type is reference: initializer must be l-value; const-correctness check.
 * [ ] If struct type without initializer: ensure all const/reference fields have default initializers. -> Not allowed
 * for reference fields in PP; see above. [x] Ensure decayed types match between variable and initializer. [x] Create
 * symbol with type, constexpr flag, escapable reference flag, and push to symbol table. Note: Types containing
 * references are allowed locally but must not escape. For prototype:
 *       - [ ] ban returning by value a type that contains a reference
 *       - [ ] ban whole-struct assignment for types containing a reference
 *       - [ ] consider banning lambda/function parameters whose type contains a reference or enforce const-propagation
 * through reference fields.
 */
static int pp_semantic_analyzer_full_analyze_variable_declaration(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                                  PP_AST_Node* node) {
    PPSymbol* variable_symbol = NULL;
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_VariableDeclarationNode);

    const char* symbol_name = node->variable_declaration.var_decl->param_declaration.identifier;
    assert(symbol_name);

    // Look up symbol.
    {
        const PPSymbol* symbol = pp_symbol_table_lookup(symbol_table, symbol_name, true);
        if (symbol) {
            pp_error_list_push_error(sa->error_list, node->loc, "Shadowing \"%s\" is not allowed.", symbol_name);
            goto error_cleanup;
        }
    }

    // Deduce type
    const PPType* symbol_type =
        pp_type_from_type_node(sa, node->variable_declaration.var_decl->param_declaration.type_expr);
    if (!symbol_type) {
        pp_error_list_push_error(sa->error_list, node->loc, "Deducing type for \"%s\" failed", symbol_name);
        goto error_cleanup;
    }

    // If reference or const, initializer must exist.
    if (pp_type_is_const(symbol_type) || pp_type_is_reference(symbol_type)) {
        if (!node->variable_declaration.initializer) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Variable \"%s\" is declared as const or reference, but initializer does not "
                                     "exist.",
                                     symbol_name);
            goto error_cleanup;
        }
    }

    // Callable values have no null representation in v1, so the initializer is mandatory.
    if (pp_type_is_callable(pp_type_decay(sa->gtt, symbol_type)) && !node->variable_declaration.initializer) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Variable \"%s\" has a callable type, but initializer does not exist.", symbol_name);
        goto error_cleanup;
    }

    // A handle is acquired exactly once, by the initializer, and released at the end of this scope. Without an
    // initializer there would be nothing to own, so it is mandatory.
    if (pp_type_contains_handle(symbol_type) && !node->variable_declaration.initializer) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Variable \"%s\" has a handle type, but initializer does not exist.", symbol_name);
        goto error_cleanup;
    }

    // Dropping a handle is an effect. A lambda body must stay free of resource ownership, so it may not declare
    // handles at all.
    if (pp_type_contains_handle(symbol_type) && sa->current_function_type &&
        sa->current_function_type->kind == PP_TK_Lambda) {
        pp_error_list_push_error(sa->error_list, node->loc, "Lambda body cannot declare a handle variable \"%s\"",
                                 symbol_name);
        goto error_cleanup;
    }

    // If initializer exists, check validity.
    if (node->variable_declaration.initializer) {
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->variable_declaration.initializer)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Initializer for variable \"%s\" is not valid",
                                     symbol_name);
            goto error_cleanup;
        }

        const PP_AST_Decoration* initializer_decor = pp_ast_get_decoration(node->variable_declaration.initializer);
        assert(initializer_decor);

        // If reference variable
        if (pp_type_is_reference(symbol_type)) {
            // l-value
            if (initializer_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Variable \"%s\" is a reference, but initializer is not an l-value",
                                         symbol_name);
                goto error_cleanup;
            }
            // const-correctness
            if (!pp_type_is_const(symbol_type) && pp_type_is_const(initializer_decor->resolved_type)) {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "Variable \"%s\" is a mutable reference, but initializer is const. Cannot "
                                         "remove constness",
                                         symbol_name);
                goto error_cleanup;
            }
        }

        // Decayed type match
        const PPType* decayed_symbol_type = pp_type_decay(sa->gtt, symbol_type);
        const PPType* decayed_initializer_type = pp_type_decay(sa->gtt, initializer_decor->resolved_type);
        assert(decayed_symbol_type);
        assert(decayed_initializer_type);

        // Static string literal size check for [uint8 : N] initialization
        if (node->variable_declaration.initializer &&
            node->variable_declaration.initializer->node_type == PP_AST_LiteralNode &&
            node->variable_declaration.initializer->literal.literal_type == PP_TT_STATIC_STRING_LITERAL) {
            char context[256];
            snprintf(context, sizeof(context), "for variable '%s'", symbol_name);
            if (!pp_semantic_check_static_string_size_mismatch(sa, node->loc, context, symbol_type,
                                                               initializer_decor->resolved_type)) {
                goto error_cleanup;
            }
        }

        if (!pp_type_equal(decayed_symbol_type, decayed_initializer_type)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Type of variable \"%s\" does not match its initializer", symbol_name);
            goto error_cleanup;
        }

        // Copyability check: if source is lvalue and type needs drop, ensure type is copyable
        if (initializer_decor->value_category == PP_VALUE_CATEGORY_LVALUE && pp_type_needs_drop(decayed_symbol_type) &&
            !pp_type_is_copyable(decayed_symbol_type)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Cannot copy initialize variable \"%s\": type is move-only or contains "
                                     "non-copyable elements",
                                     symbol_name);
            goto error_cleanup;
        }

        // Create symbol
        // is_constexpr: variable has to be constant, and initializer should also be constexpr (so the value that
        // variable has is determined at compile time) is_escapable_ref: Note that this reference can refer to variable
        // from outside, then return it back. For this, it must be reference, and initializer must be escapable.
        variable_symbol =
            pp_symbol_create(symbol_type, pp_type_is_const(symbol_type) && initializer_decor->is_constexpr,
                             pp_type_is_reference(symbol_type) && initializer_decor->is_escapable_ref, false, true,
                             false);
    } else {
        variable_symbol = pp_symbol_create(symbol_type, false, false, false, true, false);
    }
    NULL_POINTER_ERROR_HANDLE(variable_symbol);

    pp_symbol_table_push(symbol_table, symbol_name, variable_symbol);
    variable_symbol = NULL;

    return 1;
error_cleanup:
    pp_symbol_free(variable_symbol);
    return 0;
}

/* Return Statement Checklist (based on cz_semantic_analyzer_check_return_statement):
 * [x] Verify current function return type is set.
 * [x] If return type is void, expression must be NULL; otherwise expression must exist.
 * [x] Check expression validity via check_expression.
 * [x] Ensure decayed types match between return type and expression.
 * [x] If return type is reference:
 *      - Expression must be l-value.
 *      - Expression must be escapable (not local stack).
 *      - Const-correctness: non-const return cannot bind to const value.
 */
static int pp_semantic_analyzer_full_analyze_return_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                              PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_ReturnStatementNode);

    assert(sa->current_function_type);
    assert(sa->current_function_type->kind == PP_TK_Function || sa->current_function_type->kind == PP_TK_Lambda ||
           sa->current_function_type->kind == PP_TK_Operator);

    // If return type is void, expression must be NULL.
    // If return type is not void, expression must exist.
    if (sa->current_function_type->type.pp_type_function.return_type->kind == PP_TK_Void &&
        node->return_statement.expr) {
        pp_error_list_push_error(sa->error_list, node->loc, "Return type is void, but an expression is returned.");
        goto error_cleanup;
    }
    if (sa->current_function_type->type.pp_type_function.return_type->kind != PP_TK_Void &&
        !node->return_statement.expr) {
        pp_error_list_push_error(sa->error_list, node->loc, "Expected a return expression.");
        goto error_cleanup;
    }

    // If void return, end early.
    if (sa->current_function_type->type.pp_type_function.return_type->kind == PP_TK_Void) { return 1; }

    // Check expression
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->return_statement.expr)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Invalid expression");
        goto error_cleanup;
    }
    const PP_AST_Decoration* expr_decor = pp_ast_get_decoration(node->return_statement.expr);
    assert(expr_decor);

    // Check type equality.
    const PPType* decayed_return_type =
        pp_type_decay(sa->gtt, sa->current_function_type->type.pp_type_function.return_type);
    const PPType* decayed_expr_type = pp_type_decay(sa->gtt, expr_decor->resolved_type);
    if (!pp_type_equal(decayed_return_type, decayed_expr_type)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Return type and expression type in the return statement do not match.");
        goto error_cleanup;
    }

    // A handle never leaves the scope that owns it. A factory may return the *result of a call*, which is a
    // fresh resource that was never owned here; anything else (a local, a parameter, a member) would hand out
    // a value whose drop still belongs to this scope.
    if (pp_type_contains_handle(sa->current_function_type->type.pp_type_function.return_type) &&
        node->return_statement.expr->node_type != PP_AST_FunctionCallNode) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "A handle must be returned directly from the call that produces it, not from a "
                                 "value owned by this scope.");
        goto error_cleanup;
    }

    // Return type reference handling.
    if (pp_type_is_reference(sa->current_function_type->type.pp_type_function.return_type)) {
        // L-value
        if (expr_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Return type is reference, but the return expression is not an l-value.");
            goto error_cleanup;
        }

        // Dangling reference check
        if (!expr_decor->is_escapable_ref) {
            pp_error_list_push_error(sa->error_list, node->loc, "Cannot return a dangling reference");
            goto error_cleanup;
        }

        // Const-dropping detection
        if (!pp_type_is_const(sa->current_function_type->type.pp_type_function.return_type) &&
            pp_type_is_const(expr_decor->resolved_type)) {
            pp_error_list_push_error(sa->error_list, node->loc,
                                     "Cannot strip away constness when function return type is non-const.");
            goto error_cleanup;
        }
    }

    return 1;
error_cleanup:
    return 0;
}

/* Assignment Statement Checklist (based on cz_semantic_analyzer_check_assignment_statement):
 * [x] Verify operator is an assignment operator.
 * [x] Check LHS and RHS expressions via check_expression.
 * [x] Ensure LHS decoration exists and is l-value.
 * [x] Ensure LHS is mutable (not const).
 * [-] After decay, ensure both sides are primitive and types match.
 *      -> Not necessarily primitive? (Struct assignment, list assignment are possible.)
 * [x] Validate compound assignment operators against allowed primitive combinations.
 */
static int pp_semantic_analyzer_full_analyze_assignment_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                                  PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_AssignmentNode);
    assert(pp_token_type_is_assignment(node->assignment.op));

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->assignment.lhs)) {
        pp_error_list_push_error(sa->error_list, node->loc, "LHS of assignment is an invalid expression.");
        goto error_cleanup;
    }
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->assignment.rhs)) {
        pp_error_list_push_error(sa->error_list, node->loc, "RHS of assignment is an invalid expression.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* lhs_decor = pp_ast_get_decoration(node->assignment.lhs);
    const PP_AST_Decoration* rhs_decor = pp_ast_get_decoration(node->assignment.rhs);
    assert(lhs_decor);
    assert(rhs_decor);

    // LHS must be l-value
    if (lhs_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
        pp_error_list_push_error(sa->error_list, node->loc, "LHS of assignment should be an l-value.");
        goto error_cleanup;
    }
    // LHS must be mutable.
    const PPType* lhs_type_check = lhs_decor->resolved_type;
    assert(lhs_type_check);
    bool lhs_immutable = pp_type_is_top_level_const(lhs_type_check);
    if (!lhs_immutable && lhs_type_check->kind == PP_TK_Reference) {
        // References are mutable if their referent is mutable; const inside reference makes assignment immutable
        lhs_immutable = pp_type_is_const(lhs_type_check->type.pp_type_reference.reference_to);
    }
    if (lhs_immutable) {
        pp_error_list_push_error(sa->error_list, node->loc, "LHS of assignment should be mutable (non-const)");
        goto error_cleanup;
    }

    const PPType* decayed_lhs_type = pp_type_decay(sa->gtt, lhs_decor->resolved_type);
    const PPType* decayed_rhs_type = pp_type_decay(sa->gtt, rhs_decor->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_lhs_type);
    NULL_POINTER_ERROR_HANDLE(decayed_rhs_type);

    // Static string literal size check for [uint8 : N] assignment
    if (node->assignment.rhs && node->assignment.rhs->node_type == PP_AST_LiteralNode &&
        node->assignment.rhs->literal.literal_type == PP_TT_STATIC_STRING_LITERAL) {
        if (!pp_semantic_check_static_string_size_mismatch(sa, node->loc, "in assignment", lhs_decor->resolved_type,
                                                           rhs_decor->resolved_type)) {
            goto error_cleanup;
        }
    }

    if (!pp_type_can_assign(decayed_lhs_type, decayed_rhs_type)) {
        pp_error_list_push_error(sa->error_list, node->loc, "LHS and RHS of assignment must have compatible types.");
        goto error_cleanup;
    }

    // Copyability check: if RHS is lvalue and LHS type needs drop, ensure type is copyable
    if (rhs_decor->value_category == PP_VALUE_CATEGORY_LVALUE && pp_type_needs_drop(decayed_lhs_type) &&
        !pp_type_is_copyable(decayed_lhs_type)) {
        pp_error_list_push_error(sa->error_list, node->loc,
                                 "Cannot assign from lvalue: type is move-only or contains non-copyable elements");
        goto error_cleanup;
    }

    // Flags are closed under |, & and ^, so the corresponding compound forms are legal on a flag l-value.
    const bool flag_compound = pp_type_is_flag(decayed_lhs_type) && pp_type_equal(decayed_lhs_type, decayed_rhs_type) &&
                               (node->assignment.op == PP_TT_AMPERSAND_EQUAL ||
                                node->assignment.op == PP_TT_BAR_EQUAL || node->assignment.op == PP_TT_CARET_EQUAL);

    // Validate compound assignment operators against allowed primitive combinations
    switch (node->assignment.op) {
        case PP_TT_EQUAL:
            // Simple assignment already validated by type equality
            break;
        case PP_TT_PLUS_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128) ||
                (decayed_lhs_type->kind == PP_TK_String && decayed_rhs_type->kind == PP_TK_String)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "+= only defined for numeric and string types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_MINUS_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "-= only defined for numeric types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_STAR_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "*= only defined for numeric types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_SLASH_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "/= only defined for numeric types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_PERCENT_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "%= only defined for integer types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_AMPERSAND_EQUAL:
            if (flag_compound) break;
            if ((decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "&= only defined for integer types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_BAR_EQUAL:
            if (flag_compound) break;
            if ((decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "|= only defined for integer types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_CARET_EQUAL:
            if (flag_compound) break;
            if ((decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc, "^= only defined for integer types.");
                goto error_cleanup;
            }
            break;
        case PP_TT_LESS_LESS_EQUAL:
        case PP_TT_GREATER_GREATER_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, node->loc,
                                         "<<= / >>= only defined for unsigned integer types.");
                goto error_cleanup;
            }
            break;
        default:
            pp_error_list_push_error(sa->error_list, node->loc, "Unknown assignment operator.");
            goto error_cleanup;
    }

    return 1;
error_cleanup:
    return 0;
}

/* If Statement Checklist (based on cz_semantic_analyzer_check_if_statement):
 * [x] Check condition expression via check_expression.
 * [x] Ensure decayed condition type is bool.
 * [x] Analyze then branch statement.
 * [x] If else branch exists, analyze it.
 */
static int pp_semantic_analyzer_full_analyze_if_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                          PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_IfStatementNode);

    // Check condition
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->if_statement.cond)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Condition for if statement could not be analyzed.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* cond_decor = pp_ast_get_decoration(node->if_statement.cond);
    assert(cond_decor);

    // Check if condition is actually boolean
    const PPType* decayed_cond_type = pp_type_decay(sa->gtt, cond_decor->resolved_type);
    assert(decayed_cond_type);
    if (decayed_cond_type->kind != PP_TK_Bool) {
        pp_error_list_push_error(sa->error_list, node->loc, "Condition for if statement must be boolean");
        goto error_cleanup;
    }

    // Then branch
    if (!pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, node->if_statement.then_branch)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Then branch for if statement is invalid");
        goto error_cleanup;
    }

    // Else branch
    if (node->if_statement.else_branch) {
        if (!pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, node->if_statement.else_branch)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Else branch for if statement is invalid");
            goto error_cleanup;
        }
    }

    return 1;
error_cleanup:
    return 0;
}

/* For Statement Checklist (based on cz_semantic_analyzer_check_for_statement):
 * [x] Create sub-symbol table for loop scope with parent.
 * [x] If initializer exists: analyze as variable declaration or assignment in sub scope.
 * [x] If condition exists: check expression and ensure bool type after decay.
 * [x] If iteration step exists: analyze as statement in sub scope.
 * [x] Analyze body statement in sub scope.
 * [x] Wire loop scope to AST node.
 */
static int pp_semantic_analyzer_full_analyze_for_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                           PP_AST_Node* node) {
    PPSymbolTable* for_init_symbol_table = NULL;
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_ForStatementNode);

    // For init symbol table: A single symbol table bring between symbol_table and for body symbol_table, where it may
    // or may not hold the initialization variable.
    for_init_symbol_table = pp_symbol_table_create(symbol_table->scope_level + 1);
    NULL_POINTER_ERROR_HANDLE(for_init_symbol_table);
    for_init_symbol_table->parent = symbol_table;

    // Initialization
    if (node->for_statement.init) {
        if (node->for_statement.init->node_type == PP_AST_VariableDeclarationNode) {
            // Variable declaration
            if (!pp_semantic_analyzer_full_analyze_variable_declaration(sa, for_init_symbol_table,
                                                                        node->for_statement.init)) {
                pp_error_list_push_error(sa->error_list, node->for_statement.init->loc,
                                         "Invalid for statement initialization (nothing, variable declaration, or "
                                         "assignments only)");
                goto error_cleanup;
            }
        } else if (node->for_statement.init->node_type == PP_AST_AssignmentNode) {
            // Assignment
            if (!pp_semantic_analyzer_full_analyze_assignment_statement(sa, for_init_symbol_table,
                                                                        node->for_statement.init)) {
                pp_error_list_push_error(sa->error_list, node->for_statement.init->loc,
                                         "Invalid for statement initialization (nothing, variable declaration, or "
                                         "assignments only)");
                goto error_cleanup;
            }
        } else {
            // Unreachable.
            abort();
        }
    }

    // Condition
    if (node->for_statement.cond) {
        if (!pp_semantic_analyzer_check_expression(sa, for_init_symbol_table, node->for_statement.cond)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Condition for for statement could not be analyzed.");
            goto error_cleanup;
        }
        const PP_AST_Decoration* cond_decor = pp_ast_get_decoration(node->for_statement.cond);
        assert(cond_decor);

        // Check if condition is actually boolean
        const PPType* decayed_cond_type = pp_type_decay(sa->gtt, cond_decor->resolved_type);
        assert(decayed_cond_type);
        if (decayed_cond_type->kind != PP_TK_Bool) {
            pp_error_list_push_error(sa->error_list, node->loc, "Condition for for statement must be boolean");
            goto error_cleanup;
        }
    }

    // Update
    if (node->for_statement.update) {
        if (!pp_semantic_analyzer_full_analyze_statement(sa, for_init_symbol_table, node->for_statement.update)) {
            pp_error_list_push_error(sa->error_list, node->loc, "Update for for statement is invalid");
            goto error_cleanup;
        }
    }

    // Body
    assert(node->for_statement.body);
    assert(node->for_statement.body->node_type == PP_AST_StatementListNode);
    sa->current_loop_depth++;
    if (!pp_semantic_analyzer_full_analyze_statement(sa, for_init_symbol_table, node->for_statement.body)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Body for for statement is invalid");
        goto error_cleanup;
    }
    sa->current_loop_depth--;

    // Link to AST node.
    node->for_statement.for_init_symbol_table = for_init_symbol_table;
    for_init_symbol_table = NULL;

    return 1;

error_cleanup:
    pp_symbol_table_free(for_init_symbol_table);
    return 0;
}

/* While Statement Checklist (based on cz_semantic_analyzer_check_while_statement):
 * [x] Check condition expression via check_expression.
 * [x] Ensure decayed condition type is bool.
 * [x] Analyze body statement in same scope.
 */
static int pp_semantic_analyzer_full_analyze_while_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                             PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_WhileStatementNode);

    // Check condition
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->while_statement.cond)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Condition for while statement could not be analyzed.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* cond_decor = pp_ast_get_decoration(node->while_statement.cond);
    assert(cond_decor);

    // Check if condition is actually boolean
    const PPType* decayed_cond_type = pp_type_decay(sa->gtt, cond_decor->resolved_type);
    assert(decayed_cond_type);
    if (decayed_cond_type->kind != PP_TK_Bool) {
        pp_error_list_push_error(sa->error_list, node->loc, "Condition for whilte statement must be boolean");
        goto error_cleanup;
    }

    // Enter loop
    sa->current_loop_depth++;
    // Then branch
    if (!pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, node->while_statement.body)) {
        pp_error_list_push_error(sa->error_list, node->loc, "Body of while statement is invalid");
        goto error_cleanup;
    }
    sa->current_loop_depth--;

    return 1;
error_cleanup:
    sa->current_loop_depth = 0;
    return 0;
}

static int pp_semantic_analyzer_full_analyze_statement_list(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                            PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_StatementListNode);

    for (unsigned int i = 0; i < node->statement_list.stmt_count; i++) {
        PP_AST_Node* stmt = node->statement_list.stmts[i];
        pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, stmt);
    }

    return 1;
}

/* pp_semantic_analyzer_check_expression Checklist (based on cz_semantic_analyzer_check_expression):
 * [x] Validate inputs via asserts
 * [x] Dispatch to binary expression checker
 * [x] Dispatch to unary expression checker
 * [x] Dispatch to literal expression checker
 * [x] Dispatch to identifier expression checker
 * [x] Dispatch to cast expression checker
 * [x] Dispatch to function call expression checker
 * [x] Dispatch to array/list access expression checker
 * [x] Dispatch to struct member access checker
 * [x] Dispatch to struct init expression checker
 * [x] Dispatch to collection initializer expression checker
 * [-] Default error for unsupported nodes -> PP returns 0 without explicit push
 */
static int pp_semantic_analyzer_check_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                 PP_AST_Node* expr) {
    assert(sa);
    assert(symbol_table);
    assert(expr);

    switch (expr->node_type) {
        case PP_AST_FunctionCallNode:
            if (!pp_semantic_analyzer_check_function_call(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_BinaryExpressionNode:
            if (!pp_semantic_analyzer_check_binary_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_UnaryExpressionNode:
            if (!pp_semantic_analyzer_check_unary_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_LiteralNode:
            if (!pp_semantic_analyzer_check_literal_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_NullLiteralNode:
            if (!pp_semantic_analyzer_check_null_literal_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_IdentifierNode:
            if (!pp_semantic_analyzer_check_identifier_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_ArrayListAccessNode:
            if (!pp_semantic_analyzer_check_array_list_access_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_MemberAccessNode:
            if (!pp_semantic_analyzer_check_member_access_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_StructInitializerNode:
            if (!pp_semantic_analyzer_check_struct_initializer_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_CastExpressionNode:
            if (!pp_semantic_analyzer_check_cast_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_QualifiedConstantNode:
            if (!pp_semantic_analyzer_check_qualified_constant_expression(sa, expr)) goto error_cleanup;
            break;
        case PP_AST_CollectionInitializerNode:
            if (!pp_semantic_analyzer_check_collection_initializer_expression(sa, symbol_table, expr))
                goto error_cleanup;
            break;
        case PP_AST_ListPushNode:
            if (!pp_semantic_analyzer_check_list_push(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_ListPopNode:
            if (!pp_semantic_analyzer_check_list_pop(sa, symbol_table, expr)) goto error_cleanup;
            break;
        case PP_AST_ListClearNode:
            if (!pp_semantic_analyzer_check_list_clear(sa, symbol_table, expr)) goto error_cleanup;
            break;
        default:
            goto error_cleanup;
    }

    return 1;

error_cleanup:
    return 0;
}

/* Qualified Constant Expression Checklist (`Type::Member`):
 * [x] Early exit if decoration already set
 * [x] Resolve the type name in the global type table (named types, then aliases)
 * [x] The type must be an enum or a flag
 * [x] Resolve the member (Flag::none is synthetic and yields 0)
 * [x] Decorate as a constexpr rvalue carrying the constant value
 */
static int pp_semantic_analyzer_check_qualified_constant_expression(PP_SemanticAnalyzer* sa, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(expr);
    assert(expr->node_type == PP_AST_QualifiedConstantNode);

    if (expr->qualified_constant.decoration) return 1;

    const char* type_name = expr->qualified_constant.type_name;
    const char* member = expr->qualified_constant.member;

    const PPType* type = pp_global_type_table_find_by_name(sa->gtt, type_name);
    if (!type) type = pp_global_type_table_find_alias(sa->gtt, type_name);
    if (!type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Unknown type '%s' in qualified constant", type_name);
        goto error_cleanup;
    }

    if (!pp_type_is_enum_or_flag(type)) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "Type '%s' is not an enum or flag; '::' is only supported for enum and flag types",
                                 type_name);
        goto error_cleanup;
    }

    unsigned long long value = 0;
    if (!pp_semantic_lookup_qualified_constant_value(sa, type_name, member, &value, NULL)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Type '%s' has no member '%s'", type_name, member);
        goto error_cleanup;
    }

    decoration = pp_ast_decoration_create(type, PP_VALUE_CATEGORY_RVALUE, true, false);
    NULL_POINTER_ERROR_HANDLE(decoration);
    decoration->is_enum_constant = true;
    decoration->enum_constant_value = value;
    expr->qualified_constant.decoration = decoration;
    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/* Function Call Expression Checklist (based on cz_semantic_analyzer_check_function_call_expression):
 * [x] Early exit if decoration already set
 * [x] Disallow function calls at global scope
 * [x] Check callee expression validity
 * [x] Verify callee resolved type is function
 * [x] Enforce lambda purity: lambda body may only call lambdas
 * [x] Validate parameter count matches argument count
 * [x] For each argument, check expression validity
 * [x] For each argument, decay types and compare to parameter types
 * [x] For reference parameters, verify argument is l-value
 * [x] For reference parameters, verify const-correctness (no cast away const)
 * [x] Determine return type and value category
 * [x] Create AST decoration for function call result and attach
 * [ ] Relax the current conservative condition on the return type reference being escapable appropriate.
 * [ ] Lambda function could return const reference (though implement this later.)
 */
/*
 * Handle Call Checklist:
 * [x] The function bound as a handle's drop function is not an ordinary callable. Calling it from source would
 *     release the resource before its scope ends (or twice), so direct calls are rejected. The compiler emits
 *     the only call, at scope exit.
 */
static bool pp_is_handle_drop_function(PP_SemanticAnalyzer* sa, const char* name) {
    if (!sa || !sa->gtt || !name) return false;
    for (size_t i = 0; i < sa->gtt->type_count; i++) {
        const PPType* t = sa->gtt->types[i];
        if (!t || t->kind != PP_TK_Newtype) continue;
        const char* drop_name = t->type.pp_type_newtype.drop_func_name;
        if (drop_name && strcmp(drop_name, name) == 0) return true;
    }
    return false;
}

static int pp_semantic_analyzer_check_function_call(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                    PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_FunctionCallNode);

    if (expr->function_call.decoration) return 1;

    // Function calls are not allowed at global scope
    if (!sa->current_function_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Function calls are not allowed at global scope");
        goto error_cleanup;
    }

    // A handle's drop function may only be called by the compiler, at scope exit.
    if (expr->function_call.callee->node_type == PP_AST_IdentifierNode &&
        pp_is_handle_drop_function(sa, expr->function_call.callee->identifier.identifier)) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "Function '%s' is the drop function of a handle and cannot be called directly; the "
                                 "handle is dropped when its scope ends.",
                                 expr->function_call.callee->identifier.identifier);
        goto error_cleanup;
    }

    // Check callee (to support expressions like (f(2))(3), that is, higher-order functions)
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->function_call.callee)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Function callee could not be validated.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* callee_decoration = pp_ast_get_decoration(expr->function_call.callee);
    assert(callee_decoration);

    // Decay first: the callee may be a `const func[...]` or `func[...]&` value.
    const PPType* callee_type = pp_type_decay(sa->gtt, callee_decoration->resolved_type);
    NULL_POINTER_ERROR_HANDLE(callee_type);

    // Check indeed that it is a function.
    if (callee_type->kind != PP_TK_Function && callee_type->kind != PP_TK_Lambda) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Callee is not a registered function.");
        goto error_cleanup;
    }

    // Keep track of whether or not the function is lambda.
    bool function_is_lambda = callee_type->kind == PP_TK_Lambda;

    // Restriction: lambda and operator bodies may not call regular functions.
    // They may call lambda or operator functions only.
    if (sa->current_function_type &&
        (sa->current_function_type->kind == PP_TK_Lambda || sa->current_function_type->kind == PP_TK_Operator)) {
        if (callee_type->kind == PP_TK_Function) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Lambda/operator function cannot call regular function");
            goto error_cleanup;
        }
    }

    // Param count validation
    if (function_is_lambda) {
        if (callee_type->type.pp_type_lambda.param_count != expr->function_call.arg_count) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Lambda function expects %d arguments but only received %d arguments",
                                     callee_type->type.pp_type_lambda.param_count, expr->function_call.arg_count);
            goto error_cleanup;
        }
    } else {
        if (callee_type->type.pp_type_function.param_count != expr->function_call.arg_count) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Function expects %d arguments but only received %d arguments",
                                     callee_type->type.pp_type_function.param_count, expr->function_call.arg_count);
            goto error_cleanup;
        }
    }

    size_t param_count = function_is_lambda ? callee_type->type.pp_type_lambda.param_count
                                            : callee_type->type.pp_type_function.param_count;

    bool is_escapable = true;
    // Argument type checking loop
    for (size_t i = 0; i < param_count; i++) {
        // Check argument.
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->function_call.arguments[i])) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Function argument could not be deduced");
            goto error_cleanup;
        }

        // Type decay check
        const PP_AST_Decoration* arg_decor = pp_ast_get_decoration(expr->function_call.arguments[i]);
        assert(arg_decor);
        const PPType* arg_type = arg_decor->resolved_type;
        const PPType* param_type = function_is_lambda ? callee_type->type.pp_type_lambda.param_types[i]
                                                      : callee_type->type.pp_type_function.param_types[i];
        assert(arg_type);
        assert(param_type);
        const PPType* decayed_arg_type = pp_type_decay(sa->gtt, arg_type);
        const PPType* decayed_param_type = pp_type_decay(sa->gtt, param_type);
        if (!pp_type_equal(decayed_arg_type, decayed_param_type)) {
            pp_error_list_push_error(sa->error_list, expr->function_call.arguments[i]->loc,
                                     "Type mismatch between parameter and passed argument.");
            goto error_cleanup;
        }

        if (pp_type_is_reference(param_type)) {
            // If parameter is reference, mutable references require an l-value.
            // Const references may bind rvalues through materialized temporaries.
            if (arg_decor->value_category != PP_VALUE_CATEGORY_LVALUE && !pp_type_is_const(param_type)) {
                pp_error_list_push_error(sa->error_list, expr->function_call.arguments[i]->loc,
                                         "Parameter is mutable reference, so l-value is expected.");
                goto error_cleanup;
            }
            // Lambda does not allow mutable references.
            if (function_is_lambda && !pp_type_is_const(param_type)) {
                pp_error_list_push_error(sa->error_list, expr->function_call.arguments[i]->loc,
                                         "Lambda function requires all reference parameters to be immutable.");
                goto error_cleanup;
            }

            // Constness check
            if (pp_type_is_const(arg_type) && !pp_type_is_const(param_type)) {
                pp_error_list_push_error(sa->error_list, expr->function_call.arguments[i]->loc,
                                         "Parameter is mutable reference, but const l-value is passed.");
                goto error_cleanup;
            }
            // Escapability Check
            if (!arg_decor->is_escapable_ref) { is_escapable = false; }
        }
    }

    const PPType* return_type = function_is_lambda ? callee_type->type.pp_type_lambda.return_type
                                                   : callee_type->type.pp_type_function.return_type;
    assert(return_type);
    if (function_is_lambda) {
        // Void cannot be returned in lambda function
        if (return_type->kind == PP_TK_Void) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Lambda function forbids returning void");
            goto error_cleanup;
        }

        // TODO: For now lambda does not allow returning references.
        // This condition will be lifted later in v2
        if (pp_type_is_reference(return_type)) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Lambda function (for now) forbids returning references.");
            goto error_cleanup;
        }
    }

    decoration = pp_ast_decoration_create(return_type,
                                          pp_type_is_reference(return_type) ? PP_VALUE_CATEGORY_LVALUE
                                                                            : PP_VALUE_CATEGORY_RVALUE,
                                          false, pp_type_is_reference(return_type) && is_escapable);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->function_call.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/* pp_semantic_analyzer_check_binary_expression Checklist (based on cz_semantic_analyzer_check_binary_expression):
 * [x] Validate inputs via asserts
 * [x] Early exit if expr->binary_expression.decoration already set
 * [x] Recursively check left sub-expression via pp_semantic_analyzer_check_expression
 * [x] Recursively check right sub-expression via pp_semantic_analyzer_check_expression
 * [x] Retrieve lhs/rhs decorations and resolved types
 * [x] Decay lhs and rhs types via pp_type_decay
 * [x] Validate operator-specific type compatibility and set binary_type per operator
 * [x] Create decoration with result_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr = lhs&&rhs, escapable_ref false
 * [-] Explicitly verify both operands are primitive after decay with an error push -> PP checks operator-specific
 * compatibility explicitly; a blanket primitive guard would prevent future non-primitive overloads
 * [-] Propagate maximum scope level of operands into decoration, CZ uses MAX(lhs_scope,rhs_scope) -> PP does not track
 * scope level in decorations
 */
static int pp_semantic_analyzer_check_binary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                        PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* binary_type = NULL;
    bool binary_type_owned = true;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_BinaryExpressionNode);

    if (expr->binary_expression.decoration) return 1;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->binary_expression.left)) { goto error_cleanup; }
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->binary_expression.right)) { goto error_cleanup; }

    const PP_AST_Decoration* lhs_decoration = pp_ast_get_decoration(expr->binary_expression.left);
    assert(lhs_decoration);
    const PP_AST_Decoration* rhs_decoration = pp_ast_get_decoration(expr->binary_expression.right);
    assert(rhs_decoration);

    const PPType* lhs_type = lhs_decoration->resolved_type;
    assert(lhs_type);
    const PPType* rhs_type = rhs_decoration->resolved_type;
    assert(lhs_type);

    const PPType* decayed_lhs_type = pp_type_decay(sa->gtt, lhs_type);
    if (!decayed_lhs_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Decaying type failure");
        goto error_cleanup;
    }
    const PPType* decayed_rhs_type = pp_type_decay(sa->gtt, rhs_type);
    if (!decayed_rhs_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Decaying type failure");
        goto error_cleanup;
    }

    // Enum and flag values are nominal: both operands must carry the same declared type. Enums only
    // compare for equality; flags additionally support the bitwise set operators, closed over the same type.
    if (pp_type_is_enum_or_flag(decayed_lhs_type) || pp_type_is_enum_or_flag(decayed_rhs_type)) {
        if (!pp_type_equal(decayed_lhs_type, decayed_rhs_type)) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Enum and flag operands must have the same type.");
            goto error_cleanup;
        }
        switch (expr->binary_expression.op) {
            case PP_TT_EQUAL_EQUAL:
            case PP_TT_EXCLAMATION_EQUAL:
                binary_type = pp_type_create(PP_TK_Bool);
                break;
            case PP_TT_BAR:
            case PP_TT_AMPERSAND:
            case PP_TT_CARET:
                if (!pp_type_is_flag(decayed_lhs_type)) {
                    pp_error_list_push_error(sa->error_list, expr->loc,
                                             "Enum values support no operators other than == and !=.");
                    goto error_cleanup;
                }
                binary_type = (PPType*)decayed_lhs_type;  // borrowed from the GTT
                binary_type_owned = false;
                break;
            default:
                if (pp_type_is_flag(decayed_lhs_type)) {
                    pp_error_list_push_error(sa->error_list, expr->loc,
                                             "Flag values support only ==, !=, |, &, and ^.");
                } else {
                    pp_error_list_push_error(sa->error_list, expr->loc,
                                             "Enum values support no operators other than == and !=.");
                }
                goto error_cleanup;
        }
        goto builtin_binary_done;
    }

    // Callable values support identity comparison (== / !=) only, and both operands must
    // have the identical callable type. Every other operator stays rejected.
    const bool callable_operands = pp_type_is_callable(decayed_lhs_type) || pp_type_is_callable(decayed_rhs_type);
    if (callable_operands) {
        const bool is_equality =
            expr->binary_expression.op == PP_TT_EQUAL_EQUAL || expr->binary_expression.op == PP_TT_EXCLAMATION_EQUAL;
        if (!is_equality) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Callable values support no operators other than == and !=.");
            goto error_cleanup;
        }
        if (!pp_type_is_callable(decayed_lhs_type) || !pp_type_is_callable(decayed_rhs_type) ||
            !pp_type_equal(decayed_lhs_type, decayed_rhs_type)) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Callable values can only be compared with an identical callable type.");
            goto error_cleanup;
        }
        binary_type = pp_type_create(PP_TK_Bool);
    }

    // Callable operands never consult the builtin operator table or overloads.
    if (callable_operands) goto builtin_binary_done;

    switch (expr->binary_expression.op) {
        case PP_TT_BAR_BAR:
        case PP_TT_AMPERSAND_AMPERSAND:
            if (decayed_lhs_type->kind == PP_TK_Bool && decayed_rhs_type->kind == PP_TK_Bool) {
                binary_type = pp_type_create(PP_TK_Bool);
            }
            break;
        case PP_TT_EQUAL_EQUAL:
        case PP_TT_EXCLAMATION_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Bool && decayed_rhs_type->kind == PP_TK_Bool) ||
                (decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128) ||
                (decayed_lhs_type->kind == PP_TK_String && decayed_rhs_type->kind == PP_TK_String) ||
                (decayed_lhs_type->kind == PP_TK_Pointer && decayed_rhs_type->kind == PP_TK_Pointer &&
                 pp_type_equal(decayed_lhs_type, decayed_rhs_type))) {
                binary_type = pp_type_create(PP_TK_Bool);
            }
            break;
        case PP_TT_LESS:
        case PP_TT_GREATER:
        case PP_TT_LESS_EQUAL:
        case PP_TT_GREATER_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(PP_TK_Bool);
            }
            break;
        case PP_TT_BAR:
        case PP_TT_CARET:
        case PP_TT_AMPERSAND:
            if ((decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_PLUS:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128) ||
                (decayed_lhs_type->kind == PP_TK_String && decayed_rhs_type->kind == PP_TK_String)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_MINUS:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_STAR:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_SLASH:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_PERCENT:
            if ((decayed_lhs_type->kind == PP_TK_Int8 && decayed_rhs_type->kind == PP_TK_Int8) ||
                (decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_Int16 && decayed_rhs_type->kind == PP_TK_Int16) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Int64 && decayed_rhs_type->kind == PP_TK_Int64) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_Int128 && decayed_rhs_type->kind == PP_TK_Int128) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_LESS_LESS:
        case PP_TT_GREATER_GREATER:
            if ((decayed_lhs_type->kind == PP_TK_UInt8 && decayed_rhs_type->kind == PP_TK_UInt8) ||
                (decayed_lhs_type->kind == PP_TK_UInt16 && decayed_rhs_type->kind == PP_TK_UInt16) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_UInt64 && decayed_rhs_type->kind == PP_TK_UInt64) ||
                (decayed_lhs_type->kind == PP_TK_UInt128 && decayed_rhs_type->kind == PP_TK_UInt128)) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        default:
            pp_error_list_push_error(sa->error_list, expr->loc, "Unsupported binary operator");
            goto error_cleanup;
    }
    if (!binary_type) {
        const char* op_str = NULL;
        switch (expr->binary_expression.op) {
            case PP_TT_PLUS:
                op_str = "+";
                break;
            case PP_TT_MINUS:
                op_str = "-";
                break;
            case PP_TT_STAR:
                op_str = "*";
                break;
            case PP_TT_SLASH:
                op_str = "/";
                break;
            case PP_TT_PERCENT:
                op_str = "%";
                break;
            default:
                op_str = NULL;
                break;
        }
        const PPSymbol* op_sym = pp_semantic_analyzer_operator_lookup(sa, op_str, decayed_lhs_type, decayed_rhs_type);
        if (op_sym && op_sym->type && op_sym->type->kind == PP_TK_Operator) {
            const PPType* ret_type = op_sym->type->type.pp_type_function.return_type;
            const PPType* interned_ret_type = pp_global_type_table_intern(sa->gtt, ret_type);
            if (!interned_ret_type) goto error_cleanup;
            binary_type = (PPType*)interned_ret_type;  // borrowed from the GTT
            binary_type_owned = false;
        }
    }
builtin_binary_done:
    if (!binary_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Binary operator requires compatible operand types");
        goto error_cleanup;
    }

    const PPType* interned_binary_type = pp_global_type_table_intern(sa->gtt, binary_type);
    if (!interned_binary_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failed to intern binary result type");
        goto error_cleanup;
    }
    binary_type = NULL;

    decoration = pp_ast_decoration_create(interned_binary_type, PP_VALUE_CATEGORY_RVALUE,
                                          lhs_decoration->is_constexpr && rhs_decoration->is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->binary_expression.decoration = decoration;
    if (decoration) {
        const char* op_spelling = NULL;
        switch (expr->binary_expression.op) {
            case PP_TT_PLUS:
                op_spelling = "+";
                break;
            case PP_TT_MINUS:
                op_spelling = "-";
                break;
            case PP_TT_STAR:
                op_spelling = "*";
                break;
            case PP_TT_SLASH:
                op_spelling = "/";
                break;
            case PP_TT_PERCENT:
                op_spelling = "%";
                break;
            case PP_TT_AMPERSAND:
                op_spelling = "&";
                break;
            case PP_TT_BAR:
                op_spelling = "|";
                break;
            case PP_TT_CARET:
                op_spelling = "^";
                break;
            case PP_TT_LESS_LESS:
                op_spelling = "<<";
                break;
            case PP_TT_GREATER_GREATER:
                op_spelling = ">>";
                break;
            case PP_TT_EQUAL_EQUAL:
                op_spelling = "==";
                break;
            case PP_TT_EXCLAMATION_EQUAL:
                op_spelling = "!=";
                break;
            case PP_TT_LESS:
                op_spelling = "<";
                break;
            case PP_TT_GREATER:
                op_spelling = ">";
                break;
            case PP_TT_LESS_EQUAL:
                op_spelling = "<=";
                break;
            case PP_TT_GREATER_EQUAL:
                op_spelling = ">=";
                break;
            default:
                op_spelling = NULL;
                break;
        }
        const PPSymbol* oc = pp_semantic_analyzer_operator_lookup(sa, op_spelling, decayed_lhs_type, decayed_rhs_type);
        if (oc) decoration->resolved_operator_callable = oc;
    }
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    if (binary_type_owned) pp_type_free(binary_type);
    return 0;
}

/* pp_semantic_analyzer_check_unary_expression Checklist (based on cz_semantic_analyzer_check_unary_expression):
 * [x] Validate inputs via asserts
 * [x] Early exit if expr->unary_expression.decoration already set
 * [x] Recursively check operand via pp_semantic_analyzer_check_expression
 * [x] Retrieve operand decoration and resolved type
 * [x] Decay operand type via pp_type_decay
 * [x] Switch on unary operator and validate primitive compatibility, set unary_type
 *    - MINUS: PP allows Int32/UInt32/Float, CZ allows Int32/Float only
 *    - EXCLAMATION: requires Bool
 * [x] Create decoration with result_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr propagated from operand, escapable_ref
 * false
 * [-] Explicitly verify operand is primitive after decay with error push -> PP checks operator-specific compatibility
 * explicitly; blanket primitive guard would prevent future non-primitive overloads
 * [-] Preserve operand scope level in decoration, CZ passes operand->decoration->scope_level -> PP does not track scope
 * level in decorations
 */
static int pp_semantic_analyzer_check_unary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                       PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* unary_type = NULL;
    bool unary_type_owned = true;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_UnaryExpressionNode);

    if (expr->unary_expression.decoration) return 1;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->unary_expression.operand)) {
        goto error_cleanup;
    }

    const PP_AST_Decoration* operand_decoration = pp_ast_get_decoration(expr->unary_expression.operand);
    assert(operand_decoration);

    const PPType* operand_type = operand_decoration->resolved_type;
    assert(operand_type);

    const PPType* decayed_operand_type = pp_type_decay(sa->gtt, operand_type);
    if (!decayed_operand_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failure decaying type");
        goto error_cleanup;
    }

    // Callable values support only the address-of operator, and only for `func[...]`
    // values: a lambda is a value, not addressable code.
    if (pp_type_is_callable(decayed_operand_type)) {
        if (expr->unary_expression.op != PP_TT_AT) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Callable values only support the address-of operator.");
            goto error_cleanup;
        }
        if (decayed_operand_type->kind != PP_TK_Function) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Address-of cannot be applied to a lambda value.");
            goto error_cleanup;
        }
    }

    const char* op_spelling = NULL;
    switch (expr->unary_expression.op) {
        case PP_TT_PLUS:
            op_spelling = "+";
            break;
        case PP_TT_MINUS:
            op_spelling = "-";
            break;
        case PP_TT_EXCLAMATION:
            op_spelling = "!";
            break;
        case PP_TT_TILDE:
            op_spelling = "~";
            break;
        default:
            op_spelling = NULL;
            break;
    }

    const PPSymbol* custom_oc = NULL;
    if (op_spelling) { custom_oc = pp_semantic_analyzer_unary_operator_lookup(sa, op_spelling, operand_type); }
    if (custom_oc) {
        const PPType* op_type = custom_oc->type;
        const PPType* ret_type = op_type->type.pp_type_operator.return_type;
        const PPType* interned = pp_global_type_table_intern(sa->gtt, (PPType*)ret_type);
        unary_type = (PPType*)interned;
        // skip builtin processing
        // build decoration below with callable
        expr->unary_expression.decoration =
            pp_ast_decoration_create(unary_type, PP_VALUE_CATEGORY_RVALUE, operand_decoration->is_constexpr, false);
        if (!expr->unary_expression.decoration) goto error_cleanup;
        expr->unary_expression.decoration->resolved_operator_callable = custom_oc;
        return 1;
    }

    switch (expr->unary_expression.op) {
        case PP_TT_MINUS:
            switch (decayed_operand_type->kind) {
                case PP_TK_Int8:
                case PP_TK_Int16:
                case PP_TK_Int32:
                case PP_TK_Int64:
                case PP_TK_Int128:
                case PP_TK_Float:
                    unary_type = pp_type_create(decayed_operand_type->kind);
                    break;
                default:
                    pp_error_list_push_error(sa->error_list, expr->loc,
                                             "Unary operator '-' requires signed integer or float");
                    goto error_cleanup;
            }
            break;
        case PP_TT_EXCLAMATION:
            if (decayed_operand_type->kind == PP_TK_Bool) {
                unary_type = pp_type_create(PP_TK_Bool);
            } else {
                pp_error_list_push_error(sa->error_list, expr->loc, "Unary operator '!' requires Bool");
                goto error_cleanup;
            }
            break;
        case PP_TT_TILDE:
            if (pp_type_is_flag(decayed_operand_type)) {
                unary_type = (PPType*)decayed_operand_type;  // borrowed from the GTT
                unary_type_owned = false;
            } else if (pp_type_kind_is_integer(decayed_operand_type->kind)) {
                unary_type = pp_type_create(decayed_operand_type->kind);
            } else {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Unary operator '~' requires an integer or flag operand");
                goto error_cleanup;
            }
            break;
        case PP_TT_HASH:
            if (decayed_operand_type->kind == PP_TK_Array || decayed_operand_type->kind == PP_TK_List ||
                decayed_operand_type->kind == PP_TK_String) {
                unary_type = pp_type_create(PP_TK_UInt32);
            } else {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Unary operator '#' requires array, list, or string");
                goto error_cleanup;
            }
            break;
        case PP_TT_AT:
            // address-of
            if (operand_decoration->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Address-of operator '@' requires l-value operand");
                goto error_cleanup;
            }
            if (pp_type_is_reference(operand_type) || pp_type_contains_reference(operand_type)) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Address-of operator '@' cannot be applied to reference");
                goto error_cleanup;
            }
            // build pointer type ptr[operand_type]
            unary_type = pp_type_create(PP_TK_Pointer);
            if (!unary_type) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Failed to create pointer type for address-of");
                goto error_cleanup;
            }
            unary_type->type.pp_type_pointer.pointee = operand_type;
            break;
        case PP_TT_DOLLAR:
            // dereference
            if (decayed_operand_type->kind != PP_TK_Pointer) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Dereference operator '$' requires pointer operand");
                goto error_cleanup;
            }
            {
                const PPType* pointee = decayed_operand_type->type.pp_type_pointer.pointee;
                if (!pointee) {
                    pp_error_list_push_error(sa->error_list, expr->loc, "Pointer dereference has null pointee type");
                    goto error_cleanup;
                }
                decoration = pp_ast_decoration_create(pointee, PP_VALUE_CATEGORY_LVALUE,
                                                      operand_decoration->is_constexpr, false);
                NULL_POINTER_ERROR_HANDLE(decoration);
                expr->unary_expression.decoration = decoration;
                decoration = NULL;
                return 1;
            }
            break;
        default:
            abort();
    }

    if (!unary_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Unary operator requires compatible operand type");
        goto error_cleanup;
    }

    const PPType* interned_unary_type = pp_global_type_table_intern(sa->gtt, unary_type);
    if (!interned_unary_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failed to intern unary result type");
        goto error_cleanup;
    }
    unary_type = NULL;

    decoration = pp_ast_decoration_create(interned_unary_type, PP_VALUE_CATEGORY_RVALUE,
                                          operand_decoration->is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->unary_expression.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    if (unary_type_owned) pp_type_free(unary_type);
    return 0;
}

/* pp_semantic_analyzer_check_literal_expression Checklist (based on cz_semantic_analyzer_check_literal_expression):
 * [x] Validate inputs via asserts
 * [x] Early exit if expr->literal.decoration already set
 * [x] Switch on literal.literal_type and resolve base type
 *    - PP_TT_INTEGER_LITERAL -> int32 alias
 *    - PP_TT_TRUE / PP_TT_FALSE -> bool
 *    - PP_TT_FLOAT_LITERAL -> float
 * [x] Distinguish unsigned suffix for numeric literals, CZ parses lexeme for '.' and 'u' to choose int32/uint32/float
 * [x] Create const-wrapped type via pp_type_create(PP_TK_Const) then intern
 * [x] Create decoration with resolved type, PP_VALUE_CATEGORY_RVALUE, is_constexpr true, escapable_ref false
 * [ ] Explicitly reject string literal with error push, CZ pushes "String literal not yet supported."
 * [-] Set scope level 0 explicitly in decoration as CZ does -> PP does not track scope level per decoration
 */
static int pp_semantic_analyzer_check_literal_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                         PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* literal_type = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_LiteralNode);

    // If already decorated, we are done.
    if (expr->literal.decoration) return 1;

    const PPType* resolved_type = NULL;

    switch (expr->literal.literal_type) {
        case PP_TT_INTEGER_LITERAL: {
            const char* lexeme = expr->literal.lexeme;
            if (!lexeme) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Integer literal lexeme is NULL");
                goto error_cleanup;
            }
            PPTypeKind kind = pp_type_parse_int_literal_suffix(lexeme, NULL);
            if (kind == PP_TK_Void) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Unsupported integer literal suffix");
                goto error_cleanup;
            }
            const char* type_name = NULL;
            switch (kind) {
                case PP_TK_Int8:
                    type_name = "int8";
                    break;
                case PP_TK_UInt8:
                    type_name = "uint8";
                    break;
                case PP_TK_Int16:
                    type_name = "int16";
                    break;
                case PP_TK_UInt16:
                    type_name = "uint16";
                    break;
                case PP_TK_Int32:
                    type_name = "int32";
                    break;
                case PP_TK_UInt32:
                    type_name = "uint32";
                    break;
                case PP_TK_Int64:
                    type_name = "int64";
                    break;
                case PP_TK_UInt64:
                    type_name = "uint64";
                    break;
                case PP_TK_Int128:
                    type_name = "int128";
                    break;
                case PP_TK_UInt128:
                    type_name = "uint128";
                    break;
                default:
                    type_name = NULL;
                    break;
            }
            if (!type_name) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Unknown integer literal type");
                goto error_cleanup;
            }
            resolved_type = pp_global_type_table_find_alias(sa->gtt, type_name);
            if (!resolved_type) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Unknown integer literal type '%s'", type_name);
                goto error_cleanup;
            }
            break;
        }
        case PP_TT_CHAR_LITERAL: {
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "uint8");
            assert(resolved_type);
            /* Validate char literal content */
            const char* lex = expr->literal.lexeme;
            assert(lex);
            size_t len = strlen(lex);
            if (len < 3 || lex[0] != '\'' || lex[len - 1] != '\'') {
                pp_error_list_push_error(sa->error_list, expr->loc, "Invalid char literal");
                goto error_cleanup;
            }
            size_t inner_len = len - 2;
            size_t i = 1;
            size_t decoded_len = 0;
            while (i < len - 1) {
                char c = lex[i];
                if (c == '\\') {
                    if (i + 1 >= len - 1) {
                        pp_error_list_push_error(sa->error_list, expr->loc, "Invalid escape in char literal");
                        goto error_cleanup;
                    }
                    char esc = lex[i + 1];
                    if (esc == 'n' || esc == 't' || esc == 'r' || esc == '0' || esc == '\'' || esc == '\\') {
                        decoded_len++;
                        i += 2;
                        continue;
                    } else if (esc == 'x') {
                        if (i + 3 >= len - 1) {
                            pp_error_list_push_error(sa->error_list, expr->loc, "Invalid hex escape in char literal");
                            goto error_cleanup;
                        }
                        char h1 = lex[i + 2];
                        char h2 = lex[i + 3];
                        if (!isxdigit((unsigned char)h1) || !isxdigit((unsigned char)h2)) {
                            pp_error_list_push_error(sa->error_list, expr->loc, "Invalid hex digits in char literal");
                            goto error_cleanup;
                        }
                        decoded_len++;
                        i += 4;
                        continue;
                    } else {
                        pp_error_list_push_error(sa->error_list, expr->loc, "Invalid escape in char literal");
                        goto error_cleanup;
                    }
                } else {
                    decoded_len++;
                    i++;
                }
            }
            if (decoded_len != 1) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Char literal must represent a single character");
                goto error_cleanup;
            }
            break;
        }
        case PP_TT_TRUE:
        case PP_TT_FALSE:
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "bool");
            assert(resolved_type);
            break;
        case PP_TT_FLOAT_LITERAL:
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "float");
            assert(resolved_type);
            break;
        case PP_TT_STRING_LITERAL:
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "string");
            assert(resolved_type);
            {
                const char* lexeme = expr->literal.lexeme;
                assert(lexeme);
                ssize_t len = pp_tokens_unescape_string_length(lexeme);
                if (len < 0) {
                    pp_error_list_push_error(sa->error_list, expr->loc, "Invalid escape sequence in string literal");
                    goto error_cleanup;
                }
            }
            break;
        case PP_TT_STATIC_STRING_LITERAL: {
            const char* lexeme = expr->literal.lexeme;
            assert(lexeme);
            ssize_t len = pp_tokens_unescape_string_length(lexeme);
            if (len < 0) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Invalid escape sequence in static string literal");
                goto error_cleanup;
            }
            size_t total_size = (size_t)(len + 1); /* implicit NUL */
            const PPType* uint8_type = pp_global_type_table_find_alias(sa->gtt, "uint8");
            assert(uint8_type);

            PPType* arr_type = pp_type_create(PP_TK_Array);
            if (!arr_type) goto error_cleanup;
            arr_type->type.pp_type_array.element_type = uint8_type;
            arr_type->type.pp_type_array.size = total_size;
            resolved_type = pp_global_type_table_intern(sa->gtt, arr_type);
            arr_type = NULL;

            if (!resolved_type) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Failed to intern array type for static string literal");
                goto error_cleanup;
            }
        } break;
        default:
            goto error_cleanup;
    }

    // Add const type modifier
    literal_type = pp_type_create(PP_TK_Const);
    NULL_POINTER_ERROR_HANDLE(literal_type);
    literal_type->type.pp_type_const.const_of = resolved_type;

    resolved_type = pp_global_type_table_intern(sa->gtt, literal_type);
    literal_type = NULL;

    bool is_constexpr = true;
    if (expr->literal.literal_type == PP_TT_STRING_LITERAL) { is_constexpr = false; }
    decoration = pp_ast_decoration_create(resolved_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->literal.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    pp_type_free(literal_type);
    return 0;
}

static int pp_semantic_analyzer_check_null_literal_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                              PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_NullLiteralNode);

    if (expr->null_literal.decoration) return 1;

    const PPType* null_type = NULL;
    if (expr->null_literal.type_node) {
        // Explicitly typed nullptr[T] where T is pointee type
        const PPType* t = pp_type_from_type_node(sa, expr->null_literal.type_node);
        if (!t) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Failed to resolve nullptr type");
            goto error_cleanup;
        }
        // Pointers to `func[...]` are supported; pointers to lambdas are not.
        {
            const PPType* decayed_pointee = pp_type_decay(sa->gtt, t);
            if (pp_type_is_callable(decayed_pointee) && decayed_pointee->kind != PP_TK_Function) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "nullptr cannot be typed as a pointer to a lambda type");
                goto error_cleanup;
            }
        }
        // Build pointer type ptr[T] from T
        PPType* ptr_type = pp_type_create(PP_TK_Pointer);
        if (!ptr_type) goto error_cleanup;
        ptr_type->type.pp_type_pointer.pointee = t;
        const PPType* interned = pp_global_type_table_intern(sa->gtt, ptr_type);
        if (!interned) {
            pp_type_free(ptr_type);
            pp_error_list_push_error(sa->error_list, expr->loc, "Failed to intern nullptr pointer type");
            goto error_cleanup;
        }
        null_type = interned;
    } else {
        // Untyped nullptr – not allowed; require explicit type
        pp_error_list_push_error(sa->error_list, expr->loc, "nullptr requires explicit type, e.g. nullptr[T]");
        goto error_cleanup;
    }

    decoration = pp_ast_decoration_create(null_type, PP_VALUE_CATEGORY_RVALUE, true, false);
    NULL_POINTER_ERROR_HANDLE(decoration);
    expr->null_literal.decoration = decoration;
    decoration = NULL;
    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/* pp_semantic_analyzer_check_identifier_expression Checklist (based on
 * cz_semantic_analyzer_check_identifier_expression): [x] Validate inputs via asserts [x] Early exit if
 * expr->identifier.decoration already set [x] Lookup identifier in symbol table via pp_symbol_table_lookup [x] Retrieve
 * identifier type from symbol [x] Determine value category: RVALUE for callable declaration names
 * (symbol->is_callable_decl), LVALUE for everything else, including function-typed variables/parameters/fields [x]
 * Create decoration with symbol type, value_category, is_constexpr false, escapable_ref = symbol->is_escapable_ref [x]
 * Verify symbol exists and push error if not found
 * [-] Propagate symbol scope level into decoration, CZ uses symbol->scope_level -> PP does not track scope level per
 * symbol [x] Use symbol->is_escapable_ref when creating decoration [x] Lambda capture rules: allow lambda globals,
 * reject function globals, require const for variable globals
 */
static int pp_semantic_analyzer_check_identifier_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                            PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_IdentifierNode);

    // If already decorated, we are done.
    if (expr->identifier.decoration) return 1;

    bool function_is_lambda = false;
    if (sa->current_function_type) { function_is_lambda = sa->current_function_type->kind == PP_TK_Lambda; }

    const PPSymbol* identifier_symbol = pp_symbol_table_lookup(symbol_table, expr->identifier.identifier, true);
    if (!identifier_symbol) {
        pp_error_list_push_error(sa->error_list, expr->loc, "\"%s\" is not previously defined.",
                                 expr->identifier.identifier);
        goto error_cleanup;
    }

    // The drop function of a handle is not a value: it is the effect the compiler attaches to the handle's
    // scope. Reject every use, including taking it as a func[...] value that could then be called.
    if (pp_is_handle_drop_function(sa, expr->identifier.identifier)) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "\"%s\" is the drop function of a handle and can only be called by the compiler "
                                 "when the handle's scope ends.",
                                 expr->identifier.identifier);
        goto error_cleanup;
    }

    if (identifier_symbol->is_constant_decl && !identifier_symbol->constant_value_available) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "constant '%s' is used before its value is available; constants must be declared "
                                 "before use",
                                 expr->identifier.identifier);
        goto error_cleanup;
    }

    if (function_is_lambda && identifier_symbol->is_global) {
        const PPType* sym_type = identifier_symbol->type;
        /* Only declaration names are pure code. A function-typed global variable
         * is ordinary global state, so it falls through to the const check. */
        if (identifier_symbol->is_callable_decl && sym_type->kind == PP_TK_Lambda) {
            /* lambda names are immutable pure code – allowed */
        } else if (identifier_symbol->is_callable_decl && sym_type->kind == PP_TK_Function) {
            pp_error_list_push_error(sa->error_list, expr->loc, "\"%s\" is a regular function used inside lambda",
                                     expr->identifier.identifier);
            goto error_cleanup;
        } else if (identifier_symbol->is_constant_decl) {
            /* Compile-time values are pure and may be read inside a lambda. */
        } else {
            if (pp_type_is_reference(sym_type)) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "\"%s\" is a reference to a symbol declared outside of lambda",
                                         expr->identifier.identifier);
                goto error_cleanup;
            }
            if (!pp_type_is_const(sym_type)) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "\"%s\" is a mutable symbol declared outside of lambda",
                                         expr->identifier.identifier);
                goto error_cleanup;
            }
        }
    }

    const PPType* identifier_type = identifier_symbol->type;
    assert(identifier_type);

    // A callable declaration name is a link-time constant, so it is constexpr; a `constant` is a
    // compile-time value. Both are rvalues and constexpr; neither has assignable storage.
    const bool is_compile_time_value = identifier_symbol->is_callable_decl || identifier_symbol->is_constant_decl;
    decoration = pp_ast_decoration_create(identifier_type,
                                          is_compile_time_value ? PP_VALUE_CATEGORY_RVALUE : PP_VALUE_CATEGORY_LVALUE,
                                          is_compile_time_value, identifier_symbol->is_escapable_ref);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->identifier.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/*
 * [x] Resolve collection base expression
 * [x] Verify base is `Array`/`List` after decay
 * [x] Resolve index expression, require integral type and constexpr where needed
 * [x] Decay element type and create decoration for access result
 * [x] Const propagation from base to element
 * [x] Reference ban for collection element types already enforced at type construction
 */
static int pp_semantic_analyzer_check_array_list_access_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                                   PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* const_wrap = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_ArrayListAccessNode);

    if (expr->array_list_access.decoration) { return 1; }

    // Check base expression
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->array_list_access.expr)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Base expression for array/list access is invalid.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* base_decoration = pp_ast_get_decoration(expr->array_list_access.expr);
    assert(base_decoration);
    const PPType* decayed_base_type = pp_type_decay(sa->gtt, base_decoration->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_base_type);

    if (decayed_base_type->kind != PP_TK_Array && decayed_base_type->kind != PP_TK_List &&
        decayed_base_type->kind != PP_TK_String) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "Array/list/string access requires an array, list, or string base type.");
        goto error_cleanup;
    }

    // Check index expression
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->array_list_access.index)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Index expression for array/list access is invalid.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* index_decoration = pp_ast_get_decoration(expr->array_list_access.index);
    assert(index_decoration);

    const PPType* decayed_index_type = pp_type_decay(sa->gtt, index_decoration->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_index_type);

    // The index must be one of the supported unsigned integer types (uint8 .. uint64). Codegen zero-extends
    // values narrower than 64 bits to the pointer width so high-bit indices cannot become negative offsets.
    if (!pp_type_kind_is_unsigned_up_to_64(decayed_index_type->kind)) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "Array/list index must be uint8, uint16, uint32 or uint64.");
        goto error_cleanup;
    }

    // Compile-time bounds check for fixed-size arrays with constexpr index
    if (decayed_base_type->kind == PP_TK_Array && index_decoration->is_constexpr) {
        size_t idx_val = 0;
        if (pp_array_size_from_simple_expr(sa, expr->array_list_access.index, &idx_val)) {
            size_t arr_size = decayed_base_type->type.pp_type_array.size;
            if (idx_val >= arr_size) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Compile-time evaluation found out-of-bounds array access: index %zu >= size "
                                         "%zu",
                                         idx_val, arr_size);
                goto error_cleanup;
            }
        }
    }

    // Resolve element type
    const PPType* element_type = NULL;
    if (decayed_base_type->kind == PP_TK_Array) {
        element_type = decayed_base_type->type.pp_type_array.element_type;
    } else if (decayed_base_type->kind == PP_TK_List) {
        element_type = decayed_base_type->type.pp_type_list.element_type;
    } else if (decayed_base_type->kind == PP_TK_String) {
        /* string indexing yields uint8 */
        const PPType* uint8_type = pp_global_type_table_find_alias(sa->gtt, "uint8");
        assert(uint8_type);
        element_type = uint8_type;
    }
    NULL_POINTER_ERROR_HANDLE(element_type);

    // Const propagation from base to element
    const PPType* result_type = element_type;
    bool base_is_const = pp_type_is_const(base_decoration->resolved_type);
    if (base_is_const && !pp_type_is_const(element_type)) {
        const_wrap = pp_type_create(PP_TK_Const);
        NULL_POINTER_ERROR_HANDLE(const_wrap);
        const_wrap->type.pp_type_const.const_of = element_type;
        const PPType* interned = pp_global_type_table_intern(sa->gtt, const_wrap);
        const_wrap = NULL;
        NULL_POINTER_ERROR_HANDLE(interned);
        result_type = interned;
    }

    // Value category: propagate lvalue from base if base is lvalue
    PP_ValueCategory value_cat = base_decoration->value_category == PP_VALUE_CATEGORY_LVALUE ? PP_VALUE_CATEGORY_LVALUE
                                                                                             : PP_VALUE_CATEGORY_RVALUE;

    bool is_constexpr = base_decoration->is_constexpr && index_decoration->is_constexpr;

    decoration = pp_ast_decoration_create(result_type, value_cat, is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->array_list_access.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    pp_type_free(const_wrap);
    return 0;
}

/* pp_semantic_analyzer_check_member_access_expression Checklist:
 * [x] Validate inputs via asserts and node type check
 * [x] Early exit if expr already has decoration
 * [x] Check base expression recursively
 * [x] Get base decoration and decay base type
 * [x] Require decayed base kind == PP_TK_Struct, error otherwise (no newtype unwrapping)
 * [x] Find field by name in struct layout, error if missing
 * [x] Const propagation from base to field, skip for reference fields
 * [x] Compute value category from base
 * [x] Inherit is_constexpr from base
 * [x] Inherit is_escapable_ref for lvalues
 * [x] Create decoration with resolved_type, value_category, is_constexpr, is_escapable_ref
 * [x] Attach decoration to expr and return success
 */
static int pp_semantic_analyzer_check_member_access_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                               PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_MemberAccessNode);

    if (expr->member_access.decoration) { return 1; }

    // Check the base expression
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->member_access.object)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Base expression for member access is invalid.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* base_decoration = pp_ast_get_decoration(expr->member_access.object);
    assert(base_decoration);
    const PPType* decayed_base_type = pp_type_decay(sa->gtt, base_decoration->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_base_type);

    if (decayed_base_type->kind != PP_TK_Struct) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Member access is only allowed for structs.");
        goto error_cleanup;
    }

    const char* field_name = expr->member_access.member;
    assert(field_name);
    const PP_StructLayout* layout = decayed_base_type->type.pp_type_struct.layout;
    NULL_POINTER_ERROR_HANDLE(layout);

    const PPType* field_type = NULL;
    for (size_t i = 0; i < layout->field_count; i++) {
        if (layout->fields[i].field_name == field_name) {
            field_type = layout->fields[i].field_type;
            break;
        }
    }
    if (!field_type) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "Member access field \"%s\" is not found on the given struct type.", field_name);
        goto error_cleanup;
    }

    // Const propagation from base to field
    const PPType* result_type = field_type;
    bool base_is_const = pp_type_is_const(base_decoration->resolved_type);
    bool field_is_ref = pp_type_is_reference(field_type);
    if (base_is_const && !pp_type_is_const(field_type) && !field_is_ref) {
        PPType* const_wrap = pp_type_create(PP_TK_Const);
        NULL_POINTER_ERROR_HANDLE(const_wrap);
        const_wrap->type.pp_type_const.const_of = field_type;
        const PPType* interned = pp_global_type_table_intern(sa->gtt, const_wrap);
        const_wrap = NULL;
        NULL_POINTER_ERROR_HANDLE(interned);
        result_type = interned;
    }

    PP_ValueCategory value_cat = base_decoration->value_category == PP_VALUE_CATEGORY_LVALUE ? PP_VALUE_CATEGORY_LVALUE
                                                                                             : PP_VALUE_CATEGORY_RVALUE;

    bool is_constexpr = base_decoration->is_constexpr;
    bool is_escapable_ref = false;
    if (value_cat == PP_VALUE_CATEGORY_LVALUE) { is_escapable_ref = base_decoration->is_escapable_ref; }

    decoration = pp_ast_decoration_create(result_type, value_cat, is_constexpr, is_escapable_ref);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->member_access.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/* pp_semantic_analyzer_check_struct_initializer_expression Checklist (based on cz_semantic_analyzer_check_struct_init):
 * [x] Validate inputs via asserts / NULL_POINTER_ERROR_HANDLE and node type check
 * [x] Early exit if expr already has decoration
 * [x] Look up struct type by name in GTT, verify exists and kind is PP_TK_Struct
 * [x] Check member_count <= field_count, error if too many initializers
 * [x] For each field that is const or reference and has no default initializer, ensure it is present in the initializer
 * list [x] For each initializer member: [x] Check for duplicate member names within the initializer [x] Validate member
 * name exists in struct layout [x] Recursively check member expression via pp_semantic_analyzer_check_expression [x]
 * Retrieve member decoration, ensure not NULL [x] Decay both field type and initializer type and compare for equality
 *     [x] If field type is reference:
 *         [x] Initializer must be l-value
 *         [x] Const-correctness: non-const reference cannot bind to const value
 *     [x] Track constexpr flag: initializer_is_constexpr = AND of all member is_constexpr
 * [x] Create PP_AST_Decoration for the struct init with resolved_type = struct type, PP_VALUE_CATEGORY_RVALUE,
 * is_constexpr as aggregated, escapable_ref false [x] Attach decoration to expr and return success
 */
static int pp_semantic_analyzer_check_struct_initializer_expression(PP_SemanticAnalyzer* sa,
                                                                    PPSymbolTable* symbol_table, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_StructInitializerNode);

    // If already decorated, we are done.
    if (expr->struct_initializer.decoration) return 1;

    const char* struct_name = expr->struct_initializer.identifier;
    assert(struct_name);
    const PPType* struct_type = pp_global_type_table_find_by_name(sa->gtt, struct_name);
    if (!struct_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Unknown type '%s'", struct_name);
        goto error_cleanup;
    }
    // Check if struct type.
    if (struct_type->kind != PP_TK_Struct) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Expected type '%s' to be a struct", struct_name);
        goto error_cleanup;
    }

    assert(struct_type->type.pp_type_struct.layout);
    // Struct layout must be created before structural initialization

    // Initializer count check
    if (expr->struct_initializer.entry_count > struct_type->type.pp_type_struct.layout->field_count) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Too many entries in struct initializer for type '%s'",
                                 struct_name);
        goto error_cleanup;
    }

    // Check if each const/reference field has either default initializer or given initializer
    for (size_t i = 0; i < struct_type->type.pp_type_struct.layout->field_count; i++) {
        const PPStructField* field = &struct_type->type.pp_type_struct.layout->fields[i];
        const PPType* field_type = field->field_type;
        if ((pp_type_is_const(field_type) || pp_type_is_reference(field_type) ||
             pp_type_is_callable(pp_type_decay(sa->gtt, field_type))) &&
            !field->default_initializer) {
            bool found = false;
            for (size_t j = 0; j < expr->struct_initializer.entry_count; j++) {
                if (field->field_name == expr->struct_initializer.entries[j].member_identifier->identifier.identifier) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "No initializer given for const, reference, or callable field \"%s\"",
                                         field->field_name);
                goto error_cleanup;
            }
        }
    }

    // For each initializer field
    bool is_constexpr = true;
    for (size_t i = 0; i < expr->struct_initializer.entry_count; i++) {
        const char* field_name = expr->struct_initializer.entries[i].member_identifier->identifier.identifier;
        // Check for duplicate field names
        for (size_t j = 0; j < i; j++) {
            if (expr->struct_initializer.entries[j].member_identifier->identifier.identifier == field_name) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Duplicate field name \"%s\" in the initializer",
                                         field_name);
                goto error_cleanup;
            }
        }

        // Check if struct initializer field name exists
        const PPType* field_type = NULL;
        {
            bool found = false;
            for (size_t j = 0; j < struct_type->type.pp_type_struct.layout->field_count; j++) {
                if (field_name == struct_type->type.pp_type_struct.layout->fields[j].field_name) {
                    found = true;
                    field_type = struct_type->type.pp_type_struct.layout->fields[j].field_type;
                    assert(field_type);
                    break;
                }
            }
            if (!found) {
                pp_error_list_push_error(sa->error_list, expr->loc, "Field \"%s\" within initializer does not exist",
                                         field_name);
                goto error_cleanup;
            }
        }

        // Check each initializer
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table,
                                                   expr->struct_initializer.entries[i].member_value)) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Initializer for field \"%s\" is invalid", field_name);
            goto error_cleanup;
        }
        const PP_AST_Decoration* field_initializer_decor =
            pp_ast_get_decoration(expr->struct_initializer.entries[i].member_value);
        assert(field_initializer_decor);

        // Decayed type check
        const PPType* decayed_field_type = pp_type_decay(sa->gtt, field_type);
        const PPType* decayed_field_initializer_type = pp_type_decay(sa->gtt, field_initializer_decor->resolved_type);
        if (!pp_type_equal(decayed_field_type, decayed_field_initializer_type)) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Initializer for field \"%s\" does not match the field type.", field_name);
            goto error_cleanup;
        }
        // Reference
        if (pp_type_is_reference(field_type)) {
            // L-value check
            if (field_initializer_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Field \"%s\" is a reference, but the initializer is not an l-value.",
                                         field_name);
                goto error_cleanup;
            }
            // Non-const field cannot bind to const variable.
            if (!pp_type_is_const(field_type) && pp_type_is_const(field_initializer_decor->resolved_type)) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Field \"%s\" is a mutable reference, but the initializer is immutable",
                                         field_name);
                goto error_cleanup;
            }
        }

        if (!field_initializer_decor->is_constexpr) { is_constexpr = false; }
    }

    decoration = pp_ast_decoration_create(struct_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->struct_initializer.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/* pp_semantic_analyzer_check_cast_expression Checklist (based on cz_semantic_analyzer_cast_expression):
 * [x] Validate inputs via asserts
 * [x] Early exit if expr->cast_expression.decoration already set
 * [x] Recursively check inner expression via pp_semantic_analyzer_check_expression
 * [x] Retrieve target type via pp_type_from_type_node
 * [x] Reject casting to reference type with error push
 * [x] Reject casting to const type with error push
 * [x] Decay expression type and target type via pp_type_decay
 * [x] Resolve machine type via pp_type_resolve_machine_type for both decayed types
 * [x] Allow cast if resolved machine types are equal or both numerical via pp_type_is_numerical
 * [x] Create decoration with target_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr propagated from expr, escapable_ref
 * false
 * [-] Propagate environment scope level into decoration, CZ uses env->scope_level -> N/A in PP, scope_level field is
 * commented out [x] Preserve is_constexpr from inner expression explicitly as done in PP, matches CZ intent
 */
static int pp_semantic_analyzer_check_cast_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                      PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_CastExpressionNode);

    // If already decorated, we are done.
    if (expr->cast_expression.decoration) return 1;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->cast_expression.expression)) {
        goto error_cleanup;
    }
    const PP_AST_Decoration* expr_decoration = pp_ast_get_decoration(expr->cast_expression.expression);
    assert(expr_decoration);

    const PPType* target_type = pp_type_from_type_node(sa, expr->cast_expression.type_expr);
    assert(target_type);

    if (pp_type_is_top_level_const(target_type)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Casting to const is not allowed.");
        goto error_cleanup;
    }

    // A handle can only be produced by a call that returns one; casting would let a raw value masquerade as an
    // owned resource. The reverse direction (handle -> underlying) stays legal and is read-only.
    if (pp_type_contains_handle(target_type)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Casting to a handle type is not allowed.");
        goto error_cleanup;
    }

    const PPType* expr_type = expr_decoration->resolved_type;
    const PPType* decayed_expr_type = pp_type_decay(sa->gtt, expr_type);
    NULL_POINTER_ERROR_HANDLE(decayed_expr_type);
    const PPType* decayed_target_type = pp_type_decay(sa->gtt, target_type);
    NULL_POINTER_ERROR_HANDLE(decayed_target_type);

    const PPType* resolved_expr_type = pp_type_resolve_machine_type(decayed_expr_type);
    const PPType* resolved_target_type = pp_type_resolve_machine_type(decayed_target_type);

    bool cast_ok = false;

    // Enum and flag casts are nominal. An identity cast is a no-op, a nominal value can be read as an integer, and
    // an integer can be reinterpreted as a flag (every bit pattern is a valid flag value). Integers cannot be cast
    // into an enum, whose value set is closed.
    const bool expr_is_nominal = pp_type_is_enum_or_flag(decayed_expr_type);
    const bool target_is_nominal = pp_type_is_enum_or_flag(decayed_target_type);
    if (expr_is_nominal || target_is_nominal) {
        if (expr_is_nominal && target_is_nominal) {
            if (!pp_type_equal(decayed_expr_type, decayed_target_type)) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Casting between different enum or flag types is not allowed.");
                goto error_cleanup;
            }
            cast_ok = true;
        } else if (expr_is_nominal) {
            cast_ok = pp_semantic_type_is_decayed_integer_primitive(decayed_target_type);
            if (!cast_ok) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Casting an enum or flag is only allowed to an integer type.");
                goto error_cleanup;
            }
        } else if (pp_type_is_flag(decayed_target_type)) {
            cast_ok = pp_semantic_type_is_decayed_integer_primitive(decayed_expr_type);
            if (!cast_ok) {
                pp_error_list_push_error(sa->error_list, expr->loc,
                                         "Casting to a flag is only allowed from an integer type.");
                goto error_cleanup;
            }
        } else {
            pp_error_list_push_error(sa->error_list, expr->loc, "Casting from an integer to an enum is not allowed.");
            goto error_cleanup;
        }
    } else if (pp_type_is_callable(decayed_expr_type) || pp_type_is_callable(decayed_target_type)) {
        // v1 callable casts: only lambda[...] -> func[...] with an identical signature.
        // Everything else involving a callable type is rejected (func -> lambda,
        // lambda -> lambda, func -> func, callable <-> non-callable).
        if (pp_type_is_callable(decayed_expr_type) && pp_type_is_callable(decayed_target_type) &&
            decayed_expr_type->kind == PP_TK_Lambda && decayed_target_type->kind == PP_TK_Function) {
            cast_ok = pp_semantic_callable_signature_equal(decayed_expr_type, decayed_target_type);
        }
        if (!cast_ok) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Only a lambda[...] value can be cast to an identical func[...] signature.");
            goto error_cleanup;
        }
    } else if (pp_type_is_reference(target_type)) {
        // Casting to reference is not allowed
        // error already handled by cast_ok staying false
        // Cast from reference source to pointer target is allowed
    } else if (pp_type_is_reference(expr_type) && pp_type_is_pointer(decayed_target_type)) {
        const PPType* ref_to = expr_type->type.pp_type_reference.reference_to;
        const PPType* pointee = decayed_target_type->type.pp_type_pointer.pointee;
        if (pp_type_can_assign(pointee, ref_to)) { cast_ok = true; }
        // Pointer to pointer cast
    } else if (pp_type_is_pointer(decayed_expr_type) && pp_type_is_pointer(decayed_target_type)) {
        // Allow any pointer to any pointer, but enforce const matrix: no const stripping
        const PPType* src_pointee = decayed_expr_type->type.pp_type_pointer.pointee;
        const PPType* tgt_pointee = decayed_target_type->type.pp_type_pointer.pointee;
        bool src_const = pp_type_is_const(src_pointee);
        bool tgt_const = pp_type_is_const(tgt_pointee);
        // const stripping is not allowed: tgt non-const requires src non-const
        if (tgt_const || !src_const) { cast_ok = true; }
        // Pointer to integer cast: ptr[T] as numeric
    } else if (pp_type_is_pointer(decayed_expr_type) && pp_type_is_numerical(resolved_target_type)) {
        cast_ok = true;
        // Integer to pointer cast: numeric as ptr[T]
    } else if (pp_type_is_numerical(resolved_expr_type) && pp_type_is_pointer(decayed_target_type)) {
        cast_ok = true;
        // Generic machine type equality or numerical cast
    } else {
        if (pp_type_equal(resolved_expr_type, resolved_target_type)) {
            cast_ok = true;
        } else if (pp_type_is_numerical(resolved_expr_type) && pp_type_is_numerical(resolved_target_type)) {
            cast_ok = true;
        }
    }

    if (!cast_ok) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "Casting is not supported from the given expression to the given target type.");
        goto error_cleanup;
    }

    decoration = pp_ast_decoration_create(target_type, PP_VALUE_CATEGORY_RVALUE, expr_decoration->is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->cast_expression.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

/* Collection Initializer Expression Checklist (PP-specific):
 * [x] Verify node kind is PP_AST_CollectionInitializerNode and target type context exists.
 * [x] Determine collection kind from resolved target type:
 *      - PP_TK_Array  → fixed size, element type = array element type
 *      - PP_TK_List   → unbounded, element type = list element type
 * [x] For arrays with static size:
 *      - Compare entry count to array size; error if > size, allow <= size for implicit zero-fill.
 *      - Reject zero-size arrays or treat as empty per policy.
 * [x] For each initializer entry:
 *      - Recursively check expression via pp_semantic_analyzer_check_expression.
 *      - Require constexpr in global/struct default contexts.
 *      - Ensure decayed element type matches collection element type.
 *      - If element type is reference: initializer must be l-value and const-correct.
 * [x] For lists:
 *      - No size check; allow any number of entries.
 *      - Same per-element validity, constexpr and type matching.
 * [x] Populate AST decoration:
 *      - resolved_type = collection type
 *      - is_constexpr = all entries constexpr
 *      - value_category set appropriately for context
 * [x] Error reporting via sa->error_list with line/col from expr.
 */
static int pp_semantic_analyzer_check_collection_initializer_expression(PP_SemanticAnalyzer* sa,
                                                                        PPSymbolTable* symbol_table,
                                                                        PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* tmp = NULL;
    const PPType* coll_type = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_CollectionInitializerNode);

    // If already decorated, we are done.
    if (expr->collection_initializer.decoration) return 1;

    // Determine collection kind from resolved target type
    PP_AST_Node* type_expr = expr->collection_initializer.type_expr;
    if (!type_expr) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Collection initializer missing type");
        goto error_cleanup;
    }
    const PPType* elem_type = pp_type_from_type_node(sa, type_expr);
    if (!elem_type) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failed to resolve collection element type");
        goto error_cleanup;
    }

    int is_array = expr->collection_initializer.size_expr != NULL;
    /* Element type resolved, collection kind can be inferred:
     *  - size_expr present => fixed-size array initializer
     *  - no size_expr    => list initializer
     */

    if (is_array) {
        size_t arr_size = 0;
        if (!pp_array_size_from_simple_expr(sa, expr->collection_initializer.size_expr, &arr_size)) {
            pp_error_list_push_error(sa->error_list, expr->loc,
                                     "Invalid array size expression in collection initializer");
            goto error_cleanup;
        }
        if (arr_size == 0) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Array size cannot be zero");
            goto error_cleanup;
        }
        // Oversize initializer is an error; undersize is allowed for implicit zero-fill
        if (expr->collection_initializer.element_count > arr_size) {
            pp_error_list_push_error(sa->error_list, expr->loc, "Collection initializer has too many elements");
            goto error_cleanup;
        }
        tmp = pp_type_create(PP_TK_Array);
        if (!tmp) goto error_cleanup;
        tmp->type.pp_type_array.element_type = elem_type;
        tmp->type.pp_type_array.size = arr_size;
        coll_type = pp_global_type_table_intern(sa->gtt, tmp);
        if (!coll_type) goto error_cleanup;
        tmp = NULL;
    } else {
        tmp = pp_type_create(PP_TK_List);
        if (!tmp) goto error_cleanup;
        tmp->type.pp_type_list.element_type = elem_type;
        coll_type = pp_global_type_table_intern(sa->gtt, tmp);
        if (!coll_type) goto error_cleanup;
        tmp = NULL;
    }

    /* TODO: Continue with per-element checks and decoration population */
    bool all_constexpr = true;
    for (unsigned int i = 0; i < expr->collection_initializer.element_count; i++) {
        PP_AST_Node* elem = expr->collection_initializer.elements[i];
        assert(elem);
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, elem)) { goto error_cleanup; }
        const PP_AST_Decoration* elem_dec = pp_ast_get_decoration(elem);
        assert(elem_dec);
        // Verify element type matches collection element type after decay
        if (elem_dec->resolved_type) {
            const PPType* decayed_elem_type = pp_type_decay(sa->gtt, elem_dec->resolved_type);
            const PPType* decayed_coll_elem_type = pp_type_decay(sa->gtt, elem_type);
            if (!pp_type_can_assign(decayed_coll_elem_type, decayed_elem_type)) {
                pp_error_list_push_error(sa->error_list, elem->loc, "Collection element type mismatch");
                goto error_cleanup;
            }
        }
        // Collection element types must never be references – rejected at type construction
        assert(!pp_type_contains_reference(elem_type) && "Collection element type must not contain a reference");
        if (!elem_dec->is_constexpr) { all_constexpr = false; }
    }

    bool is_constexpr = is_array && all_constexpr;
    // Empty list is constexpr; non-empty lists remain non-constexpr
    if (!is_array && expr->collection_initializer.element_count == 0) { is_constexpr = true; }
    // Lists are never constexpr per language design except empty
    decoration = pp_ast_decoration_create(coll_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr, false);
    if (!decoration) goto error_cleanup;
    expr->collection_initializer.decoration = decoration;
    decoration = NULL;
    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    pp_type_free(tmp);
    return 0;
}

const PPSymbol* pp_semantic_analyzer_operator_lookup(PP_SemanticAnalyzer* sa, const char* spelling,
                                                     const PPType* lhs_type, const PPType* rhs_type) {
    if (!sa || !spelling) return NULL;
    const PPType* dl = pp_type_decay(sa->gtt, lhs_type);
    const PPType* dr = pp_type_decay(sa->gtt, rhs_type);
    PP_OperatorOverloadRegistry* reg = sa->operator_overloads;
    if (!reg) return NULL;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->entries[i].spelling && strcmp(reg->entries[i].spelling, spelling) == 0 &&
            reg->entries[i].lhs_type == dl && reg->entries[i].rhs_type == dr) {
            return reg->entries[i].callable;
        }
    }
    return NULL;
}
const PPSymbol* pp_semantic_analyzer_unary_operator_lookup(PP_SemanticAnalyzer* sa, const char* spelling,
                                                           const PPType* operand_type) {
    if (!sa || !spelling) return NULL;
    const PPType* d = pp_type_decay(sa->gtt, operand_type);
    PP_OperatorOverloadRegistry* reg = sa->operator_overloads;
    if (!reg) return NULL;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->entries[i].arity == 1 && reg->entries[i].spelling && strcmp(reg->entries[i].spelling, spelling) == 0 &&
            reg->entries[i].lhs_type == d) {
            return reg->entries[i].callable;
        }
    }
    return NULL;
}

static bool pp_type_is_top_level_const(const PPType* type) { return type && type->kind == PP_TK_Const; }

/* Signature identity of two callable types, ignoring the callable kind itself.
 * Used by the explicit lambda[...] -> func[...] widening cast. */
static bool pp_semantic_callable_signature_equal(const PPType* a, const PPType* b) {
    size_t a_count = 0;
    size_t b_count = 0;
    const PPType* const* a_params = NULL;
    const PPType* const* b_params = NULL;
    const PPType* a_ret = NULL;
    const PPType* b_ret = NULL;
    size_t i = 0;

    if (!a || !b) return false;
    if (!pp_type_is_callable(a) || !pp_type_is_callable(b)) return false;

    a_count = (a->kind == PP_TK_Lambda) ? a->type.pp_type_lambda.param_count : a->type.pp_type_function.param_count;
    b_count = (b->kind == PP_TK_Lambda) ? b->type.pp_type_lambda.param_count : b->type.pp_type_function.param_count;
    if (a_count != b_count) return false;

    a_params = (a->kind == PP_TK_Lambda) ? a->type.pp_type_lambda.param_types : a->type.pp_type_function.param_types;
    b_params = (b->kind == PP_TK_Lambda) ? b->type.pp_type_lambda.param_types : b->type.pp_type_function.param_types;
    for (i = 0; i < a_count; i++) {
        if (!pp_type_equal(a_params[i], b_params[i])) return false;
    }

    a_ret = (a->kind == PP_TK_Lambda) ? a->type.pp_type_lambda.return_type : a->type.pp_type_function.return_type;
    b_ret = (b->kind == PP_TK_Lambda) ? b->type.pp_type_lambda.return_type : b->type.pp_type_function.return_type;
    return pp_type_equal(a_ret, b_ret);
}

/* True when the (already decayed) type is a callable signature. Callable values
 * are code pointers: no null value, no operators, no storage inside collections. */
static bool pp_type_is_callable(const PPType* type) {
    if (!type) return false;
    return type->kind == PP_TK_Function || type->kind == PP_TK_Lambda || type->kind == PP_TK_Operator;
}

static bool pp_type_can_assign(const PPType* target, const PPType* source) {
    if (!target || !source) return false;
    if (target == source) return true;
    // Strip const from source for assignability
    if (source->kind == PP_TK_Const) { return pp_type_can_assign(target, source->type.pp_type_const.const_of); }
    if (target->kind != source->kind) {
        // Allow assigning to const-wrapped target from inner source
        if (target->kind == PP_TK_Const) { return pp_type_can_assign(target->type.pp_type_const.const_of, source); }
        return false;
    }
    switch (target->kind) {
        case PP_TK_Pointer: {
            assert(source->kind == PP_TK_Pointer);
            const PPType* src_pointee = source->type.pp_type_pointer.pointee;
            const PPType* tgt_pointee = target->type.pp_type_pointer.pointee;
            // Prevent const stripping on pointer pointee
            if (pp_type_is_const(src_pointee) && !pp_type_is_const(tgt_pointee)) { return false; }
            return pp_type_can_assign(tgt_pointee, src_pointee);
        }
        case PP_TK_Const:
            return pp_type_can_assign(target->type.pp_type_const.const_of, source->type.pp_type_const.const_of);
        case PP_TK_Array:
            if (target->type.pp_type_array.size != source->type.pp_type_array.size) return false;
            return pp_type_can_assign(target->type.pp_type_array.element_type, source->type.pp_type_array.element_type);
        case PP_TK_List:
            return pp_type_can_assign(target->type.pp_type_list.element_type, source->type.pp_type_list.element_type);
        default:
            return pp_type_equal(target, source);
    }
}

static int pp_semantic_analyzer_check_list_push(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                PP_AST_Node* expr) {
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_ListPushNode);
    const PP_AST_Decoration* list_dec = NULL;
    const PP_AST_Decoration* val_dec = NULL;
    const PPType* list_type = NULL;
    const PPType* elem_type = NULL;
    PP_AST_Decoration* dec = NULL;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->list_push.list)) goto error_cleanup;
    list_dec = pp_ast_get_decoration(expr->list_push.list);
    if (!list_dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_push: list expression has no type");
        goto error_cleanup;
    }
    assert(list_dec->resolved_type);
    if (list_dec->value_category != PP_VALUE_CATEGORY_LVALUE) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_push: list must be an lvalue");
        goto error_cleanup;
    }
    list_type = list_dec->resolved_type;
    if (pp_type_is_const(list_type)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_push: list must be mutable");
        goto error_cleanup;
    }
    const PPType* list_type_decayed = pp_type_decay(sa->gtt, list_type);
    if (!list_type_decayed || list_type_decayed->kind != PP_TK_List) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_push: first argument must be a list");
        goto error_cleanup;
    }
    elem_type = list_type_decayed->type.pp_type_list.element_type;
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->list_push.value)) goto error_cleanup;
    val_dec = pp_ast_get_decoration(expr->list_push.value);
    if (!val_dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_push: value expression has no type");
        goto error_cleanup;
    }
    if (!pp_type_can_assign(elem_type, val_dec->resolved_type)) {
        pp_error_list_push_error(sa->error_list, expr->loc,
                                 "list_push: value type not assignable to list element type");
        goto error_cleanup;
    }
    dec = pp_ast_decoration_create(NULL, PP_VALUE_CATEGORY_RVALUE, false, false);
    if (!dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failed to allocate decoration for list_push");
        goto error_cleanup;
    }
    expr->list_push.decoration = dec;
    dec = NULL;
    return 1;

error_cleanup:
    pp_ast_decoration_free(dec);
    return 0;
}

static int pp_semantic_analyzer_check_list_pop(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                               PP_AST_Node* expr) {
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_ListPopNode);
    const PP_AST_Decoration* list_dec = NULL;
    const PPType* list_type = NULL;
    const PPType* elem_type = NULL;
    PP_AST_Decoration* dec = NULL;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->list_pop.list)) goto error_cleanup;
    list_dec = pp_ast_get_decoration(expr->list_pop.list);
    if (!list_dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_pop: list expression has no type");
        goto error_cleanup;
    }
    if (list_dec->value_category != PP_VALUE_CATEGORY_LVALUE) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_pop: list must be an lvalue");
        goto error_cleanup;
    }
    list_type = list_dec->resolved_type;
    if (pp_type_is_const(list_type)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_pop: list must be mutable");
        goto error_cleanup;
    }
    const PPType* list_type_decayed = pp_type_decay(sa->gtt, list_type);
    if (!list_type_decayed || list_type_decayed->kind != PP_TK_List) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_pop: argument must be a list");
        goto error_cleanup;
    }
    elem_type = list_type_decayed->type.pp_type_list.element_type;
    dec = pp_ast_decoration_create(elem_type, PP_VALUE_CATEGORY_RVALUE, false, false);
    if (!dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failed to allocate decoration for list_pop");
        goto error_cleanup;
    }
    expr->list_pop.decoration = dec;
    dec = NULL;
    return 1;

error_cleanup:
    pp_ast_decoration_free(dec);
    return 0;
}

static int pp_semantic_analyzer_check_list_clear(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table,
                                                 PP_AST_Node* expr) {
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_ListClearNode);
    const PP_AST_Decoration* list_dec = NULL;
    const PPType* list_type = NULL;
    PP_AST_Decoration* dec = NULL;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->list_clear.list)) goto error_cleanup;
    list_dec = pp_ast_get_decoration(expr->list_clear.list);
    if (!list_dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_clear: list expression has no type");
        goto error_cleanup;
    }
    if (list_dec->value_category != PP_VALUE_CATEGORY_LVALUE) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_clear: list must be an lvalue");
        goto error_cleanup;
    }
    list_type = list_dec->resolved_type;
    if (pp_type_is_const(list_type)) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_clear: list must be mutable");
        goto error_cleanup;
    }
    const PPType* list_type_decayed = pp_type_decay(sa->gtt, list_type);
    if (!list_type_decayed || list_type_decayed->kind != PP_TK_List) {
        pp_error_list_push_error(sa->error_list, expr->loc, "list_clear: argument must be a list");
        goto error_cleanup;
    }
    dec = pp_ast_decoration_create(NULL, PP_VALUE_CATEGORY_RVALUE, false, false);
    if (!dec) {
        pp_error_list_push_error(sa->error_list, expr->loc, "Failed to allocate decoration for list_clear");
        goto error_cleanup;
    }
    expr->list_clear.decoration = dec;
    dec = NULL;
    return 1;

error_cleanup:
    pp_ast_decoration_free(dec);
    return 0;
}
