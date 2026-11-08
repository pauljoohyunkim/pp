#include "pp_tokens.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

bool pp_token_type_is_assignment(PP_TokenType token_type) {
    switch (token_type) {
        case PP_TT_EQUAL:
        case PP_TT_PLUS_EQUAL:
        case PP_TT_MINUS_EQUAL:
        case PP_TT_STAR_EQUAL:
        case PP_TT_SLASH_EQUAL:
        case PP_TT_PERCENT_EQUAL:
        case PP_TT_AMPERSAND_EQUAL:
        case PP_TT_BAR_EQUAL:
        case PP_TT_CARET_EQUAL:
        case PP_TT_LESS_LESS_EQUAL:
        case PP_TT_GREATER_GREATER_EQUAL:
            return true;
        default:
            return false;
    }
}

const char* pp_token_to_human_name(PP_TokenType token_type) {
    switch (token_type) {
        case PP_TT_LEFT_CURLY_BRACKET:
            return "{";
        case PP_TT_RIGHT_CURLY_BRACKET:
            return "}";
        case PP_TT_LEFT_PARENTHESIS:
            return "(";
        case PP_TT_RIGHT_PARENTHESIS:
            return ")";
        case PP_TT_LEFT_SQUARE_BRACKET:
            return "[";
        case PP_TT_RIGHT_SQUARE_BRACKET:
            return "]";
        case PP_TT_EQUAL:
            return "=";
        case PP_TT_PLUS:
            return "+";
        case PP_TT_MINUS:
            return "-";
        case PP_TT_STAR:
            return "*";
        case PP_TT_SLASH:
            return "/";
        case PP_TT_PERCENT:
            return "%";
        case PP_TT_AMPERSAND:
            return "&";
        case PP_TT_BAR:
            return "|";
        case PP_TT_CARET:
            return "^";
        case PP_TT_SEMICOLON:
            return ";";
        case PP_TT_PERIOD:
            return ".";
        case PP_TT_HASH:
            return "#";
        case PP_TT_COMMA:
            return ",";
        case PP_TT_COLON:
            return ":";
        case PP_TT_EXCLAMATION:
            return "!";
        case PP_TT_GREATER:
            return ">";
        case PP_TT_LESS:
            return "<";

        // Double-letter
        case PP_TT_PLUS_EQUAL:
            return "+=";
        case PP_TT_MINUS_EQUAL:
            return "-=";
        case PP_TT_STAR_EQUAL:
            return "*=";
        case PP_TT_SLASH_EQUAL:
            return "/=";
        case PP_TT_PERCENT_EQUAL:
            return "%=";
        case PP_TT_AMPERSAND_EQUAL:
            return "&=";
        case PP_TT_BAR_EQUAL:
            return "|=";
        case PP_TT_CARET_EQUAL:
            return "^=";
        case PP_TT_EQUAL_EQUAL:
            return "==";
        case PP_TT_EXCLAMATION_EQUAL:
            return "!=";
        case PP_TT_GREATER_EQUAL:
            return ">=";
        case PP_TT_LESS_EQUAL:
            return "<=";
        case PP_TT_RIGHT_ARROW:
            return "->";
        case PP_TT_COLON_COLON:
            return "::";
        case PP_TT_AMPERSAND_AMPERSAND:
            return "&&";
        case PP_TT_BAR_BAR:
            return "||";
        case PP_TT_LESS_LESS:
            return "<<";
        case PP_TT_GREATER_GREATER:
            return ">>";
        case PP_TT_LESS_LESS_EQUAL:
            return "<<=";
        case PP_TT_GREATER_GREATER_EQUAL:
            return ">>=";

        // Keywords / Types
        case PP_TT_FOR:
            return "for";
        case PP_TT_WHILE:
            return "while";
        case PP_TT_IF:
            return "if";
        case PP_TT_ELSE:
            return "else";
        case PP_TT_RETURN:
            return "return";
        case PP_TT_FUNC:
            return "func";
        case PP_TT_LAMBDA:
            return "lambda";
        case PP_TT_LET:
            return "let";
        case PP_TT_CONST:
            return "const";
        case PP_TT_VOID:
            return "void";
        case PP_TT_BOOL:
            return "bool";
        case PP_TT_INT8:
            return "int8";
        case PP_TT_UINT8:
            return "uint8";
        case PP_TT_INT16:
            return "int16";
        case PP_TT_UINT16:
            return "uint16";
        case PP_TT_INT32:
            return "int32";
        case PP_TT_UINT32:
            return "uint32";
        case PP_TT_INT64:
            return "int64";
        case PP_TT_UINT64:
            return "uint64";
        case PP_TT_INT128:
            return "int128";
        case PP_TT_UINT128:
            return "uint128";
        case PP_TT_FLOAT:
            return "float";
        case PP_TT_STRING:
            return "string";
        case PP_TT_STRUCT:
            return "struct";
        case PP_TT_TYPEDEF:
            return "typedef";
        case PP_TT_NEWTYPE:
            return "newtype";
        case PP_TT_AS:
            return "as";
        case PP_TT_TRUE:
            return "true";
        case PP_TT_FALSE:
            return "false";

        // Literals
        case PP_TT_INTEGER_LITERAL:
            return "integer literal";
        case PP_TT_FLOAT_LITERAL:
            return "float literal";
        case PP_TT_STATIC_STRING_LITERAL:
            return "static string literal";
        case PP_TT_STRING_LITERAL:
            return "string literal";
        case PP_TT_IDENTIFIER:
            return "identifier";
        case PP_TT_UNKNOWN:
            return "unknown";
        case PP_TT_EOF:
            return "EOF";

        default:
            return NULL;
    }
}

#include <ctype.h>
#include <string.h>

/* Return unescaped byte length of a string literal lexeme.
 * For static string: lexeme is @"..."; for heap string: "..."
 * Returns -1 on invalid escape, otherwise number of bytes in the unescaped content.
 * Caller should add +1 for NUL if needed for static strings.
 */
ssize_t pp_tokens_unescape_string_length(const char* lexeme) {
    if (!lexeme) return -1;
    size_t i = 0;
    size_t len = strlen(lexeme);
    /* Skip opening delimiter */
    if (len >= 2 && lexeme[0] == '@' && lexeme[1] == '"') {
        i = 2;
    } else if (len >= 2 && lexeme[0] == '"') {
        i = 1;
    } else {
        return -1;
    }
    ssize_t out = 0;
    while (i < len) {
        char c = lexeme[i];
        if (c == '"') {
            /* End of literal */
            break;
        }
        if (c == '\\') {
            if (i + 1 >= len) return -1;
            char esc = lexeme[i + 1];
            switch (esc) {
                case 'n': out += 1; i += 2; continue;
                case 't': out += 1; i += 2; continue;
                case 'r': out += 1; i += 2; continue;
                case '"': out += 1; i += 2; continue;
                case '\\': out += 1; i += 2; continue;
                case '0': out += 1; i += 2; continue;
                case 'x': {
                    if (i + 3 >= len) return -1;
                    char h1 = lexeme[i + 2];
                    char h2 = lexeme[i + 3];
                    if (!isxdigit((unsigned char)h1) || !isxdigit((unsigned char)h2)) return -1;
                    out += 1; i += 4; continue;
                }
                default:
                    /* Unknown escape */
                    return -1;
            }
        }
        /* Normal char */
        out += 1;
        i += 1;
    }
    return out;
}

