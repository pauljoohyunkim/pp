#ifndef PP_LEXER_H
#define PP_LEXER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "pp_tokens.h"
#include "pp_string_pool.h"

typedef struct {
    const char* filename;
    const char* code;
    size_t code_length;
    unsigned int idx;
    unsigned int row;
    unsigned int col;
    PP_Token* tokens;
    size_t n_tokens_capacity;
    size_t n_tokens;
    PP_StringPool* sp;
} PP_Lexer;

PP_Lexer* pp_lexer_create(const char* code, const char* filename);
int pp_lexer_analyze(PP_Lexer* lexer);
void pp_lexer_free(PP_Lexer* lexer);

#ifdef __cplusplus
}
#endif

#endif /* PP_LEXER_H */
