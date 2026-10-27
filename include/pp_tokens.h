#ifndef PP_TOKENS_H
#define PP_TOKENS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef enum {
    // Single-letter
    PP_TT_LEFT_CURLY_BRACKET,
    PP_TT_RIGHT_CURLY_BRACKET,
    PP_TT_LEFT_PARENTHESIS,
    PP_TT_RIGHT_PARENTHESIS,
    PP_TT_LEFT_SQUARE_BRACKET,
    PP_TT_RIGHT_SQUARE_BRACKET,
    PP_TT_EQUAL,
    PP_TT_PLUS,
    PP_TT_MINUS,
    PP_TT_STAR,
    PP_TT_SLASH,
    PP_TT_PERCENT,
    PP_TT_AMPERSAND,
    PP_TT_BAR,
    PP_TT_CARET,
    PP_TT_SEMICOLON,
    PP_TT_PERIOD,
    PP_TT_COMMA,
    PP_TT_COLON,
    PP_TT_EXCLAMATION,
    PP_TT_GREATER,
    PP_TT_LESS,

    // Double-letter
    PP_TT_PLUS_EQUAL,
    PP_TT_MINUS_EQUAL,
    PP_TT_STAR_EQUAL,
    PP_TT_SLASH_EQUAL,
    PP_TT_PERCENT_EQUAL,
    PP_TT_AMPERSAND_EQUAL,
    PP_TT_BAR_EQUAL,
    PP_TT_CARET_EQUAL,
    PP_TT_EQUAL_EQUAL,
    PP_TT_EXCLAMATION_EQUAL,
    PP_TT_GREATER_EQUAL,
    PP_TT_LESS_EQUAL,
    PP_TT_RIGHT_ARROW,
    PP_TT_COLON_COLON,
    PP_TT_AMPERSAND_AMPERSAND,
    PP_TT_BAR_BAR,

    // Others
    PP_TT_FOR,
    PP_TT_WHILE,
    PP_TT_IF,
    PP_TT_ELSE,
    PP_TT_RETURN,
    PP_TT_FUNC,
    PP_TT_LAMBDA,
    PP_TT_LET,
    PP_TT_CONST,
    PP_TT_VOID,
    PP_TT_BOOL,
    PP_TT_INT32,
    PP_TT_UINT32,
    PP_TT_FLOAT,
    PP_TT_STRUCT,
    PP_TT_TYPEDEF,
    PP_TT_NEWTYPE,
    PP_TT_AS,
    PP_TT_TRUE,
    PP_TT_FALSE,
    PP_TT_INTEGER_LITERAL,
    PP_TT_UNSIGNED_INTEGER_LITERAL,
    PP_TT_FLOAT_LITERAL,
    PP_TT_STRING_LITERAL,
    PP_TT_IDENTIFIER,
    PP_TT_UNKNOWN,

    PP_TT_EOF
} PP_TokenType;

typedef struct {
    PP_TokenType token_type;
    const char* lexeme;
    unsigned int line;
    unsigned int column;
} PP_Token;

const char* pp_token_to_human_name(PP_TokenType token_type);
bool pp_token_type_is_assignment(PP_TokenType token_type);

#ifdef __cplusplus
}
#endif

#endif /* PP_TOKENS_H */
