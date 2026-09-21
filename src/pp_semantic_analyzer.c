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

/**
 * @brief Creates semantic analyzer from parser
 */
PP_SemanticAnalyzer* pp_semantic_analyzer_create(PP_Parser* parser) {
    PP_SemanticAnalyzer* sa = NULL;
    PP_GlobalTypeTable* gtt = NULL;
    PPSymbolTable* gst = NULL;

    NULL_POINTER_ERROR_HANDLE(parser);

    sa = (PP_SemanticAnalyzer*)calloc(1, sizeof(PP_SemanticAnalyzer));
    NULL_POINTER_ERROR_HANDLE(sa);

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
    NULL_POINTER_ERROR_HANDLE(gtt);

    gst = pp_symbol_table_create(0);
    NULL_POINTER_ERROR_HANDLE(gst);

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
    /* pp_semantic_analyzer_full_analyze(sa); */

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
            #warning "Type identifier not yet implemented"
            abort();
            break;
    }
    NULL_POINTER_ERROR_HANDLE(temp_type);

    gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
    temp_type = NULL;
    NULL_POINTER_ERROR_HANDLE(gtt_registered_type);

    if (type_node->type_expression.is_const) {
        temp_type = pp_type_create(PP_TK_Const);
        NULL_POINTER_ERROR_HANDLE(temp_type);
        temp_type->type.pp_type_const.const_of = gtt_registered_type;
        gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
        temp_type = NULL;
        NULL_POINTER_ERROR_HANDLE(gtt_registered_type);
    }

    #warning "Array/List not yet supported"

    if (type_node->type_expression.is_reference) {
        temp_type = pp_type_create(PP_TK_Reference);
        NULL_POINTER_ERROR_HANDLE(temp_type);
        temp_type->type.pp_type_reference.reference_to = gtt_registered_type;
        gtt_registered_type = pp_global_type_table_intern(sa->gtt, temp_type);
        temp_type = NULL;
        NULL_POINTER_ERROR_HANDLE(gtt_registered_type);
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
 * [ ] Resolve return type
 * [ ] Resolve parameter types
 * [ ] Build function type and intern in GTT
 * [ ] Create symbol and register in global symbol table
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
    NULL_POINTER_ERROR_HANDLE(tmp_func);

    const PPType* return_type = pp_type_from_type_node(sa, node->function_declaration.return_type);
    NULL_POINTER_ERROR_HANDLE(return_type);

    // Assign read-only return_type pointer.
    tmp_func->type.pp_type_function.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->function_declaration.param_count; i++) {
        const PPType* param_type = pp_type_from_type_node(sa, node->function_declaration.params[i]->param_declaration.type_expr);
        NULL_POINTER_ERROR_HANDLE(param_type);
        if (!pp_type_push_param(tmp_func, param_type)) {
            goto error_cleanup;
        }
    }

    // Add to type table
    func_type = pp_global_type_table_intern(sa->gtt, tmp_func);
    tmp_func = NULL;
    NULL_POINTER_ERROR_HANDLE(func_type);

    // Create symbol
    func_symbol = pp_symbol_create(func_type, false, false);
    NULL_POINTER_ERROR_HANDLE(func_symbol);

    // Register to symbol table.
    if (!pp_symbol_table_push(sa->gst, node->function_declaration.name, func_symbol)) {
        goto error_cleanup;
    }
    func_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_func);
    pp_symbol_free(func_symbol);
    assert(sa);
    assert(sa->error_list);
    pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Semantic error processing function declaration '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
    return 0;
}

/* Pass 1 - Lambda Declaration Checklist:
 * [x] Detect duplicate symbol
 * [ ] Resolve return type
 * [ ] Resolve parameter types
 * [ ] Build lambda type and intern in GTT
 * [ ] Create symbol and register in global symbol table
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
    NULL_POINTER_ERROR_HANDLE(tmp_lambda);

    const PPType* return_type = pp_type_from_type_node(sa, node->function_declaration.return_type);
    NULL_POINTER_ERROR_HANDLE(return_type);

    // Assign read-only return_type pointer.
    tmp_lambda->type.pp_type_lambda.return_type = return_type;

    // Add parameters
    for (size_t i = 0; i < node->function_declaration.param_count; i++) {
        const PPType* param_type = pp_type_from_type_node(sa, node->function_declaration.params[i]->param_declaration.type_expr);
        NULL_POINTER_ERROR_HANDLE(param_type);
        if (!pp_type_push_param(tmp_lambda, param_type)) {
            goto error_cleanup;
        }
    }

    // Add to type table
    lambda_type = pp_global_type_table_intern(sa->gtt, tmp_lambda);
    tmp_lambda = NULL;
    NULL_POINTER_ERROR_HANDLE(lambda_type);

    // Create symbol
    lambda_symbol = pp_symbol_create(lambda_type, false, false);
    NULL_POINTER_ERROR_HANDLE(lambda_symbol);

    // Register to symbol table.
    if (!pp_symbol_table_push(sa->gst, node->function_declaration.name, lambda_symbol)) {
        goto error_cleanup;
    }
    lambda_symbol = NULL;

    return 1;

error_cleanup:
    pp_type_free(tmp_lambda);
    pp_symbol_free(lambda_symbol);
    assert(sa);
    assert(sa->error_list);
    pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Semantic error processing function declaration '%s'", node->function_declaration.name ? node->function_declaration.name : "<unknown>");
    return 0;
}

/* Pass 1 - Struct Declaration Checklist:
 * [ ] Detect duplicate type name in GTT
 * [ ] Create struct type with NULL layout (defer field processing to Pass 2)
 * [ ] Intern struct type in GTT
 */
static int pp_semantic_analyzer_build_global_table_struct_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_StructDeclarationNode);
    PPType* tmp_struct = NULL;
    const PPType* struct_type = NULL;
    PP_StructLayout* struct_layout = NULL;

    struct_layout = pp_struct_layout_create();
    NULL_POINTER_ERROR_HANDLE(struct_layout);

    #warning "Not yet resolving types to AST"
    for (size_t i = 0; i < node->struct_declaration.field_count; i++) {
        const PP_AST_StructField* field = &node->struct_declaration.fields[i];
        assert(field->field->node_type == PP_AST_ParamDeclarationNode);
        const PPType* field_type = pp_type_from_type_node(sa, field->field->param_declaration.type_expr);
        NULL_POINTER_ERROR_HANDLE(field_type);

        if (!pp_struct_layout_push_field(struct_layout, field->field->param_declaration.identifier, field_type, field->initializer)) {
            goto error_cleanup;
        }
    }

    tmp_struct = pp_type_create(PP_TK_Struct);
    NULL_POINTER_ERROR_HANDLE(tmp_struct);

    /* Assign */
    tmp_struct->type.pp_type_struct.layout = struct_layout;
    struct_layout = NULL;
    tmp_struct->type.pp_type_struct.name = node->struct_declaration.name;

    /* Add to type table */
    struct_type = pp_global_type_table_intern(sa->gtt, tmp_struct);
    tmp_struct = NULL;
    NULL_POINTER_ERROR_HANDLE(struct_type);

    return 1;

error_cleanup:
    pp_type_free(tmp_struct);
    pp_struct_layout_free(struct_layout);
    assert(sa);
    assert(sa->error_list);
    pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Semantic error processing struct declaration '%s'", node->struct_declaration.name ? node->struct_declaration.name : "<unknown>");
    return 0;
}

/* Pass 1 - Variable Declaration Checklist:
 * [x] Detect duplicate symbol in current scope
 * [ ] Resolve variable type
 * [ ] Disallow reference types for global variables
 * [ ] Create symbol and register in global symbol table
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

    #warning "Variable temporarily constructed"
    const PPType* var_type = pp_type_from_type_node(sa, node->variable_declaration.var_decl->param_declaration.type_expr);

    symbol = pp_symbol_create(var_type, false, false);
    NULL_POINTER_ERROR_HANDLE(symbol);

    if (!pp_symbol_table_push(sa->gst, node->variable_declaration.var_decl->param_declaration.identifier, symbol)) {
        // TODO: ERROR_MESSAGE
        goto error_cleanup;
    }
    symbol = NULL;

    return 1;
error_cleanup:
    pp_symbol_free(symbol);
    assert(sa);
    assert(sa->error_list);
    pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Semantic error processing variable declaration '%s'", node->variable_declaration.var_decl->param_declaration.identifier);
    return 0;
}

/* Pass 1 - Typedef Declaration Checklist:
 * [ ] Ensure base type is defined
 * [ ] Ensure base type is not const or reference
 * [ ] Detect duplicate typedef name
 * [ ] Register alias in GTT
 */
static int pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_TypedefDeclarationNode);

    #warning "Typedef temporarily constructed"

    const PPType* base_type = pp_type_from_type_node(sa, node->typedef_declaration.base_type);
    NULL_POINTER_ERROR_HANDLE(base_type);
    
    if (!pp_global_type_table_register_alias(sa->gtt, node->typedef_declaration.identifier, base_type)) {
        // TODO: ERROR MESSAGE
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    assert(sa);
    assert(sa->error_list);
    pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Semantic error processing typedef declaration '%s'", node->typedef_declaration.identifier);
    return 0;
}

/* Pass 1 - Newtype Declaration Checklist:
 * [ ] Ensure base type is defined
 * [ ] Ensure base type is not const or reference
 * [ ] Detect duplicate newtype name
 * [ ] Detect cycles in newtype definition
 * [ ] Register named type in GTT
 */
static int pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_NewtypeDeclarationNode);

    const PPType* base_type = pp_type_from_type_node(sa, node->newtype_declaration.base_type);
    NULL_POINTER_ERROR_HANDLE(base_type);

    if (!pp_global_type_table_register_named(sa->gtt, node->newtype_declaration.identifier, base_type)) {
        // TODO: ERROR MESSAGE
        goto error_cleanup;
    }

    return 1;

error_cleanup:
    assert(sa);
    assert(sa->error_list);
    pp_error_list_push_error(sa->error_list, sa->filename, node->line, node->col, "Semantic error processing newtype declaration '%s'", node->newtype_declaration.identifier);
    return 0;
}

static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa) {
    (void)sa;
    return 0;
}
