#include "pp_semantic_analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node);
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
static int pp_semantic_analyzer_full_analyze_function_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_lambda_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);
static int pp_semantic_analyzer_full_analyze_variable_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node);

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

    gst = pp_symbol_table_create(0);
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
            pp_global_type_table_intern(sa->gtt, t);
        }
        t = pp_type_create(PP_TK_Int32);
        if (t) {
            pp_global_type_table_intern(sa->gtt, t);
        }
        t = pp_type_create(PP_TK_Float);
        if (t) {
            pp_global_type_table_intern(sa->gtt, t);
        }
        t = pp_type_create(PP_TK_Bool);
        if (t) {
            pp_global_type_table_intern(sa->gtt, t);
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
#warning "Multiple passes might need return value checking for each pass."

    if (!pp_semantic_analyzer_build_global_table(sa)) {
        return 0;
    }
    if (!pp_semantic_analyzer_full_analyze(sa)) {
        return 0;
    }

    return 1;
}


static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node) {
    assert(sa);
    assert(type_node);
    assert(type_node->node_type == PP_AST_TypeExpressionNode);

    PPType *temp_type = NULL;                 /* owned – can be freed on error */
    const PPType *gtt_registered_type = NULL;/* borrowed from GTT – must not be freed here */

    switch (type_node->type_expression.kind) {
        case PP_AST_TYPE_KIND_VOID:
            temp_type = pp_type_create(PP_TK_Void);
            break;
        case PP_AST_TYPE_KIND_INT32:
            temp_type = pp_type_create(PP_TK_Int32);
            break;
        case PP_AST_TYPE_KIND_FLOAT:
            temp_type = pp_type_create(PP_TK_Float);
            break;
        case PP_AST_TYPE_KIND_BOOL:
            temp_type = pp_type_create(PP_TK_Bool);
            break;
        case PP_AST_TYPE_KIND_IDENTIFIER:
            #warning "Type identifier not yet implemented: Note that this might already be dealt using named_type and alises (and their underlying types)"
            abort();
            break;
    }
    if (!temp_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to create type");
        goto error_cleanup;
    }

    gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
    temp_type = NULL;
    if (!gtt_registered_type) {
        pp_error_list_push_error(sa->error_list, sa->filename, type_node->line, type_node->col, "Failed to intern type");
        goto error_cleanup;
    }

    if (type_node->type_expression.is_const) {
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

    #warning "Array/List not yet supported"

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
    func_symbol = pp_symbol_create(func_type, false, false);
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
    lambda_symbol = pp_symbol_create(lambda_type, false, false);
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

    /* Field processing deferred to Pass 2 - layout stays NULL in Pass 1 
    struct_layout = pp_struct_layout_create();
    if (!struct_layout) {
        pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Failed to allocate struct layout for struct '%s'", node->struct_declaration.name);
        goto error_cleanup;
    }

    for (size_t i = 0; i < node->struct_declaration.field_count; i++) {
        const PP_AST_StructField* field = &node->struct_declaration.fields[i];
        if (field->field->node_type != PP_AST_ParamDeclarationNode) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->line, field->field->col,
                                     "Expected parameter declaration for struct field");
            goto error_cleanup;
        }
        const PPType* field_type = pp_type_from_type_node(sa, field->field->param_declaration.type_expr);
        if (!field_type) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->param_declaration.type_expr->line, field->field->param_declaration.type_expr->col,
                                     "Failed to create type for struct field '%s'", field->field->param_declaration.identifier);
            goto error_cleanup;
        }

        if (!pp_struct_layout_push_field(struct_layout, field->field->param_declaration.identifier, field_type, field->initializer)) {
            pp_error_list_push_error(sa->error_list, sa->filename, field->field->line, field->field->col,
                                     "Failed to add struct field '%s'", field->field->param_declaration.identifier);
            goto error_cleanup;
        }
    }
    */


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

    symbol = pp_symbol_create(var_type, false, false);
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

static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa) {
    NULL_POINTER_ERROR_HANDLE(sa);
    NULL_POINTER_ERROR_HANDLE(sa->program);

    for (size_t i = 0; i < sa->program->program.decl_count; i++) {
        PP_AST_Node* decl = sa->program->program.decls[i];
        switch (decl->node_type) {
            case PP_AST_FunctionDeclarationNode:
                pp_semantic_analyzer_full_analyze_function_decl(sa, decl);
                break;
            case PP_AST_LambdaDeclarationNode:
                #warning "Lambda declaration check"
                break;
            case PP_AST_StructDeclarationNode:
                #warning "Struct declaration check
                break;
            case PP_AST_VariableDeclarationNode:
                #warning "Variable declaration check"
                break;
            case PP_AST_TypedefDeclarationNode:
                // Nothing to do
                break;
            case PP_AST_NewtypeDeclarationNode:
                // Nothing to do
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

static int pp_semantic_analyzer_full_analyze_function_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_FunctionDeclarationNode);


}

static int pp_semantic_analyzer_full_analyze_lambda_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_LambdaDeclarationNode);

}

static int pp_semantic_analyzer_full_analyze_variable_decl(PP_SemanticAnalyzer* sa, PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_VariableDeclarationNode);

}
