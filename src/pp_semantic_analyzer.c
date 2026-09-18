#include "pp_semantic_analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr) == NULL) goto error_cleanup; } while (0)

static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node);
/* Pass I */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa);
static void pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static void pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static void pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);
static void pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node);

/* Pass II */
static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa);

/**
 * @brief Creates semantic analyzer from parser
 */
PP_SemanticAnalyzer* pp_semantic_analyzer_create(PP_Parser* parser) {
    PP_SemanticAnalyzer* sa = NULL;
    PP_GlobalTypeTable* gtt = NULL;

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
    sa->gtt = gtt;

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

    free(sa);
}

/**
 * @brief Invoke semantic analyzer to analyze the AST.
 */
int pp_semantic_analyzer_analyze(PP_SemanticAnalyzer* sa) {
    if (!sa) return 0;
#warning "Multiple passes might need return value checking for each pass."

    pp_semantic_analyzer_build_global_table(sa);
    /* pp_semantic_analyzer_full_analyze(sa); */

    return 1;
}


static const PPType* pp_type_from_type_node(PP_SemanticAnalyzer* sa, const PP_AST_Node* type_node) {
    assert(sa);
    assert(type_node);
    assert(type_node->node_type == PP_AST_TypeExpressionNode);
    PPType* type = NULL;
    PPType* subtype = NULL;

    switch (type_node->type_expression.kind) {
        case PP_AST_TYPE_KIND_VOID:
            type = pp_type_create(PP_TK_Void);
            break;
        case PP_AST_TYPE_KIND_INT32:
            type = pp_type_create(PP_TK_Int32);
            break;
        case PP_AST_TYPE_KIND_FLOAT:
            type = pp_type_create(PP_TK_Float);
            break;
        case PP_AST_TYPE_KIND_BOOL:
            type = pp_type_create(PP_TK_Bool);
            break;
        case PP_AST_TYPE_KIND_IDENTIFIER:
            #warning "Type identifier not yet implemented"
            abort();
            break;
        //default:
        //    assert(0 && "Invalid AST type kind");
        //    abort();
    }
    NULL_POINTER_ERROR_HANDLE(type);

    // Intern
    type = pp_global_type_table_intern(sa->gtt, type);
    NULL_POINTER_ERROR_HANDLE(type);

    // Constness
    if (type_node->type_expression.is_const) {
        subtype = type;
        type = NULL;

        // Create const wrapper
        type = pp_type_create(PP_TK_Const);
        NULL_POINTER_ERROR_HANDLE(type);

        // Move internal type to a leaf ot const wrapper.
        type->type.pp_type_const.const_of = subtype;
        subtype = NULL;

        // Intern
        type = pp_global_type_table_intern(sa->gtt, type);
        NULL_POINTER_ERROR_HANDLE(type);
    }

    #warning "Array/List and reference needs implementation"
    //if (type_node->type_expression.array_spec) {
    //    subtype = type;
    //    type = NULL;

    //    // Create array/list wrapper
    //    type->type.pp_type_
    //}
    if (type_node->type_expression.is_reference) {
        subtype = type;
        type = NULL;

        type->type.pp_type_reference.reference_to = subtype;
        subtype = NULL;

        // Intern
        type = pp_global_type_table_intern(sa->gtt, type);
        NULL_POINTER_ERROR_HANDLE(type);
    }

    return type;

error_cleanup:
    pp_type_free(type);
    pp_type_free(subtype);

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


static void pp_semantic_analyzer_build_global_table_function_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);


error_cleanup:
    return;
}

static void pp_semantic_analyzer_build_global_table_lambda_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);


error_cleanup:
    return;
}

static void pp_semantic_analyzer_build_global_table_typedef_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_TypedefDeclarationNode);

    #warning "Typedef temporarily constructed"

    PPType* ptr = pp_type_from_type_node(sa, node->typedef_declaration.base_type);

    return;

error_cleanup:
    return;
}

static void pp_semantic_analyzer_build_global_table_newtype_decl(PP_SemanticAnalyzer* sa, const PP_AST_Node* node) {
    assert(sa);
    assert(node);
    assert(node->node_type == PP_AST_NewtypeDeclarationNode);


error_cleanup:
    return;
}

static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa) {
    (void)sa;
    return 0;
}
