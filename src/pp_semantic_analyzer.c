#include "pp_semantic_analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr) == NULL) goto error_cleanup; } while (0)

static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node);
/* Pass I */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa);
static int pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static int pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
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


static int pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_FunctionDeclarationNode);
    PPType *tmp_func = NULL;
    const PPType *func_type = NULL;
    PPSymbol* func_symbol = NULL;

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

static int pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);

    return 1;
error_cleanup:
    return 0;
}

static int pp_semantic_analyzer_build_global_table_variable_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_VariableDeclarationNode);
    assert(node->variable_declaration.var_decl->node_type == PP_AST_ParamDeclarationNode);
    assert(node->variable_declaration.var_decl->param_declaration.identifier);
    PPSymbol* symbol = NULL;

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
