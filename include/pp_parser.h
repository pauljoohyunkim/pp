#ifndef PP_PARSER_H
#define PP_PARSER_H

#include <stddef.h>
#include "pp_tokens.h"
#include "pp_lexer.h"
#include "pp_error.h"
#include "pp_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* filename;
    const char* code;
    size_t code_length;
    PP_Token* tokens;
    size_t n_tokens;
    size_t idx;
    PP_AST_Node* program;
    PP_ErrorList* error_list;
    PP_StringPool* sp;
} PP_Parser;

/**
 * @brief Creates PP_Parser.
 * 
 * @param lexer Pointer to PP_Lexer struct.
 * @return PP_Parser* Pointer to PP_Parser struct upon success. NULL if failure.
 * 
 * Ownership of code string and token array are moved to Parser.
 */
PP_Parser* pp_parser_create(PP_Lexer* lexer);

/**
 * @brief Parse tokens to AST.
 * 
 * @param parser Pointer to PP_Parser struct.
 * @return int 1 if successful. 0 otherwise.
 */
int pp_parser_parse(PP_Parser* parser);

/**
 * @brief Frees the PP_Parser.
 * 
 * @param parser Pointer to PP_Parser struct.
 */
void pp_parser_free(PP_Parser* parser);

#ifdef __cplusplus
}
#endif

#endif /* PP_PARSER_H */
