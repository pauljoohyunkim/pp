#include "pp_semantic_analyzer.h"
#include <stdlib.h>
#include <string.h>

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr) == NULL) goto error_cleanup; } while (0)

/* Forward declarations for internal helpers - shells only */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa);
static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa);

/**
 * @brief Return read-only PPType from Type Expression Node.
 * Shell implementation - not implemented yet.
 */
const PPType* pp_type_from_type_node(const PP_AST_Node* type_node, PP_SemanticAnalyzer* sa) {
    (void)type_node;
    (void)sa;
    /* TODO: implement */
    return NULL;
}

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

    sa->current_function_return = NULL;

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
        /* Free may be NULL-safe */
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

    /* TODO: implement passes */
    /* pp_semantic_analyzer_build_global_table(sa); */
    /* pp_semantic_analyzer_full_analyze(sa); */

    return 1;
}

/* --- Shell stubs for internal passes --- */
static int pp_semantic_analyzer_build_global_table(PP_SemanticAnalyzer* sa) {
    (void)sa;
    return 0;
}

static int pp_semantic_analyzer_full_analyze(PP_SemanticAnalyzer* sa) {
    (void)sa;
    return 0;
}
