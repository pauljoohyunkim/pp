#include "pp_semantic_analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
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

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr) == NULL) goto error_cleanup; } while (0)

static int pp_array_size_from_simple_expr(PP_SemanticAnalyzer* sa, const PP_AST_Node* simple_expr_node, size_t* val);
static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node);
static bool pp_type_contains_reference(const PPType* type);
static int pp_type_validate_collection_element(PP_SemanticAnalyzer* sa, const PPType* elem_type, unsigned int line, unsigned int col);
/* Pass I */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa);
static int pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_struct_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_variable_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);

/* Pass II */
static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa);
static int pp_semantic_analyzer_full_analyze_struct_decl_fields(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_struct_cycle_detect(PP_SemanticAnalyzer* sa, const PPType* type, PP_StructRecursiveCycleState* states);
static int pp_semantic_analyzer_full_analyze_struct_decl_initializers(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_function_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_lambda_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_global_variable_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);

/* Function Bodies */
static int pp_semantic_analyzer_full_analyze_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_variable_declaration(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_assignment_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_return_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_if_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_for_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_while_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_statement_list(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node);

/* Expression Checking */
static int pp_semantic_analyzer_check_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_function_call(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_binary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_unary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_literal_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_identifier_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_member_access_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_struct_initializer_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_cast_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);
static int pp_semantic_analyzer_check_collection_initializer_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr);

/**
 * @brief Creates semantic analyzer from parser
 */
PP_SemanticAnalyzer* pp_semantic_analyzer_create(PP_Parser* parser) {
    PP_SemanticAnalyzer* sa = NULL;
    PP_GlobalTypeTable* gtt = NULL;
    PPSymbolTable* gst = NULL;

    if (!parser) {
        pp_error_list_push_error(sa->error_list, sa->filename, 0, 0, "Parser is NULL");
        goto error_cleanup;
    }

    sa = (PP_SemanticAnalyzer*)calloc(1, sizeof(PP_SemanticAnalyzer));
    if (!sa) {
        pp_error_list_push_error(NULL, 0, 0, 0, "Failed to allocate memory for semantic analyzer");
        goto error_cleanup;
    }

    /* Transfer ownership of parser resources */
    sa->filename = parser->filename;
    sa->code = parser->code;
    parser->code = NULL;
    sa->code_length = parser->code_length;

    sa->tokens = parser->tokens;
    parser->tokens = NULL;
    sa->n_tokens = parser->n_tokens;

    sa->program = parser->program;
    parser->program = NULL;

    sa->sp = parser->sp;
    parser->sp = NULL;

    sa->error_list = parser->error_list;
    parser->error_list = NULL;

    sa->current_function_type = NULL;

    gtt = pp_global_type_table_create();
    if (!gtt) {
        pp_error_list_push_error(sa->error_list, sa->filename, 0, 0, "Failed to create global type table");
        goto error_cleanup;
    }

    gst = pp_symbol_table_create(SCOPE_LEVEL_GLOBAL);
    if (!gst) {
        pp_error_list_push_error(sa->error_list, sa->filename, 0, 0, "Failed to create symbol table");
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
    }

    return sa;
error_cleanup:
    if (sa) {
        free((char*)sa->code);
        free(sa->tokens);
        if (sa->error_list) {
            pp_error_list_free(sa->error_list);
        }
        pp_global_type_table_free(sa->gtt);
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
    pp_string_pool_free(sa->sp);
    pp_error_list_free(sa->error_list);
    pp_global_type_table_free(sa->gtt);
    pp_symbol_table_free(sa->gst);

    free(sa);
}

/**
 * @brief Invoke semantic analyzer to analyze the AST.
 */
int pp_semantic_analyzer_analyze(PP_SemanticAnalyzer* sa) {
    if (!sa) return 0;

    if (!pp_semantic_analyzer_build_global_table(sa)) {
        return 0;
    }
    if (!pp_semantic_analyzer_full_analyze(sa)) {
        return 0;
    }

    return 1;
}


static bool pp_type_contains_reference(const PPType* t) {
    if (!t) return false;
    switch (t->kind) {
        case PP_TK_Reference:
            return true;
        case PP_TK_Const:
            return pp_type_contains_reference(t->type.pp_type_const.const_of);
        case PP_TK_Array:
            return pp_type_contains_reference(t->type.pp_type_array.element_type);
        case PP_TK_List:
            return pp_type_contains_reference(t->type.pp_type_list.element_type);
        case PP_TK_Newtype:
            return pp_type_contains_reference(t->type.pp_type_newtype.underlying);
        case PP_TK_Struct:
            if (t->type.pp_type_struct.layout) {
                for (size_t i = 0; i < t->type.pp_type_struct.layout->field_count; i++) {
                    if (pp_type_contains_reference(t->type.pp_type_struct.layout->fields[i].field_type)) {
                        return true;
                    }
                }
            }
            return false;
        case PP_TK_Function:
            if (pp_type_contains_reference(t->type.pp_type_function.return_type)) return true;
            for (size_t i = 0; i < t->type.pp_type_function.param_count; i++) {
                if (pp_type_contains_reference(t->type.pp_type_function.param_types[i])) return true;
            }
            return false;
        case PP_TK_Lambda:
            if (pp_type_contains_reference(t->type.pp_type_lambda.return_type)) return true;
            for (size_t i = 0; i < t->type.pp_type_lambda.param_count; i++) {
                if (pp_type_contains_reference(t->type.pp_type_lambda.param_types[i])) return true;
            }
            return false;
        default:
            return false;
    }
}

static int pp_array_size_from_simple_expr(PP_SemanticAnalyzer* sa, const PP_AST_Node* simple_expr_node, size_t* val) {
    assert(sa);
    assert(simple_expr_node);
    assert(val);

    switch (simple_expr_node->node_type) {
        case PP_AST_LiteralNode:
            {
                if (simple_expr_node->literal.literal_type != PP_TT_UNSIGNED_INTEGER_LITERAL) {
                    pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                    "For array size expression, literals can only be unsigned integer");
                    goto error_cleanup;
                }
                char* endptr;
                errno = 0;
                unsigned long long parsed_val = strtoull(simple_expr_node->literal.lexeme, &endptr, 10);
                if (errno != 0 || simple_expr_node->literal.lexeme == endptr) {
                    pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                    "Could not parse literal to number");
                    goto error_cleanup;
                }
                if (parsed_val > SIZE_MAX) {
                    pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                    "Array size literal out of range for size_t");
                    goto error_cleanup;
                }
                *val = (size_t) parsed_val;
                return 1;
            }
            break;
        case PP_AST_BinaryExpressionNode:
            {
                switch (simple_expr_node->binary_expression.op) {
                    case PP_TT_PLUS:
                    case PP_TT_STAR:
                    case PP_TT_SLASH:
                    case PP_TT_PERCENT:
                        break;
                    default:
                        pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                        "The only binary operator allowed in array size are \'+\', \'*\', \'/\', and \'%\'");
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
                            pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                            "Array size addition overflow");
                            goto error_cleanup;
                        }
                        *val = left_val + right_val;
                        break;
                    case PP_TT_STAR:
                        if (left_val != 0 && right_val > SIZE_MAX / left_val) {
                            pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                            "Array size multiplication overflow");
                            goto error_cleanup;
                        }
                        *val = left_val * right_val;
                        break;
                    case PP_TT_SLASH:
                        if (right_val == 0) {
                            pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                            "Division by zero in array size expression");
                            goto error_cleanup;
                        }
                        *val = left_val / right_val;
                        break;
                    case PP_TT_PERCENT:
                        if (right_val == 0) {
                            pp_error_list_push_error(sa->error_list, sa->filename, simple_expr_node->line, simple_expr_node->col,
                            "Modulo by zero in array size expression");
                            goto error_cleanup;
                        }
                        *val = left_val % right_val;
                        break;
                    default:
                        abort();
                }
                return 1;
            }
            break;
        default:
            goto error_cleanup;
    }

error_cleanup:
    return 0;
}

static int pp_type_validate_collection_element(PP_SemanticAnalyzer* sa, const PPType* elem_type, unsigned int line, unsigned int col) {
    if (!elem_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, line, col, "Element type is null");
        return 0;
    }
    if (pp_type_is_reference(elem_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, line, col, "element type cannot be a reference");
        return 0;
    }
    if (elem_type->kind == PP_TK_Void) {
        pp_error_list_push_error(sa->error_list, sa->filename, line, col, "Array/List element type cannot be void");
        return 0;
    }
    if (elem_type->kind == PP_TK_Function || elem_type->kind == PP_TK_Lambda) {
        pp_error_list_push_error(sa->error_list, sa->filename, line, col, "Array/List element type cannot be function/lambda");
        return 0;
    }
    if (pp_type_contains_reference(elem_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, line, col, "Array/List element type cannot transitively contain a reference");
        return 0;
    }
    return 1;
}

static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node) {
    assert(sa);
    assert(type_node);
    assert(type_node->node_type == PP_AST_TypeExpressionNode);

    PPType *temp_type = NULL;                 /* owned – can be freed on error */
    const PPType* gtt_lookedup_type = NULL;   /* For alias and named types */
    const PPType *gtt_registered_type = NULL;/* borrowed from GTT – must not be freed here */

    // Resolve base type via new recursive AST or legacy fields
    const PP_AST_Node* inner = type_node->type_expression.type_node;
    PP_AST_TypeKind base_kind = PP_AST_TYPE_KIND_IDENTIFIER;
    const char* base_identifier = NULL;

    if (inner) {
        if (inner->node_type == PP_AST_TypeBaseNode) {
            base_kind = inner->type_base.kind;
            base_identifier = inner->type_base.identifier_name;
        } else if (inner->node_type == PP_AST_TypeArrayNode || inner->node_type == PP_AST_TypeListNode) {
            // Collection handling deferred to later step – for now fall through to collection path
            // We will handle via separate logic below
        } else {
            pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Unsupported type node");
            goto error_cleanup;
        }
    } else {
        // legacy fallback for existing code paths
        base_kind = type_node->type_expression.kind;
        base_identifier = type_node->type_expression.identifier_name;
    }

    // Handle collection types from new AST
    if (inner && (inner->node_type == PP_AST_TypeArrayNode || inner->node_type == PP_AST_TypeListNode)) {
        const PPType* elem_type = NULL;
        if (inner->node_type == PP_AST_TypeArrayNode) {
            elem_type = pp_type_from_type_node(sa, inner->type_array.element_type);
        } else {
            elem_type = pp_type_from_type_node(sa, inner->type_list.element_type);
        }
        if (!elem_type) goto error_cleanup;
        if (!pp_type_validate_collection_element(sa, elem_type, type_node->line, type_node->col)) goto error_cleanup;
        if (inner->node_type == PP_AST_TypeArrayNode) {
            size_t sz = 0;
            if (!pp_array_size_from_simple_expr(sa, inner->type_array.size_expr, &sz)) {
                pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Could not compute array size");
                goto error_cleanup;
            }
            if (sz == 0) {
                pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Array size cannot be zero");
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
            pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to intern array/list");
            goto error_cleanup;
        }
        // Skip base_kind switch; proceed to const/reference handling below
        goto after_base_resolution;
    }

    switch (base_kind) {
        case PP_AST_TYPE_KIND_VOID:
            temp_type = pp_type_create(PP_TK_Void);
            break;
        case PP_AST_TYPE_KIND_INT32:
            temp_type = pp_type_create(PP_TK_Int32);
            break;
        case PP_AST_TYPE_KIND_UINT32:
            temp_type = pp_type_create(PP_TK_UInt32);
            break;
        case PP_AST_TYPE_KIND_FLOAT:
            temp_type = pp_type_create(PP_TK_Float);
            break;
        case PP_AST_TYPE_KIND_BOOL:
            temp_type = pp_type_create(PP_TK_Bool);
            break;
        case PP_AST_TYPE_KIND_IDENTIFIER:
            gtt_lookedup_type = pp_global_type_table_find_by_name(sa->gtt, base_identifier);
            if (gtt_lookedup_type) {
                break;
            }
            gtt_lookedup_type = pp_global_type_table_find_alias(sa->gtt, base_identifier);
            if (gtt_lookedup_type) {
                break;
            }
            break;
        default:
            abort();
            break;
    }
    if (!temp_type && !gtt_lookedup_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to create type");
        goto error_cleanup;
    }

    //gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type ? temp_type : gtt_lookedup_type);
    gtt_registered_type = temp_type ? pp_global_type_table_intern(sa->gtt, temp_type) : gtt_lookedup_type;
    temp_type = NULL;
    if (!gtt_registered_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to intern type");
        goto error_cleanup;
    }

after_base_resolution:
    if (type_node->type_expression.is_const) {
        // Canonicalize const for arrays/lists by pushing const inward
        if (gtt_registered_type && (gtt_registered_type->kind == PP_TK_Array || gtt_registered_type->kind == PP_TK_List)) {
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
            temp_type = (gtt_registered_type->kind == PP_TK_Array) ? pp_type_create(PP_TK_Array) : pp_type_create(PP_TK_List);
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
                pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to create const type");
                goto error_cleanup;
            }
            temp_type->type.pp_type_const.const_of = gtt_registered_type;
            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            temp_type = NULL;
            if (!gtt_registered_type) {
                pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to intern const type");
                goto error_cleanup;
            }
        }
    }

    {
        PP_AST_ArrayListSpecifier* spec = type_node->type_expression.array_spec;
        while (spec) {
            if (spec->kind == PP_ARRAY_SPEC_EMPTY) {
                temp_type = pp_type_create(PP_TK_List);
                if (!temp_type) {
                    pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to create list type");
                    goto error_cleanup;
                }
                temp_type->type.pp_type_list.element_type = gtt_registered_type;
            } else {
                size_t size = 0;
                if (!pp_array_size_from_simple_expr(sa, spec->size_expr, &size)) {
                    pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Could not compute array size");
                    goto error_cleanup;
                }
                if (size == 0) {
                    pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Array size cannot be zero");
                    goto error_cleanup;
                }
                temp_type = pp_type_create(PP_TK_Array);
                if (!temp_type) {
                    pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to create array type");
                    goto error_cleanup;
                }
                temp_type->type.pp_type_array.size = size;
                temp_type->type.pp_type_array.element_type = gtt_registered_type;
            }
            gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
            temp_type = NULL;
            if (!gtt_registered_type) {
                pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to intern array/list type");
                goto error_cleanup;
            }
            spec = spec->next;
        }
    }

    if (type_node->type_expression.is_reference) {
        temp_type = pp_type_create(PP_TK_Reference);
        if (!temp_type) {
            pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to create reference type");
            goto error_cleanup;
        }
        temp_type->type.pp_type_reference.reference_to = gtt_registered_type;
        gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
        temp_type = NULL;
        if (!gtt_registered_type) {
            pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to intern reference type");
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
            case PP_AST_FunctionDeclarationNode:
                pp_semantic_analyzer_build_global_table_function_decl(sa, decl);
                break;
            case PP_AST_LambdaDeclarationNode:
                pp_semantic_analyzer_build_global_table_lambda_decl(sa, decl);
                break;
            case PP_AST_StructDeclarationNode:
                pp_semantic_analyzer_build_global_table_struct_decl(sa, decl);
                break;
            case PP_AST_VariableDeclarationNode:
                pp_semantic_analyzer_build_global_table_variable_decl(sa, decl);
                break;
            case PP_AST_TypedefDeclarationNode:
                pp_semantic_analyzer_build_global_table_typedef_decl(sa, decl);
                break;
            case PP_AST_NewtypeDeclarationNode:
                pp_semantic_analyzer_build_global_table_newtype_decl(sa, decl);
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


/* Pass 1 - Function Declaration Checklist:
 * [x] Detect duplicate symbol
 * [x] Build function type and intern in GTT
 * [x] Create symbol and register in global symbol table
 */
static int pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_FunctionDeclarationNode);
    PPType *tmp_func = NULL;
    const PPType *func_type = NULL;
    PPSymbol* func_symbol = NULL;

    // Duplicate check
    if (pp_symbol_table_lookup(sa->gst, node->function_declaration.name, false)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Symbol '%s' is previously defined.", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    tmp_func = pp_type_create(PP_TK_Function);
    if (!tmp_func) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate memory for function type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->function_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid return type for function '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Assign read-only return_type pointer.
    tmp_func->type.pp_type_function.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->function_declaration.param_count; i++) {
        const PPType* param_type = pp_type_from_type_node(sa, node->function_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid parameter type for function '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_func, param_type)) {
            goto error_cleanup;
        }
    }

    // Add to type table
    func_type = pp_global_type_table_intern(sa->gtt, tmp_func);
    if (!func_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to intern function type for function '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    tmp_func = NULL;

    // Create symbol
    func_symbol = pp_symbol_create(func_type, false, false, true);
    if (!func_symbol) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate symbol for function '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Register to symbol table.
    if (!pp_symbol_table_push(sa->gst, node->function_declaration.name, func_symbol)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to register function '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
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
    assert(node->node_type == PP_AST_LambdaDeclarationNode);
    PPType *tmp_lambda = NULL;
    const PPType *lambda_type = NULL;
    PPSymbol* lambda_symbol = NULL;
    
    // Duplicate symbol check
    if (pp_symbol_table_lookup(sa->gst, node->function_declaration.name, false)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                                 "Symbol '%s' previously defined.",
                                 node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    tmp_lambda = pp_type_create(PP_TK_Lambda);
    if (!tmp_lambda) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate memory for lambda type");
        goto error_cleanup;
    }

    const PPType* return_type = pp_type_from_type_node(sa, node->function_declaration.return_type);
    if (!return_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid return type for lambda");
        goto error_cleanup;
    }

    // Assign read-only return_type pointer.
    tmp_lambda->type.pp_type_lambda.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->function_declaration.param_count; i++) {
        const PPType* param_type = pp_type_from_type_node(sa, node->function_declaration.params[i]->param_declaration.type_expr);
        if (!param_type) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid parameter type for lambda '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
            goto error_cleanup;
        }
        if (!pp_type_push_param(tmp_lambda, param_type)) {
            goto error_cleanup;
        }
    }

    // Add to type table
    lambda_type = pp_global_type_table_intern(sa->gtt, tmp_lambda);
    if (!lambda_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to intern lambda type for lambda '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }
    tmp_lambda = NULL;

    // Create symbol
    lambda_symbol = pp_symbol_create(lambda_type, false, false, true);
    if (!lambda_symbol) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate symbol for lambda '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
        goto error_cleanup;
    }

    // Register to symbol table.
    if (!pp_symbol_table_push(sa->gst, node->function_declaration.name, lambda_symbol)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to register lambda '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
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
    if (pp_global_type_table_find_by_name(sa->gtt, node->struct_declaration.name) != NULL || pp_global_type_table_find_alias(sa->gtt, node->struct_declaration.name) != NULL) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                                 "Type '%s' previously defined.", node->struct_declaration.name);
        goto error_cleanup;
    }

    // Field processing deferred to Pass 2 - layout stays NULL in Pass 1 

    tmp_struct = pp_type_create(PP_TK_Struct);
    if (!tmp_struct) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate struct type for struct '%s'", node->struct_declaration.name);
        goto error_cleanup;
    }

    /* Assign */
    tmp_struct->type.pp_type_struct.layout = NULL;  /* deferred to Pass 2 */
    tmp_struct->type.pp_type_struct.name = node->struct_declaration.name;

    /* Add to type table */
    struct_type = pp_global_type_table_intern(sa->gtt, tmp_struct);
    if (!struct_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to intern struct type for struct '%s'", node->struct_declaration.name);
        goto error_cleanup;
    }
    tmp_struct = NULL;

    if (!pp_global_type_table_register_named(sa->gtt, node->struct_declaration.name, struct_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to register struct type for struct '%s'", node->struct_declaration.name);
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
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                                 "Symbol '%s' previously defined.",
                                 node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    const PPType* var_type = pp_type_from_type_node(sa, node->variable_declaration.var_decl->param_declaration.type_expr);
    if (!var_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid type for variable '%s'", node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }
    if (pp_type_is_reference(var_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Global variable '%s' cannot be a reference", node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    symbol = pp_symbol_create(var_type, false, false, true);
    if (!symbol) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate symbol for variable '%s'", node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    if (!pp_symbol_table_push(sa->gst, node->variable_declaration.var_decl->param_declaration.identifier, symbol)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to register variable '%s'", node->variable_declaration.var_decl->param_declaration.identifier);
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
 * [-] Detect cycles in typedef definition  -> Dead code (though might need to be supported if later supporting forward declaration.)
 * [x] Register alias in GTT
 */
static int pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_TypedefDeclarationNode);

    if (pp_global_type_table_find_alias(sa->gtt, node->typedef_declaration.identifier) || pp_global_type_table_find_by_name(sa->gtt, node->typedef_declaration.identifier)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Type '%s' previously defined", node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    const PPType* base_type = pp_type_from_type_node(sa, node->typedef_declaration.base_type);
    if (!base_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid base type for typedef '%s'", node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    if (pp_type_is_const(base_type) || pp_type_is_reference(base_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Base type for typedef '%s' cannot be const or reference", node->typedef_declaration.identifier);
        goto error_cleanup;
    }
    
    if (!pp_global_type_table_register_alias(sa->gtt, node->typedef_declaration.identifier, base_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to register typedef alias '%s'", node->typedef_declaration.identifier);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    return 0;
}

/* Pass 1 - Newtype Declaration Checklist:
 * [x] Ensure base type is defined
 * [x] Ensure base type is not const or reference
 * [x] Detect duplicate newtype name
 * [-] Detect cycles in newtype definition  -> See comments for pass 1 typedef
 * [x] Register named type in GTT
 * [ ] Do not allow function or lambda types as newtype.
 */
static int pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_NewtypeDeclarationNode);

    if (pp_global_type_table_find_alias(sa->gtt, node->newtype_declaration.identifier) || pp_global_type_table_find_by_name(sa->gtt, node->newtype_declaration.identifier)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Type '%s' previously defined", node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    const PPType* base_type = pp_type_from_type_node(sa, node->newtype_declaration.base_type);
    if (!base_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Invalid base type for newtype '%s'", node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    if (pp_type_is_const(base_type) || pp_type_is_reference(base_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Base type for newtype '%s' cannot be const or reference", node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    if (!pp_global_type_table_register_named(sa->gtt, node->newtype_declaration.identifier, base_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to register newtype '%s'", node->newtype_declaration.identifier);
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    return 0;
}

static ssize_t pp_find_type_index(PP_GlobalTypeTable* gtt, const PPType* type) {
    if (!gtt || !type) return -1;
    for (size_t i = 0; i < gtt->type_count; i++) {
        if (gtt->types[i] == type) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static int pp_semantic_analyzer_struct_cycle_detect(PP_SemanticAnalyzer* sa, const PPType* type, PP_StructRecursiveCycleState* states) {
    if (!type) return 1;
    if (type->kind != PP_TK_Struct) return 1;

    ssize_t idx = pp_find_type_index(sa->gtt, type);
    if (idx == -1) {
        return 1;
    }

    if (states[(size_t)idx] == PP_STRUCT_CYCLE_RESOLVING) {
        const char* name = type->type.pp_type_struct.name ? type->type.pp_type_struct.name : "<unnamed>";
        for (size_t i = 0; i < sa->program->program.decl_count; i++) {
            PP_AST_Node* decl = sa->program->program.decls[i];
            if (decl->node_type == PP_AST_StructDeclarationNode && decl->struct_declaration.name && strcmp(decl->struct_declaration.name, name) == 0) {
                pp_error_list_push_error(sa->error_list, sa->filename, decl->line, decl->col, "Struct '%s' contains recursive cycle", name);
                break;
            }
        }
        return 0;
    }

    if (states[(size_t)idx] == PP_STRUCT_CYCLE_RESOLVED) {
        return 1;
    }

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
                        if (!pp_semantic_analyzer_struct_cycle_detect(sa, t, states)) {
                            return 0;
                        }
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
                pp_error_list_push_error(sa->error_list, sa->filename, 0, 0, "Failed to allocate cycle detection states");
                goto error_cleanup;
            }
        }
        for (size_t i = 0; i < sa->program->program.decl_count; i++) {
            PP_AST_Node* decl = sa->program->program.decls[i];
            if (decl->node_type == PP_AST_StructDeclarationNode) {
                PPType* struct_type = pp_global_type_table_find_by_name_mut(sa->gtt, decl->struct_declaration.name);
                if (struct_type) {
                    ssize_t idx = pp_find_type_index(sa->gtt, struct_type);
                    if (idx != -1 && states[(size_t)idx] == PP_STRUCT_CYCLE_RESOLVED) {
                        continue;
                    }
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

    // Step 4: functions, lambdas, globals
    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        PP_AST_Node* decl = sa->program->program.decls[i];
        switch (decl->node_type) {
            case PP_AST_FunctionDeclarationNode:
                pp_semantic_analyzer_full_analyze_function_decl(sa, decl);
                break;
            case PP_AST_LambdaDeclarationNode:
                pp_semantic_analyzer_full_analyze_lambda_decl(sa, decl);
                break;
            case PP_AST_VariableDeclarationNode:
                pp_semantic_analyzer_full_analyze_global_variable_decl(sa, decl);
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
 * [ ] If return type is non-void, verify all control paths return a value (cz_ast_node_returns_on_all_paths).
 * [x] Create parameter scope (SCOPE_LEVEL_FUNC_PARAMS) with parent = global table.
 * [x] Verify parameter count in AST matches parameter count in function type.
 * [x] Detect duplicate parameter names within parameter scope.
 * [x] Create symbols for parameters with correct types and escapable reference flag (is_escapable_ref = pp_type_is_reference).
 * [x] Push parameter symbols into parameter scope.
 * [x] Create function body scope (SCOPE_LEVEL_FUNC_BODY) with parent = parameter scope.
 * [x] Check statement list in the body using full analyze.
 * [x] Wire scopes to AST nodes: param_symbol_table and body symbol table.
 * [x] Reset sa->current_function_type after processing.
 */
static int pp_semantic_analyzer_full_analyze_function_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    PPSymbolTable* func_param_symbol_table = NULL;
    PPSymbol* param_symbol = NULL;
    PPSymbolTable* func_body_symbol_table = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_FunctionDeclarationNode);

    assert(sa->current_function_type == NULL);

    // Name
    const char* func_name = node->function_declaration.name;
    assert(func_name);

    // Get Symbol
    const PPSymbol* func_symbol = pp_symbol_table_lookup(sa->gst, func_name, false);
    NULL_POINTER_ERROR_HANDLE(func_symbol);

    // Check if the type is actually function
    // Since newtype does not allow function types and lambda types, this is justified.
    if (func_symbol->type->kind != PP_TK_Function) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "\"%s\" is not a function", func_name);
        goto error_cleanup;
    }

    // Set the current function type
    sa->current_function_type = func_symbol->type;

    // Create parameter scope.
    func_param_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_PARAMS);
    NULL_POINTER_ERROR_HANDLE(func_param_symbol_table);
    func_param_symbol_table->parent = sa->gst;

    // Check number of parameters in declaration = number of parameters in symbol type.
    // TODO: This might actually be a dead code. Check this later.
    if (func_symbol->type->type.pp_type_function.param_count != node->function_declaration.param_count) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Function \"%s\" has wrong number of types.", func_name);
        goto error_cleanup;
    }

    // Fill in the parameter scope, checking for duplicate symbol.
    for (size_t i = 0; i < node->function_declaration.param_count; i++) {
        assert(node->function_declaration.params[i]->node_type == PP_AST_ParamDeclarationNode);
        const char* param_name = node->function_declaration.params[i]->param_declaration.identifier;
        if (pp_symbol_table_lookup(func_param_symbol_table, param_name, false)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->function_declaration.params[i]->line, node->function_declaration.params[i]->col,
            "Duplicate parameter \"%s\" detected", param_name);
            goto error_cleanup;
        }

        const PPType* param_type = pp_type_from_type_node(sa, node->function_declaration.params[i]->param_declaration.type_expr);
        NULL_POINTER_ERROR_HANDLE(param_type);
        
        param_symbol = pp_symbol_create(param_type, false, pp_type_is_reference(param_type), false);
        if (!pp_symbol_table_push(func_param_symbol_table, param_name, param_symbol)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->function_declaration.params[i]->line, node->function_declaration.params[i]->col,
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
    pp_semantic_analyzer_full_analyze_statement_list(sa, func_body_symbol_table, node->function_declaration.body);
    
    // Wire everything up.
    node->function_declaration.body->statement_list.symbol_table = func_body_symbol_table;
    func_body_symbol_table = NULL;
    node->function_declaration.param_symbol_table = func_param_symbol_table;
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
 * [ ] If return type is non-void, verify all control paths return a value (cz_ast_node_returns_on_all_paths).
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
    PPSymbolTable* lambda_param_symbol_table = NULL;
    PPSymbol* param_symbol = NULL;
    PPSymbolTable* lambda_body_symbol_table = NULL;
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_LambdaDeclarationNode);

    assert(sa->current_function_type == NULL);

    // Name
    const char* func_name = node->function_declaration.name;
    assert(func_name);

    // Get Symbol
    const PPSymbol* lambda_symbol = pp_symbol_table_lookup(sa->gst, func_name, false);
    NULL_POINTER_ERROR_HANDLE(lambda_symbol);

    // Check if the type is actually function
    // Since newtype does not allow function types and lambda types, this is justified.
    if (lambda_symbol->type->kind != PP_TK_Lambda) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "\"%s\" is not a lambda function", func_name);
        goto error_cleanup;
    }

    // Set the current function type
    sa->current_function_type = lambda_symbol->type;

    // Create parameter scope.
    lambda_param_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_PARAMS);
    NULL_POINTER_ERROR_HANDLE(lambda_param_symbol_table);
    lambda_param_symbol_table->parent = sa->gst;

    // Check number of parameters in declaration = number of parameters in symbol type.
    // TODO: This might actually be a dead code. Check this later.
    if (lambda_symbol->type->type.pp_type_function.param_count != node->function_declaration.param_count) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Function \"%s\" has wrong number of types.", func_name);
        goto error_cleanup;
    }

    // Fill in the parameter scope, checking for duplicate symbol.
    for (size_t i = 0; i < node->function_declaration.param_count; i++) {
        assert(node->function_declaration.params[i]->node_type == PP_AST_ParamDeclarationNode);
        const char* param_name = node->function_declaration.params[i]->param_declaration.identifier;
        if (pp_symbol_table_lookup(lambda_param_symbol_table, param_name, false)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->function_declaration.params[i]->line, node->function_declaration.params[i]->col,
            "Duplicate parameter \"%s\" detected", param_name);
            goto error_cleanup;
        }

        const PPType* param_type = pp_type_from_type_node(sa, node->function_declaration.params[i]->param_declaration.type_expr);
        NULL_POINTER_ERROR_HANDLE(param_type);
        
        param_symbol = pp_symbol_create(param_type, false, pp_type_is_reference(param_type), false);
        if (!pp_symbol_table_push(lambda_param_symbol_table, param_name, param_symbol)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->function_declaration.params[i]->line, node->function_declaration.params[i]->col,
            "Could not push parameter \"%s\" to parameter scope", param_name);
            goto error_cleanup;
        }
        param_symbol = NULL;
    }
    
    // Create body environment
    lambda_body_symbol_table = pp_symbol_table_create(SCOPE_LEVEL_FUNC_BODY);
    NULL_POINTER_ERROR_HANDLE(lambda_body_symbol_table);
    lambda_body_symbol_table->parent = lambda_param_symbol_table;

    // TODO: Handle error.
    pp_semantic_analyzer_full_analyze_statement_list(sa, lambda_body_symbol_table, node->function_declaration.body);
    
    // Wire everything up.
    node->function_declaration.body->statement_list.symbol_table = lambda_body_symbol_table;
    lambda_body_symbol_table = NULL;
    node->function_declaration.param_symbol_table = lambda_param_symbol_table;
    lambda_param_symbol_table = NULL;
    
    // Nullify current function type.
    sa->current_function_type = NULL;
    return 1;
error_cleanup:
    pp_symbol_table_free(lambda_param_symbol_table);
    pp_symbol_free(param_symbol);
    pp_symbol_table_free(lambda_body_symbol_table);
    sa->current_function_type = NULL;
    return 0;
}

/* Pass 2 - Struct Declaration Checklist (based on cz_semantic_analyzer_check_struct_fields):
 * [x] Ensure struct is defined in GTT and of correct kind STRUCT.
 * [x] Detect duplicate field names while populating layout.
 * [x] Populate struct layout with fields from AST.
 * [x] Verify each field type is well-defined in GTT.
 * [ ] For fields with initializers:
 *      - [x] Check expression validity and type resolution via check_expression.
 *      - [x] Require constexpr initializer.
 *      - [x] Ensure type match between field and initializer after decay.
 *      - [-] If field is reference: initializer must be l-value, const-correctness (non-const ref cannot bind to const).
 *             -> Reference fields cannot have default initializers at global scope per PP design; notes/global_statement_types.md
 *      - [x] Store default initializer in struct layout field.
 * [ ] Detect struct recursive cycles via separate pass (cz_semantic_analyzer_struct_cycle_detect).
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
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Could not find struct '%s' from type table from pass 1.", node->struct_declaration.name);
        goto error_cleanup;
    }

    // Create layout and push fields.
    struct_layout = pp_struct_layout_create();
    if (!struct_layout) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate struct layout for struct '%s'", node->struct_declaration.name);
        goto error_cleanup;
    }

    for (size_t i = 0; i < node->struct_declaration.field_count; i++) {
        const PP_AST_StructField* field = &node->struct_declaration.fields[i];
        assert(field->field->node_type == PP_AST_ParamDeclarationNode);

        // Duplicate field check
        for (size_t j = 0; j < i; j++) {
            if (struct_layout->fields[j].field_name == field->field->param_declaration.identifier) {
                pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                        "Struct field '%s' is duplicate", field->field->param_declaration.identifier);
                goto error_cleanup;
            }
        }

        const PPType* field_type = pp_type_from_type_node(sa, field->field->param_declaration.type_expr);
        if (!field_type) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                     "Failed to create type for struct field '%s'", field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // If initializer = NULL, it stores NULL, and if initializer exists, it stores the initializer.
        if (!pp_struct_layout_push_field(struct_layout, field->field->param_declaration.identifier, field_type, field->initializer)) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->line, field->field->col,
                                     "Failed to add struct field '%s'", field->field->param_declaration.identifier);
            goto error_cleanup;
        }
    }

    // Link the struct layout to the type.
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
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Could not find struct '%s' from type table.", struct_name);
        goto error_cleanup;
    }

    const PP_StructLayout* layout = struct_type->type.pp_type_struct.layout;
    if (!layout) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Struct '%s' layout not initialized.", struct_name);
        goto error_cleanup;
    }

    for (size_t i = 0; i < node->struct_declaration.field_count; i++) {
        const PP_AST_StructField* field = &node->struct_declaration.fields[i];
        if (!field->initializer) {
            continue;
        }

        // Expression check
        if (!pp_semantic_analyzer_check_expression(sa, sa->gst, field->initializer)) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                    "Initializer for struct field '%s' is invalid", field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        const PP_AST_Decoration* initializer_decoration = pp_ast_get_decoration(field->initializer);
        assert(initializer_decoration);

        // Require constexpr
        if (!initializer_decoration->is_constexpr) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                    "Global scope initializer for struct field '%s' should be constexpr", field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // Use layout field type
        const PPType* field_type = layout->fields[i].field_type;
        if (pp_type_is_reference(field_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                    "Struct field '%s' is a reference and cannot have a default initializer at global scope", field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        // Decay/type equality check
        const PPType* decayed_field_type = pp_type_decay(sa->gtt, field_type);
        const PPType* decayed_initializer_type = pp_type_decay(sa->gtt, initializer_decoration->resolved_type);
        if (!pp_type_equal(decayed_field_type, decayed_initializer_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                    "Initializer for struct field '%s' does not match the field type", field->field->param_declaration.identifier);
            goto error_cleanup;
        }
    }

    return 1;
error_cleanup:
    return 0;
}

/* Pass 2 - Variable Declaration Checklist (based on cz_semantic_analyzer_check_global_var_init):
 * [x] Look up variable symbol mutably from global table.
 * [x] Global variables cannot be references – enforced in Pass 1 and re-checked here. Reference handling from CZ is N/A in PP.
 * [x] If struct type, ensure it does not contain reference fields (global rule).
 * [x] If no initializer:
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

    PPSymbol* variable_symbol = pp_symbol_table_lookup_mut(sa->gst, node->variable_declaration.var_decl->param_declaration.identifier, false);
    NULL_POINTER_ERROR_HANDLE(variable_symbol);

    // If struct type, it should not contain any reference fields (as this is a global variable.)
    if (variable_symbol->type->kind == PP_TK_Struct) {
        const PP_StructLayout* struct_layout = variable_symbol->type->type.pp_type_struct.layout;
        if (!struct_layout) {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "Struct \"%s\" could not be previously analyzed.",
                node->variable_declaration.var_decl->param_declaration.identifier);
                goto error_cleanup;
        }
        for (size_t i = 0; i < struct_layout->field_count; i++) {
            if (pp_type_is_reference(struct_layout->fields[i].field_type)) {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "Global variable \"%s\" is a struct, but global variable cannot contain any references.",
                node->variable_declaration.var_decl->param_declaration.identifier);
                goto error_cleanup;
            }
        }
    }

    // Global variable cannot be reference.
    if (pp_type_is_reference(variable_symbol->type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Global variable \"%s\" declared as reference, which is not allowed.", node->variable_declaration.var_decl->param_declaration.identifier);
        goto error_cleanup;
    }

    // Check for initializer.
    if (!node->variable_declaration.initializer) {
        if (pp_type_is_const(variable_symbol->type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Variable \"%s\" declared as const must have initializer.", node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }
    } else {
        // Initializer exists here.
        if (!pp_semantic_analyzer_check_expression(sa, sa->gst, node->variable_declaration.initializer)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Variable \"%s\" has an invalid initializer.", node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }

        // Require constexpr
        const PP_AST_Decoration* decor = pp_ast_get_decoration(node->variable_declaration.initializer);
        NULL_POINTER_ERROR_HANDLE(decor);
        if (!decor->is_constexpr) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Global variable \"%s\" has a non constexpr initializer.", node->variable_declaration.var_decl->param_declaration.identifier);
            goto error_cleanup;
        }

        // Check if types match
        const PPType* decayed_lhs_type = pp_type_decay(sa->gtt, variable_symbol->type);
        const PPType* decayed_rhs_type = pp_type_decay(sa->gtt, decor->resolved_type);
        NULL_POINTER_ERROR_HANDLE(decayed_lhs_type);
        NULL_POINTER_ERROR_HANDLE(decayed_rhs_type);
        if (!pp_type_equal(decayed_lhs_type, decayed_rhs_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Type mismatch during variable declaration.");
            goto error_cleanup;
        }

        // For const variable, track constexpr.
        if (pp_type_is_const(variable_symbol->type)) {
            variable_symbol->is_constexpr = true;
        }

        // Global variable is always escapable.
        variable_symbol->is_escapable_ref = true;
    }

    return 1;

error_cleanup:
    return 0;
}

static int pp_semantic_analyzer_full_analyze_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
    PPSymbolTable* inner_symbol_table = NULL;
    assert(sa);
    assert(symbol_table);
    assert(node);

    switch (node->node_type) {
        case PP_AST_VariableDeclarationNode:
            if (!pp_semantic_analyzer_full_analyze_variable_declaration(sa, symbol_table, node)) {
                goto error_cleanup;
            }
            break;
        case PP_AST_AssignmentNode:
            /* Assignment Statement Checklist (based on cz_semantic_analyzer_check_assignment_statement) */
            if (!pp_semantic_analyzer_full_analyze_assignment_statement(sa, symbol_table, node)) {
                goto error_cleanup;
            }
            break;
        case PP_AST_ReturnStatementNode:
            /* Return Statement Checklist (based on cz_semantic_analyzer_check_return_statement) */
            if (!pp_semantic_analyzer_full_analyze_return_statement(sa, symbol_table, node)) {
                goto error_cleanup;
            }
            break;
        case PP_AST_IfStatementNode:
            /* If Statement Checklist (based on cz_semantic_analyzer_check_if_statement) */
            if (!pp_semantic_analyzer_full_analyze_if_statement(sa, symbol_table, node)) {
                goto error_cleanup;
            }
            break;
        case PP_AST_ForStatementNode:
            /* For Statement Checklist (based on cz_semantic_analyzer_check_for_statement) */
            if (!pp_semantic_analyzer_full_analyze_for_statement(sa, symbol_table, node)) {
                goto error_cleanup;
            }
            break;
        case PP_AST_WhileStatementNode:
            /* While Statement Checklist (based on cz_semantic_analyzer_check_while_statement) */
            if (!pp_semantic_analyzer_full_analyze_while_statement(sa, symbol_table, node)) {
                goto error_cleanup;
            }
            break;
        case PP_AST_StatementListNode:
            inner_symbol_table = pp_symbol_table_create(symbol_table->scope_level + 1);
            NULL_POINTER_ERROR_HANDLE(inner_symbol_table);

            inner_symbol_table->parent = symbol_table;
            if (!pp_semantic_analyzer_full_analyze_statement_list(sa, inner_symbol_table, node)) {
                goto error_cleanup;
            }
            
            node->statement_list.symbol_table = inner_symbol_table;
            inner_symbol_table = NULL;
            break;
        default:
            break;
    }

error_cleanup:
    pp_symbol_table_free(inner_symbol_table);
    return 0;
}

/* Variable Declaration Statement Checklist (based on cz_semantic_analyzer_check_variable_declaration_statement):
 * [x] Lookup existing symbol; prevent shadowing function parameters and redeclaration in same scope. -> Shadowing banned for all parent scopes.
 * [x] Resolve type from type node.
 * [x] If type is reference or const, initializer must exist.
 * [ ] If type contains reference (direct or via struct field), initializer must exist; no zero-init / default.
 * [ ] If struct type contains reference fields:
 *     [ ] Each reference field must be explicitly initialized from l-value with const-correctness
 *     [ ] Reference fields cannot use default initializers
 * [x] If initializer exists: check expression validity.
 * [x] If type is reference: initializer must be l-value; const-correctness check.
 * [ ] If struct type without initializer: ensure all const/reference fields have default initializers. -> Not allowed for reference fields in PP; see above.
 * [x] Ensure decayed types match between variable and initializer.
 * [x] Create symbol with type, constexpr flag, escapable reference flag, and push to symbol table.
 * Note: Types containing references are allowed locally but must not escape. For prototype:
 *       - [ ] ban returning by value a type that contains a reference
 *       - [ ] ban whole-struct assignment for types containing a reference
 *       - [ ] consider banning lambda/function parameters whose type contains a reference or enforce const-propagation through reference fields.
 */
static int pp_semantic_analyzer_full_analyze_variable_declaration(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
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
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Shadowing \"%s\" is not allowed.", symbol_name);
            goto error_cleanup;
        }
    }

    // Deduce type
    const PPType* symbol_type = pp_type_from_type_node(sa, node->variable_declaration.var_decl->param_declaration.type_expr);
    if (!symbol_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Deducing type for \"%s\" failed", symbol_name);
        goto error_cleanup;
    }

    // If reference or const, initializer must exist.
    if (pp_type_is_const(symbol_type) || pp_type_is_reference(symbol_type)) {
        if (!node->variable_declaration.initializer) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Variable \"%s\" is declared as const or reference, but initializer does not exist.", symbol_name);
            goto error_cleanup;
        }
    }

    // If initializer exists, check validity.
    if (node->variable_declaration.initializer) {
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->variable_declaration.initializer)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Initializer for variable \"%s\" is not valid", symbol_name);
            goto error_cleanup;
        }

        const PP_AST_Decoration* initializer_decor = pp_ast_get_decoration(node->variable_declaration.initializer);
        assert(initializer_decor);

        // If reference variable
        if (pp_type_is_reference(symbol_type)) {
            // l-value
            if (initializer_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "Variable \"%s\" is a reference, but initializer is not an l-value", symbol_name);
                goto error_cleanup;
            }
            // const-correctness
            if (!pp_type_is_const(symbol_type) && pp_type_is_const(initializer_decor->resolved_type)) {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "Variable \"%s\" is a mutable reference, but initializer is const. Cannot remove constness", symbol_name);
                goto error_cleanup;
            }
        }

        // Decayed type match
        const PPType* decayed_symbol_type = pp_type_decay(sa->gtt, symbol_type);
        const PPType* decayed_initializer_type = pp_type_decay(sa->gtt, initializer_decor->resolved_type);
        assert(decayed_symbol_type);
        assert(decayed_initializer_type);
        if (!pp_type_equal(decayed_symbol_type, decayed_initializer_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Type of variable \"%s\" does not match its initializer", symbol_name);
            goto error_cleanup;
        }

        // Create symbol
        // is_constexpr: variable has to be constant, and initializer should also be constexpr (so the value that variable has is determined at compile time)
        // is_escapable_ref: Note that this reference can refer to variable from outside, then return it back. For this, it must be reference, and initializer must be escapable.
        variable_symbol = pp_symbol_create(symbol_type,
                                           pp_type_is_const(symbol_type) && initializer_decor->is_constexpr,
                                           pp_type_is_reference(symbol_type) && initializer_decor->is_escapable_ref,
                                           false);
    } else {
        variable_symbol = pp_symbol_create(symbol_type, false, false, false);
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
static int pp_semantic_analyzer_full_analyze_return_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_ReturnStatementNode);

    assert(sa->current_function_type);
    assert(sa->current_function_type->kind == PP_TK_Function || sa->current_function_type->kind == PP_TK_Lambda);

    // If return type is void, expression must be NULL.
    // If return type is not void, expression must exist.
    if (sa->current_function_type->type.pp_type_function.return_type->kind == PP_TK_Void && node->return_statement.expr) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Return type is void, but an expression is returned.");
        goto error_cleanup;
    }
    if (sa->current_function_type->type.pp_type_function.return_type->kind != PP_TK_Void && !node->return_statement.expr) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Expected a return expression.");
        goto error_cleanup;
    }

    // If void return, end early.
    if (sa->current_function_type->type.pp_type_function.return_type->kind == PP_TK_Void) {
        return 1;
    }

    // Check expression
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->return_statement.expr)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Invalid expression");
        goto error_cleanup;
    }
    const PP_AST_Decoration* expr_decor = pp_ast_get_decoration(node->return_statement.expr);
    assert(expr_decor);

    // Check type equality.
    const PPType* decayed_return_type = pp_type_decay(sa->gtt, sa->current_function_type->type.pp_type_function.return_type);
    const PPType* decayed_expr_type = pp_type_decay(sa->gtt, expr_decor->resolved_type);
    if (!pp_type_equal(decayed_return_type, decayed_expr_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Return type and expression type in the return statement do not match.");
        goto error_cleanup;
    }
    
    // Return type reference handling.
    if (pp_type_is_reference(sa->current_function_type->type.pp_type_function.return_type)) {
        // L-value
        if (expr_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Return type is reference, but the return expression is not an l-value.");
            goto error_cleanup;
        }

        // Dangling reference check
        if (!expr_decor->is_escapable_ref) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Cannot return a dangling reference");
            goto error_cleanup;
        }

        // Const-dropping detection
        if (!pp_type_is_const(sa->current_function_type->type.pp_type_function.return_type) &&
            pp_type_is_const(expr_decor->resolved_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
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
 * [ ] Validate compound assignment operators against allowed primitive combinations.
 */
static int pp_semantic_analyzer_full_analyze_assignment_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_AssignmentNode);
    assert(pp_token_type_is_assignment(node->assignment.op));

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->assignment.lhs)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "LHS of assignment is an invalid expression.");
        goto error_cleanup;
    }
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->assignment.rhs)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "RHS of assignment is an invalid expression.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* lhs_decor = pp_ast_get_decoration(node->assignment.lhs);
    const PP_AST_Decoration* rhs_decor = pp_ast_get_decoration(node->assignment.rhs);
    assert(lhs_decor);
    assert(rhs_decor);

    // LHS must be l-value
    if (lhs_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "LHS of assignment should be an l-value.");
        goto error_cleanup;
    }
    // LHS must be mutable.
    if (pp_type_is_const(lhs_decor->resolved_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "LHS of assignment should be mutable (non-const)");
        goto error_cleanup;
    }

    const PPType* decayed_lhs_type = pp_type_decay(sa->gtt, lhs_decor->resolved_type);
    const PPType* decayed_rhs_type = pp_type_decay(sa->gtt, rhs_decor->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_lhs_type);
    NULL_POINTER_ERROR_HANDLE(decayed_rhs_type);
    if (!pp_type_equal(decayed_lhs_type, decayed_rhs_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "LHS and RHS of assignment must have the same type.");
        goto error_cleanup;
    }

    // Validate compound assignment operators against allowed primitive combinations
    switch (node->assignment.op) {
        case PP_TT_EQUAL:
            // Simple assignment already validated by type equality
            break;
        case PP_TT_PLUS_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "+= only defined for int32, uint32, or float.");
                goto error_cleanup;
            }
            break;
        case PP_TT_MINUS_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "-= only defined for int32, uint32, or float.");
                goto error_cleanup;
            }
            break;
        case PP_TT_STAR_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "*= only defined for int32, uint32, or float.");
                goto error_cleanup;
            }
            break;
        case PP_TT_SLASH_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32) ||
                (decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "/= only defined for int32, uint32, or float.");
                goto error_cleanup;
            }
            break;
        case PP_TT_PERCENT_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "%= only defined for int32 or uint32.");
                goto error_cleanup;
            }
            break;
        case PP_TT_AMPERSAND_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "&= only defined for int32 or uint32.");
                goto error_cleanup;
            }
            break;
        case PP_TT_BAR_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "|= only defined for int32 or uint32.");
                goto error_cleanup;
            }
            break;
        case PP_TT_CARET_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)) {
                // Valid
            } else {
                pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
                "^= only defined for int32 or uint32.");
                goto error_cleanup;
            }
            break;
        default:
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Unknown assignment operator.");
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
static int pp_semantic_analyzer_full_analyze_if_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_IfStatementNode);

    // Check condition
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->if_statement.cond)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Condition for if statement could not be analyzed.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* cond_decor = pp_ast_get_decoration(node->if_statement.cond);
    assert(cond_decor);

    // Check if condition is actually boolean
    const PPType* decayed_cond_type = pp_type_decay(sa->gtt, cond_decor->resolved_type);
    assert(decayed_cond_type);
    if (decayed_cond_type->kind != PP_TK_Bool) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Condition for if statement must be boolean");
        goto error_cleanup;
    }

    // Then branch
    if (!pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, node->if_statement.then_branch)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Then branch for if statement is invalid");
        goto error_cleanup;
    }

    // Else branch
    if (node->if_statement.else_branch) {
        if (!pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, node->if_statement.else_branch)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Else branch for if statement is invalid");
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
static int pp_semantic_analyzer_full_analyze_for_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
    PPSymbolTable* for_init_symbol_table = NULL;
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_ForStatementNode);

    // For init symbol table: A single symbol table bring between symbol_table and for body symbol_table, where it may or may not hold the initialization variable.
    for_init_symbol_table = pp_symbol_table_create(symbol_table->scope_level+1);
    NULL_POINTER_ERROR_HANDLE(for_init_symbol_table);
    for_init_symbol_table->parent = symbol_table;


    // Initialization
    if (node->for_statement.init) {
        if (node->for_statement.init->node_type == PP_AST_VariableDeclarationNode) {
            // Variable declaration
            if (!pp_semantic_analyzer_full_analyze_variable_declaration(sa, for_init_symbol_table, node->for_statement.init)) {
                pp_error_list_push_error(sa->error_list, sa->filename, node->for_statement.init->line, node->for_statement.init->col,
                "Invalid for statement initialization (nothing, variable declaration, or assignments only)");
                goto error_cleanup;
            }
        } else if (node->for_statement.init->node_type == PP_AST_AssignmentNode) {
            // Assignment
            if (!pp_semantic_analyzer_full_analyze_assignment_statement(sa, for_init_symbol_table, node->for_statement.init)) {
                pp_error_list_push_error(sa->error_list, sa->filename, node->for_statement.init->line, node->for_statement.init->col,
                "Invalid for statement initialization (nothing, variable declaration, or assignments only)");
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
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Condition for for statement could not be analyzed.");
            goto error_cleanup;
        }
        const PP_AST_Decoration* cond_decor = pp_ast_get_decoration(node->for_statement.cond);
        assert(cond_decor);

        // Check if condition is actually boolean
        const PPType* decayed_cond_type = pp_type_decay(sa->gtt, cond_decor->resolved_type);
        assert(decayed_cond_type);
        if (decayed_cond_type->kind != PP_TK_Bool) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Condition for for statement must be boolean");
            goto error_cleanup;
        }
    }

    // Update
    if (node->for_statement.update) {
        if (!pp_semantic_analyzer_full_analyze_statement(sa, for_init_symbol_table, node->for_statement.update)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Update for for statement is invalid");
            goto error_cleanup;
        }
    }

    // Body
    assert(node->for_statement.body);
    assert(node->for_statement.body->node_type == PP_AST_StatementListNode);
    if (!pp_semantic_analyzer_full_analyze_statement(sa, for_init_symbol_table, node->for_statement.body)) {
            pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
            "Body for for statement is invalid");
            goto error_cleanup;
    }

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
static int pp_semantic_analyzer_full_analyze_while_statement(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
    assert(sa);
    assert(symbol_table);
    assert(node);
    assert(node->node_type == PP_AST_WhileStatementNode);

    // Check condition
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, node->while_statement.cond)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Condition for while statement could not be analyzed.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* cond_decor = pp_ast_get_decoration(node->while_statement.cond);
    assert(cond_decor);

    // Check if condition is actually boolean
    const PPType* decayed_cond_type = pp_type_decay(sa->gtt, cond_decor->resolved_type);
    assert(decayed_cond_type);
    if (decayed_cond_type->kind != PP_TK_Bool) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Condition for whilte statement must be boolean");
        goto error_cleanup;
    }

    // Then branch
    if (!pp_semantic_analyzer_full_analyze_statement(sa, symbol_table, node->while_statement.body)) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col,
        "Body of while statement is invalid");
        goto error_cleanup;
    }

    return 1;
error_cleanup:
    return 0;
}

static int pp_semantic_analyzer_full_analyze_statement_list(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* node) {
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
 * [x] Dispatch to cast expression checker PP_AST_CastExpressionNode -> pp_semantic_analyzer_check_cast_expression
 * [ ] Dispatch to function call expression checker CZ_AST_FunctionCallNodeType -> cz_semantic_analyzer_check_function_call_expression
 * [ ] Dispatch to struct member access checker CZ_AST_StructMemberAccessNodeType -> cz_semantic_analyzer_check_struct_access
 * [ ] Dispatch to struct init checker CZ_AST_StructInitNodeType -> cz_semantic_analyzer_check_struct_init
 * [ ] Default error for unsupported node types with cz_error_list_push_error
 */
static int pp_semantic_analyzer_check_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
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
        case PP_AST_IdentifierNode:
            if (!pp_semantic_analyzer_check_identifier_expression(sa, symbol_table, expr)) goto error_cleanup;
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
        case PP_AST_CollectionInitializerNode:
            if (!pp_semantic_analyzer_check_collection_initializer_expression(sa, symbol_table, expr)) goto error_cleanup;
            break;
        default:
            goto error_cleanup;
    }

    return 1;

error_cleanup:
    return 0;
}

/* Function Call Expression Checklist (based on cz_semantic_analyzer_check_function_call_expression):
 * [x] Early exit if decoration already set
 * [x] Check callee expression validity
 * [x] Verify callee resolved type is function
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
static int pp_semantic_analyzer_check_function_call(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_FunctionCallNode);

    if (expr->function_call.decoration) return 1;

    // Check callee (to support expressions like (f(2))(3), that is, higher-order functions)
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->function_call.callee)) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Function callee could not be validated.");
        goto error_cleanup;
    }
    const PP_AST_Decoration* callee_decoration = pp_ast_get_decoration(expr->function_call.callee);
    assert(callee_decoration);

    // Check indeed that it is a function.
    if (callee_decoration->resolved_type->kind != PP_TK_Function && callee_decoration->resolved_type->kind != PP_TK_Lambda) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Callee is not a registered function.");
        goto error_cleanup;
    }

    // Keep track of whether or not the function is lambda.
    bool function_is_lambda = callee_decoration->resolved_type->kind == PP_TK_Lambda;

    // Param count validation
    if (function_is_lambda) {
        if (callee_decoration->resolved_type->type.pp_type_lambda.param_count != expr->function_call.arg_count) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Lambda function expects %d arguments but only received %d arguments",
            callee_decoration->resolved_type->type.pp_type_lambda.param_count,
            expr->function_call.arg_count);
            goto error_cleanup;
        }
    } else {
        if (callee_decoration->resolved_type->type.pp_type_function.param_count != expr->function_call.arg_count) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Function expects %d arguments but only received %d arguments",
            callee_decoration->resolved_type->type.pp_type_function.param_count,
            expr->function_call.arg_count);
            goto error_cleanup;
        }
    }

    size_t param_count = function_is_lambda ?
        callee_decoration->resolved_type->type.pp_type_lambda.param_count :
        callee_decoration->resolved_type->type.pp_type_function.param_count;

    bool is_escapable = true;
    // Argument type checking loop
    for (size_t i = 0; i < param_count; i++) {
        // Check argument.
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->function_call.arguments[i])) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Function argument could not be deduced");
            goto error_cleanup;
        }

        // Type decay check
        const PP_AST_Decoration* arg_decor = pp_ast_get_decoration(expr->function_call.arguments[i]);
        assert(arg_decor);
        const PPType* arg_type = arg_decor->resolved_type;
        const PPType* param_type = function_is_lambda ?
            callee_decoration->resolved_type->type.pp_type_lambda.param_types[i] :
            callee_decoration->resolved_type->type.pp_type_function.param_types[i];
        assert(arg_type);
        assert(param_type);
        const PPType* decayed_arg_type = pp_type_decay(sa->gtt, arg_type);
        const PPType* decayed_param_type = pp_type_decay(sa->gtt, param_type);
        if (!pp_type_equal(decayed_arg_type, decayed_param_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->function_call.arguments[i]->line, expr->function_call.arguments[i]->col,
            "Type mismatch between parameter and passed argument.");
            goto error_cleanup;
        }

        if (pp_type_is_reference(param_type)) {
            // If parameter is reference, passed argument must be l-value
            if (arg_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, sa->filename, expr->function_call.arguments[i]->line, expr->function_call.arguments[i]->col,
                "Parameter is reference, so l-value is expected.");
                goto error_cleanup;
            }
            // Lambda does not allow mutable references.
            if (function_is_lambda && !pp_type_is_const(param_type)) {
                pp_error_list_push_error(sa->error_list, sa->filename, expr->function_call.arguments[i]->line, expr->function_call.arguments[i]->col,
                "Lambda function requires all reference parameters to be immutable.");
                goto error_cleanup;
            }

            // Constness check
            if (pp_type_is_const(arg_type) && !pp_type_is_const(param_type)) {
                pp_error_list_push_error(sa->error_list, sa->filename, expr->function_call.arguments[i]->line, expr->function_call.arguments[i]->col,
                "Parameter is mutable reference, but const l-value is passed.");
                goto error_cleanup;
            }
            // Escapability Check
            if (!arg_decor->is_escapable_ref) {
                is_escapable = false;
            }
        }

    }

    const PPType* return_type = function_is_lambda ?
        callee_decoration->resolved_type->type.pp_type_lambda.return_type :
        callee_decoration->resolved_type->type.pp_type_function.return_type;
    assert(return_type);
    if (function_is_lambda) {
        // Void cannot be returned in lambda function
        if (return_type->kind == PP_TK_Void) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Lambda function forbids returning void");
            goto error_cleanup;
        }

        // TODO: For now lambda does not allow returning references.
        // This condition will be lifted later in v2
        if (pp_type_is_reference(return_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Lambda function (for now) forbids returning references.");
            goto error_cleanup;
        }
    }
    
    decoration = pp_ast_decoration_create(return_type,
        pp_type_is_reference(return_type) ? PP_VALUE_CATEGORY_LVALUE : PP_VALUE_CATEGORY_RVALUE,
        false,
        pp_type_is_reference(return_type) && is_escapable
    );
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
 * [ ] Explicitly verify both operands are primitive after decay with an error push
 * [ ] Propagate maximum scope level of operands into decoration, CZ uses MAX(lhs_scope,rhs_scope)
 */
static int pp_semantic_analyzer_check_binary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* binary_type = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_BinaryExpressionNode);

    if (expr->binary_expression.decoration) return 1;

    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->binary_expression.left)) {
        goto error_cleanup;
    }
    if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->binary_expression.right)) {
        goto error_cleanup;
    }

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
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Decaying type failure");
        goto error_cleanup;
    }
    const PPType* decayed_rhs_type = pp_type_decay(sa->gtt, rhs_type);
    if (!decayed_rhs_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Decaying type failure");
        goto error_cleanup;
    }

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
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(PP_TK_Bool);
            }
            break;
        case PP_TT_LESS:
        case PP_TT_GREATER:
        case PP_TT_LESS_EQUAL:
        case PP_TT_GREATER_EQUAL:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(PP_TK_Bool);
            }
            break;
        case PP_TT_BAR:
        case PP_TT_CARET:
        case PP_TT_AMPERSAND:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_PLUS:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_MINUS:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_STAR:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_SLASH:
            if ((decayed_lhs_type->kind == PP_TK_Float && decayed_rhs_type->kind == PP_TK_Float) ||
                (decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        case PP_TT_PERCENT:
            if ((decayed_lhs_type->kind == PP_TK_Int32 && decayed_rhs_type->kind == PP_TK_Int32) ||
                (decayed_lhs_type->kind == PP_TK_UInt32 && decayed_rhs_type->kind == PP_TK_UInt32)
            ) {
                binary_type = pp_type_create(decayed_lhs_type->kind);
            }
            break;
        default:
            abort();
    }
    NULL_POINTER_ERROR_HANDLE(binary_type);

    decoration = pp_ast_decoration_create(binary_type, PP_VALUE_CATEGORY_RVALUE, lhs_decoration->is_constexpr && rhs_decoration->is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);
    binary_type = NULL;

    expr->binary_expression.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    pp_type_free(binary_type);
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
 * [x] Create decoration with result_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr propagated from operand, escapable_ref false
 * [ ] Explicitly verify operand is primitive after decay with error push
 * [ ] Preserve operand scope level in decoration, CZ passes operand->decoration->scope_level
 */
static int pp_semantic_analyzer_check_unary_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    PPType* unary_type = NULL;
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
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Failure decaying type");
        goto error_cleanup;
    }

    switch (expr->unary_expression.op) {
        case PP_TT_MINUS:
            switch (decayed_operand_type->kind) {
                case PP_TK_Int32:
                    unary_type = pp_type_create(PP_TK_Int32);
                    break;
                case PP_TK_UInt32:
                    unary_type = pp_type_create(PP_TK_UInt32);
                    break;
                case PP_TK_Float:
                    unary_type = pp_type_create(PP_TK_Float);
                    break;
                default:
                    abort();
            }
            break;
        case PP_TT_EXCLAMATION:
            if (decayed_operand_type->kind == PP_TK_Bool) {
                unary_type = pp_type_create(PP_TK_Bool);
            } else {
                abort();
            }
            break;
        default:
            abort();
    }

    NULL_POINTER_ERROR_HANDLE(unary_type);

    decoration = pp_ast_decoration_create(unary_type, PP_VALUE_CATEGORY_RVALUE, operand_decoration->is_constexpr, false);
    NULL_POINTER_ERROR_HANDLE(decoration);
    unary_type = NULL;

    expr->unary_expression.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    pp_type_free(unary_type);
    return 0;
}

/* pp_semantic_analyzer_check_literal_expression Checklist (based on cz_semantic_analyzer_check_literal_expression):
 * [x] Validate inputs via asserts
 * [x] Early exit if expr->literal.decoration already set
 * [x] Switch on literal.literal_type and resolve base type
 *    - PP_TT_INTEGER_LITERAL -> int32 alias
 *    - PP_TT_TRUE / PP_TT_FALSE -> bool
 *    - PP_TT_FLOAT_LITERAL -> float
 * [ ] Distinguish unsigned suffix for numeric literals, CZ parses lexeme for '.' and 'u' to choose int32/uint32/float
 * [x] Create const-wrapped type via pp_type_create(PP_TK_Const) then intern
 * [x] Create decoration with resolved type, PP_VALUE_CATEGORY_RVALUE, is_constexpr true, escapable_ref false
 * [ ] Explicitly reject string literal with error push, CZ pushes "String literal not yet supported."
 * [ ] Set scope level 0 explicitly in decoration as CZ does
 */
static int pp_semantic_analyzer_check_literal_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
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
        case PP_TT_INTEGER_LITERAL:
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "int32");
            NULL_POINTER_ERROR_HANDLE(resolved_type);
            break;
        case PP_TT_TRUE:
        case PP_TT_FALSE:
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "bool");
            NULL_POINTER_ERROR_HANDLE(resolved_type);
            break;
        case PP_TT_FLOAT_LITERAL:
            resolved_type = pp_global_type_table_find_alias(sa->gtt, "float");
            NULL_POINTER_ERROR_HANDLE(resolved_type);
            break;
        default:
            goto error_cleanup;
    }

    // Add const type modifier
    literal_type = pp_type_create(PP_TK_Const);
    NULL_POINTER_ERROR_HANDLE(literal_type);
    literal_type->type.pp_type_const.const_of = resolved_type;

    resolved_type = pp_global_type_table_intern(sa->gtt, literal_type);
    literal_type = NULL;

    decoration = pp_ast_decoration_create(resolved_type, PP_VALUE_CATEGORY_RVALUE, true, false);
    NULL_POINTER_ERROR_HANDLE(decoration);

    expr->literal.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    pp_type_free(literal_type);
    return 0;
}

/* pp_semantic_analyzer_check_identifier_expression Checklist (based on cz_semantic_analyzer_check_identifier_expression):
 * [x] Validate inputs via asserts
 * [x] Early exit if expr->identifier.decoration already set
 * [x] Lookup identifier in symbol table via pp_symbol_table_lookup
 * [x] Retrieve identifier type from symbol
 * [x] Determine value category: RVALUE if Function/Lambda else LVALUE
 * [x] Create decoration with symbol type, value_category, is_constexpr false, escapable_ref = symbol->is_escapable_ref
 * [x] Verify symbol exists and push error if not found
 * [-] Propagate symbol scope level into decoration, CZ uses symbol->scope_level -> PP does not track scope level per symbol
 * [x] Use symbol->is_escapable_ref when creating decoration
 * [x] Lambda capture rules: ban reference to global, require const for globals used inside lambda
 */
static int pp_semantic_analyzer_check_identifier_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_IdentifierNode);

    // If already decorated, we are done.
    if (expr->identifier.decoration) return 1;

    bool function_is_lambda = false;
    if (sa->current_function_type) {
        function_is_lambda = sa->current_function_type->kind == PP_TK_Lambda;
    }

    const PPSymbol* identifier_symbol = pp_symbol_table_lookup(symbol_table, expr->identifier.identifier, true);
    if (!identifier_symbol) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "\"%s\" is not previously defined.", expr->identifier.identifier);
        goto error_cleanup;
    }

    if (function_is_lambda && identifier_symbol->is_global) {
        if (pp_type_is_reference(identifier_symbol->type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "\"%s\" is a reference to a symbol declared outside of lambda", expr->identifier.identifier);
            goto error_cleanup;
        }
        if (!pp_type_is_const(identifier_symbol->type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "\"%s\" is a mutable symbol declared outside of lambda", expr->identifier.identifier);
            goto error_cleanup;
        }
    }

    const PPType* identifier_type = identifier_symbol->type;
    assert(identifier_type);

    decoration = pp_ast_decoration_create(identifier_type,
                                          identifier_type->kind == PP_TK_Function || identifier_type->kind == PP_TK_Lambda ? PP_VALUE_CATEGORY_RVALUE : PP_VALUE_CATEGORY_LVALUE,
                                          false, identifier_symbol->is_escapable_ref);
    NULL_POINTER_ERROR_HANDLE(decoration);
    
    expr->identifier.decoration = decoration;
    decoration = NULL;

    return 1;

error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;
}

static int pp_semantic_analyzer_check_member_access_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
    PP_AST_Decoration* decoration = NULL;
    assert(sa);
    assert(symbol_table);
    assert(expr);
    assert(expr->node_type == PP_AST_MemberAccessNode);



error_cleanup:
    pp_ast_decoration_free(decoration);
    return 0;

}

/* pp_semantic_analyzer_check_struct_initializer_expression Checklist (based on cz_semantic_analyzer_check_struct_init):
 * [x] Validate inputs via asserts / NULL_POINTER_ERROR_HANDLE and node type check
 * [x] Early exit if expr already has decoration
 * [x] Look up struct type by name in GTT, verify exists and kind is PP_TK_Struct
 * [x] Check member_count <= field_count, error if too many initializers
 * [x] For each field that is const or reference and has no default initializer, ensure it is present in the initializer list
 * [x] For each initializer member:
 *     [x] Check for duplicate member names within the initializer
 *     [x] Validate member name exists in struct layout
 *     [x] Recursively check member expression via pp_semantic_analyzer_check_expression
 *     [x] Retrieve member decoration, ensure not NULL
 *     [x] Decay both field type and initializer type and compare for equality
 *     [x] If field type is reference:
 *         [x] Initializer must be l-value
 *         [x] Const-correctness: non-const reference cannot bind to const value
 *     [x] Track constexpr flag: initializer_is_constexpr = AND of all member is_constexpr
 * [x] Create PP_AST_Decoration for the struct init with resolved_type = struct type, PP_VALUE_CATEGORY_RVALUE, is_constexpr as aggregated, escapable_ref false
 * [x] Attach decoration to expr and return success
 */
static int pp_semantic_analyzer_check_struct_initializer_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
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
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Unknown type '%s'", struct_name);
        goto error_cleanup;
    }
    // Check if struct type.
    if (struct_type->kind != PP_TK_Struct) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Expected type '%s' to be a struct", struct_name);
        goto error_cleanup;
    }

    // Initializer count check
    if (expr->struct_initializer.entry_count > struct_type->type.pp_type_struct.layout->field_count) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Too many entries in struct initializer for type '%s'", struct_name);
        goto error_cleanup;
    }

    // Check if each const/reference field has either default initializer or given initializer
    for (size_t i = 0; i < struct_type->type.pp_type_struct.layout->field_count; i++) {
        const PPStructField* field = &struct_type->type.pp_type_struct.layout->fields[i];
        const PPType* field_type = field->field_type;
        if ((pp_type_is_const(field_type) || pp_type_is_reference(field_type)) && !field->default_initializer) {
            bool found = false;
            for (size_t j = 0; j < expr->struct_initializer.entry_count; j++) {
                if (field->field_name == expr->struct_initializer.entries[j].member_identifier->identifier.identifier) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
                "No initializer given for const or reference field \"%s\"", field->field_name);
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
                pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
                "Duplicate field name \"%s\" in the initializer", field_name);
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
                pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
                "Field \"%s\" within initializer does not exist", field_name);
                goto error_cleanup;
            }
        }

        // Check each initializer
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, expr->struct_initializer.entries[i].member_value)) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Initializer for field \"%s\" is invalid", field_name);
            goto error_cleanup;
        }
        const PP_AST_Decoration* field_initializer_decor = pp_ast_get_decoration(expr->struct_initializer.entries[i].member_value);
        assert(field_initializer_decor);

        // Decayed type check
        const PPType* decayed_field_type = pp_type_decay(sa->gtt, field_type);
        const PPType* decayed_field_initializer_type = pp_type_decay(sa->gtt, field_initializer_decor->resolved_type);
        if (!pp_type_equal(decayed_field_type, decayed_field_initializer_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
            "Initializer for field \"%s\" does not match the field type.", field_name);
            goto error_cleanup;
        }
        // Reference
        if (pp_type_is_reference(field_type)) {
            // L-value check
            if (field_initializer_decor->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
                "Field \"%s\" is a reference, but the initializer is not an l-value.", field_name);
                goto error_cleanup;
            }
            // Non-const field cannot bind to const variable.
            if (!pp_type_is_const(field_type) && pp_type_is_const(field_initializer_decor->resolved_type)) {
                pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
                "Field \"%s\" is a mutable reference, but the initializer is immutable", field_name);
                goto error_cleanup;
            }
        }

        if (!field_initializer_decor->is_constexpr) {
            is_constexpr = false;
        }
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
 * [x] Decay expression type and target type via pp_type_decay
 * [x] Unwrap Newtype to underlying primitive for comparison
 * [x] Allow cast if primitive kinds equal or both numerical via pp_type_is_numerical
 * [x] Create decoration with target_type, PP_VALUE_CATEGORY_RVALUE, is_constexpr propagated from expr, escapable_ref false
 * [ ] Verify both decayed types are primitive or newtype with explicit error push as CZ does
 * [ ] Propagate environment scope level into decoration, CZ uses env->scope_level
 * [ ] Preserve is_constexpr from inner expression explicitly as done in PP, matches CZ intent
 */
static int pp_semantic_analyzer_check_cast_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
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

    if (pp_type_is_reference(target_type)) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
        "Casting to reference is not allowed.");
        goto error_cleanup;
    }

    const PPType* decayed_expr_type = pp_type_decay(sa->gtt, expr_decoration->resolved_type);
    NULL_POINTER_ERROR_HANDLE(decayed_expr_type);
    const PPType* decayed_target_type = pp_type_decay(sa->gtt, target_type);
    NULL_POINTER_ERROR_HANDLE(decayed_expr_type);

    const PPType* primitive_expr_type = decayed_expr_type->kind == PP_TK_Newtype ? decayed_expr_type->type.pp_type_newtype.underlying : decayed_expr_type;
    assert(primitive_expr_type);
    const PPType* primitive_target_type = decayed_target_type->kind == PP_TK_Newtype ? decayed_target_type->type.pp_type_newtype.underlying : decayed_target_type;
    assert(decayed_expr_type);
    
    if (primitive_expr_type->kind == primitive_target_type->kind) {
        // Same machine type (OK)
    } else if (pp_type_is_numerical(primitive_expr_type) && pp_type_is_numerical(primitive_target_type)) {
        // Numerical types can be casted to each other (OK)
    } else {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col,
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
 * [ ] Determine collection kind from resolved target type:
 *      - PP_TK_Array  → fixed size, element type = array element type
 *      - PP_TK_List   → unbounded, element type = list element type
 * [ ] For arrays with static size:
 *      - Compare entry count to array size; error on mismatch.
 *      - Reject zero-size arrays or treat as empty per policy.
 * [ ] For each initializer entry:
 *      - Recursively check expression via pp_semantic_analyzer_check_expression.
 *      - Require constexpr in global/struct default contexts.
 *      - Ensure decayed element type matches collection element type.
 *      - If element type is reference: initializer must be l-value and const-correct.
 * [ ] For lists:
 *      - No size check; allow any number of entries.
 *      - Same per-element validity, constexpr and type matching.
 * [ ] Populate AST decoration:
 *      - resolved_type = collection type
 *      - is_constexpr = all entries constexpr
 *      - value_category set appropriately for context
 * [ ] Error reporting via sa->error_list with line/col from expr.
 */
static int pp_semantic_analyzer_check_collection_initializer_expression(PP_SemanticAnalyzer* sa, PPSymbolTable* symbol_table, PP_AST_Node* expr) {
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
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col, "Collection initializer missing type");
        goto error_cleanup;
    }
    const PPType* elem_type = pp_type_from_type_node(sa, type_expr);
    if (!elem_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col, "Failed to resolve collection element type");
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
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col, "Invalid array size expression in collection initializer");
            goto error_cleanup;
        }
        if (arr_size == 0) {
            pp_error_list_push_error(sa->error_list, sa->filename, expr->line, expr->col, "Array size cannot be zero");
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
        if (!elem) continue;
        if (!pp_semantic_analyzer_check_expression(sa, symbol_table, elem)) {
            goto error_cleanup;
        }
        const PP_AST_Decoration* elem_dec = pp_ast_get_decoration(elem);
        assert(elem_dec);
        // Verify element type matches collection element type after decay
        if (elem_dec->resolved_type && !pp_type_equal(elem_dec->resolved_type, elem_type)) {
            pp_error_list_push_error(sa->error_list, sa->filename, elem->line, elem->col,
                "Collection element type mismatch");
            goto error_cleanup;
        }
        // Reference element types require lvalue initializer
        if (pp_type_is_reference(elem_type)) {
            if (elem_dec->value_category != PP_VALUE_CATEGORY_LVALUE) {
                pp_error_list_push_error(sa->error_list, sa->filename, elem->line, elem->col,
                    "Initializer for reference collection element must be an lvalue");
                goto error_cleanup;
            }
        }
        if (!elem_dec->is_constexpr) {
            all_constexpr = false;
            break;
        }
    }

    bool is_constexpr = is_array && all_constexpr;
    // Lists are never constexpr per language design
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

