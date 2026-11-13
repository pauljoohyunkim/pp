#include "pp_lexer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pp_error.h"
#include "pp_string_pool.h"
#include "pp_tokens.h"

static int pp_lexer_push_token(PP_Lexer* lexer, PP_Token token);
static inline bool is_at_end(PP_Lexer* lexer);
static inline void advance_pp(PP_Lexer* lexer, unsigned int n);
static inline char peek_pp(PP_Lexer* lexer, unsigned int n);
static int pp_lexer_push_token_helper(PP_Lexer* lexer, PP_TokenType type, size_t length);
static bool pp_lexer_exactly_match_keyword(PP_Lexer* lexer, const char* keyword);
static size_t pp_lexer_scan_until_quote(PP_Lexer* lexer, size_t start_offset);

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

static size_t pp_lexer_scan_until_quote(PP_Lexer* lexer, size_t start_offset) {
    size_t pos = start_offset;
    while (lexer->idx + pos < lexer->code_length && peek_pp(lexer, pos) != '"') {
        char ch = peek_pp(lexer, pos);
        if (ch == '\n' || ch == '\r') {
            break;  // forbid multiline
        }
        if (ch == '\\')
            pos += 2;
        else
            pos++;
    }
    if (lexer->idx + pos < lexer->code_length) {
        pos += 1;  // include closing quote if present
    }
    return pos;
}

PP_Lexer* pp_lexer_create(const char* code, const char* filename, PP_StringPool* sp_in) {
    PP_Lexer* lexer = NULL;
    PP_Token* tokens = NULL;
    char* code_copy = NULL;
    PP_StringPool* sp = NULL;
    PP_ErrorList* error_list = NULL;

    if (code == NULL) return NULL;

    NULL_POINTER_ERROR_HANDLE(sp_in);

    const size_t code_length = strlen(code);

    lexer = (PP_Lexer*)calloc(1, sizeof(PP_Lexer));
    NULL_POINTER_ERROR_HANDLE(lexer);

    lexer->n_tokens_capacity = 8;
    lexer->n_tokens = 0;
    tokens = (PP_Token*)calloc(lexer->n_tokens_capacity, sizeof(PP_Token));
    NULL_POINTER_ERROR_HANDLE(tokens);

    code_copy = (char*)calloc(code_length + 1, sizeof(char));
    NULL_POINTER_ERROR_HANDLE(code_copy);
    strncpy(code_copy, code, code_length);

    sp = sp_in;

    error_list = pp_error_list_create();
    NULL_POINTER_ERROR_HANDLE(error_list);

    lexer->tokens = tokens;
    lexer->code = code_copy;
    lexer->sp = sp;
    lexer->error_list = error_list;
    lexer->filename = filename;
    lexer->row = 1;
    lexer->col = 1;
    lexer->code_length = code_length;
    lexer->idx = 0;

    return lexer;

error_cleanup:
    free(tokens);
    free(code_copy);
    pp_error_list_free(error_list);
    if (lexer) free(lexer);
    return NULL;
}

void pp_lexer_free(PP_Lexer* lexer) {
    if (lexer != NULL) {
        free(lexer->tokens);
        free((char*)lexer->code);
        /* String pool is owned by orchestrator, do not free here */
        pp_error_list_free(lexer->error_list);
        free(lexer);
    }
}

static int pp_lexer_push_token(PP_Lexer* lexer, PP_Token token) {
    if (lexer == NULL) return 0;
    if (lexer->n_tokens_capacity == lexer->n_tokens) {
        PP_Token* new_tokens = (PP_Token*)realloc(lexer->tokens, sizeof(PP_Token) * lexer->n_tokens_capacity * 2);
        if (!new_tokens) return 0;
        lexer->tokens = new_tokens;
        lexer->n_tokens_capacity *= 2;
    }
    lexer->tokens[lexer->n_tokens++] = token;
    return 1;
}

static inline bool is_at_end(PP_Lexer* lexer) { return lexer->idx >= lexer->code_length; }
static inline void advance_pp(PP_Lexer* lexer, unsigned int n) {
    if (is_at_end(lexer)) return;
    for (unsigned int i = 0; i < n; i++) {
        char c = lexer->code[lexer->idx];
        lexer->idx++;
        if (lexer->idx >= lexer->code_length) break;
        if (c == '\n') {
            lexer->row++;
            lexer->col = 1;
        } else {
            lexer->col++;
        }
    }
}
static inline char peek_pp(PP_Lexer* lexer, unsigned int n) {
    return lexer->idx + n >= lexer->code_length ? '\0' : lexer->code[lexer->idx + n];
}

static int pp_lexer_push_token_helper(PP_Lexer* lexer, PP_TokenType type, size_t length) {
    const char* lexeme = pp_string_pool_push(lexer->sp, lexer->code + lexer->idx, length);
    if (!lexeme) return 0;
    PP_Token tok = {.token_type = type, .lexeme = lexeme};
    tok.loc.filename = lexer->filename;
    tok.loc.line = lexer->row;
    tok.loc.column = lexer->col;
    return pp_lexer_push_token(lexer, tok);
}

static bool pp_lexer_exactly_match_keyword(PP_Lexer* lexer, const char* keyword) {
    size_t kw_len = strlen(keyword);
    if (lexer->idx + kw_len > lexer->code_length) return false;
    for (size_t i = 0; i < kw_len; i++) {
        if (lexer->code[lexer->idx + i] != keyword[i]) return false;
    }
    if (lexer->idx + kw_len < lexer->code_length) {
        char next = lexer->code[lexer->idx + kw_len];
        if (isalnum((unsigned char)next) || next == '_') return false;
    }
    return true;
}

int pp_lexer_analyze(PP_Lexer* lexer) {
    if (lexer == NULL) return 0;
    while (!is_at_end(lexer)) {
        char c = lexer->code[lexer->idx];
        if (isspace((unsigned char)c)) {
            advance_pp(lexer, 1);
            continue;
        }

        switch (c) {
            case '{':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_LEFT_CURLY_BRACKET, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case '}':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_RIGHT_CURLY_BRACKET, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case '(':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_LEFT_PARENTHESIS, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case ')':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_RIGHT_PARENTHESIS, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case '[':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_LEFT_SQUARE_BRACKET, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case ']':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_RIGHT_SQUARE_BRACKET, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case ';':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_SEMICOLON, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case '.':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_PERIOD, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case '#':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_HASH, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case ',':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_COMMA, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            case ':':
                if (peek_pp(lexer, 1) == ':') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_COLON_COLON, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_COLON, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '!':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_EXCLAMATION_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_EXCLAMATION, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '>':
                if (peek_pp(lexer, 1) == '>') {
                    if (peek_pp(lexer, 2) == '=') {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_GREATER_GREATER_EQUAL, 3)) return 0;
                        advance_pp(lexer, 3);
                    } else {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_GREATER_GREATER, 2)) return 0;
                        advance_pp(lexer, 2);
                    }
                } else if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_GREATER_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_GREATER, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '<':
                if (peek_pp(lexer, 1) == '<') {
                    if (peek_pp(lexer, 2) == '=') {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_LESS_LESS_EQUAL, 3)) return 0;
                        advance_pp(lexer, 3);
                    } else {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_LESS_LESS, 2)) return 0;
                        advance_pp(lexer, 2);
                    }
                } else if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_LESS_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_LESS, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '=':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_EQUAL_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_EQUAL, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '+':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_PLUS_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_PLUS, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '-':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_MINUS_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else if (peek_pp(lexer, 1) == '>') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_RIGHT_ARROW, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_MINUS, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '*':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_STAR_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_STAR, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '/':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_SLASH_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else if (peek_pp(lexer, 1) == '/') {
                    // comment
                    advance_pp(lexer, 2);
                    while (!is_at_end(lexer) && lexer->code[lexer->idx] != '\n') advance_pp(lexer, 1);
                    continue;
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_SLASH, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '%':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_PERCENT_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_PERCENT, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '&':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_AMPERSAND_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else if (peek_pp(lexer, 1) == '&') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_AMPERSAND_AMPERSAND, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_AMPERSAND, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '|':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_BAR_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else if (peek_pp(lexer, 1) == '|') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_BAR_BAR, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_BAR, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '^':
                if (peek_pp(lexer, 1) == '=') {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_CARET_EQUAL, 2)) return 0;
                    advance_pp(lexer, 2);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_CARET, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '@':
                if (peek_pp(lexer, 1) == '"') {
                    // static string literal: @"..."
                    size_t len = pp_lexer_scan_until_quote(lexer, 2);
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_STATIC_STRING_LITERAL, len)) return 0;
                    advance_pp(lexer, len);
                } else {
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_AT, 1)) return 0;
                    advance_pp(lexer, 1);
                }
                break;
            case '$':
                if (!pp_lexer_push_token_helper(lexer, PP_TT_DOLLAR, 1)) return 0;
                advance_pp(lexer, 1);
                break;
            default:
                if (isalpha((unsigned char)c) || c == '_') {
                    size_t len = 1;
                    while (!is_at_end(lexer) &&
                           (isalnum((unsigned char)peek_pp(lexer, len)) || peek_pp(lexer, len) == '_'))
                        len++;
                    // Keyword check
                    if (pp_lexer_exactly_match_keyword(lexer, "for")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_FOR, 3)) return 0;
                        advance_pp(lexer, 3);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "while")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_WHILE, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "if")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_IF, 2)) return 0;
                        advance_pp(lexer, 2);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "else")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_ELSE, 4)) return 0;
                        advance_pp(lexer, 4);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "return")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_RETURN, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "func")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_FUNC, 4)) return 0;
                        advance_pp(lexer, 4);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "lambda")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_LAMBDA, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "operator")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_OPERATOR, 8)) return 0;
                        advance_pp(lexer, 8);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "extern")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_EXTERN, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "let")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_LET, 3)) return 0;
                        advance_pp(lexer, 3);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "const")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_CONST, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "void")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_VOID, 4)) return 0;
                        advance_pp(lexer, 4);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "bool")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_BOOL, 4)) return 0;
                        advance_pp(lexer, 4);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "int8")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_INT8, 4)) return 0;
                        advance_pp(lexer, 4);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "uint8")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_UINT8, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "int16")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_INT16, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "uint16")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_UINT16, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "int32")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_INT32, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "uint32")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_UINT32, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "int64")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_INT64, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "uint64")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_UINT64, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "int128")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_INT128, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "uint128")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_UINT128, 7)) return 0;
                        advance_pp(lexer, 7);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "float")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_FLOAT, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "string")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_STRING, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "struct")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_STRUCT, 6)) return 0;
                        advance_pp(lexer, 6);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "typedef")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_TYPEDEF, 7)) return 0;
                        advance_pp(lexer, 7);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "newtype")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_NEWTYPE, 7)) return 0;
                        advance_pp(lexer, 7);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "ptr")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_PTR, 3)) return 0;
                        advance_pp(lexer, 3);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "nullptr")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_NULLPTR, 7)) return 0;
                        advance_pp(lexer, 7);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "as")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_AS, 2)) return 0;
                        advance_pp(lexer, 2);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "true")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_TRUE, 4)) return 0;
                        advance_pp(lexer, 4);
                    } else if (pp_lexer_exactly_match_keyword(lexer, "false")) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_FALSE, 5)) return 0;
                        advance_pp(lexer, 5);
                    } else {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_IDENTIFIER, len)) return 0;
                        advance_pp(lexer, len);
                    }
                } else if (isdigit((unsigned char)c)) {
                    size_t len = 1;
                    bool is_float = false;
                    while (!is_at_end(lexer) && isdigit((unsigned char)peek_pp(lexer, len))) len++;
                    if (!is_at_end(lexer) && peek_pp(lexer, len) == '.') {
                        is_float = true;
                        len++;
                        while (!is_at_end(lexer) && isdigit((unsigned char)peek_pp(lexer, len))) len++;
                    }
                    if (!is_float) {
                        if (!is_at_end(lexer) && (peek_pp(lexer, len) == 'u' || peek_pp(lexer, len) == 'U')) { len++; }
                        // Suffix parsing for integer literals: s, ss, l, ll case-insensitive, also allow overlong for
                        // error detection
                        while (!is_at_end(lexer)) {
                            char c_suf = tolower(peek_pp(lexer, len));
                            if (c_suf == 's' || c_suf == 'l') {
                                len++;
                            } else {
                                break;
                            }
                        }
                    }
                    if (is_float) {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_FLOAT_LITERAL, len)) return 0;
                    } else {
                        if (!pp_lexer_push_token_helper(lexer, PP_TT_INTEGER_LITERAL, len)) return 0;
                    }
                    advance_pp(lexer, len);
                } else if (c == '"') {
                    // simple string literal: skip until closing quote
                    size_t len = pp_lexer_scan_until_quote(lexer, 1);
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_STRING_LITERAL, len)) return 0;
                    advance_pp(lexer, len);
                } else {
                    // unknown
                    if (!pp_lexer_push_token_helper(lexer, PP_TT_UNKNOWN, 1)) return 0;
                    pp_error_list_push_error(lexer->error_list, (PP_SourceLoc){.filename=lexer->filename, .line=lexer->row, .column=lexer->col}, 
                                             "Unknown token");
                    advance_pp(lexer, 1);
                }
                break;
        }
    }
    // Push EOF
    PP_Token eof = {.token_type = PP_TT_EOF, .lexeme = NULL};
    eof.loc.filename = lexer->filename;
    eof.loc.line = lexer->row;
    eof.loc.column = lexer->col;
    if (!pp_lexer_push_token(lexer, eof)) return 0;
    return 1;
}
