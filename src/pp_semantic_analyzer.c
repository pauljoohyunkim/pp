#include "pp_semantic_analyzer.h"
#include <stdlib.h>
#include <string.h>

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
    if (parser == NULL) return NULL;

    PP_SemanticAnalyzer* sa = (PP_SemanticAnalyzer*)calloc(1, sizeof(PP_SemanticAnalyzer));
    if (!sa) return NULL;

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

    return sa;
}

/**
 * @brief Frees semantic analyzer and its components
 */
void pp_semantic_analyzer_free(PP_SemanticAnalyzer* sa) {
    if (!sa) return;

    free((char*)sa->code);
    free(sa->tokens);
    if (sa->program) {
        /* TODO: use proper AST free */
        /* pp_ast_root_free(sa->program); */
    }
    if (sa->sp) {
        /* TODO: pp_string_pool_free(sa->sp); */
    }
    if (sa->error_list) {
        pp_error_list_free(sa->error_list);
    }

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
