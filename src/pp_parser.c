#include "pp_parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pp_ast.h"
#include "pp_error.h"

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

typedef enum {
    PP_PRECEDENCE_NONE = 0,
    PP_PRECEDENCE_LOGICAL_OR = 10,
    PP_PRECEDENCE_LOGICAL_AND = 20,
    PP_PRECEDENCE_EQUALITY = 30,
    PP_PRECEDENCE_RELATIONAL = 40,
    PP_PRECEDENCE_BITWISE_OR = 50,
    PP_PRECEDENCE_BITWISE_XOR = 60,
    PP_PRECEDENCE_BITWISE_AND = 70,
    PP_PRECEDENCE_BITSHIFT = 80,
    PP_PRECEDENCE_ADDITIVE = 90,
    PP_PRECEDENCE_MULTIPLICATIVE = 100,
    PP_PRECEDENCE_CAST = 110,
    PP_PRECEDENCE_UNARY = 120,
    PP_PRECEDENCE_CALL_ACCESS = 130,
    PP_PRECEDENCE_PRIMARY = 140
} PP_Precedence;

static inline unsigned int pp_parser_get_line(const PP_Parser* parser);
static inline unsigned int pp_parser_get_col(const PP_Parser* parser);
static PP_TokenType pp_parser_peek_token_type(const PP_Parser* parser, unsigned int offset);
static int pp_parser_infix_binding_power(PP_TokenType token_type, unsigned char* left_bp, unsigned char* right_bp);
static bool pp_parser_is_sync_token(PP_TokenType token_type);
static int pp_parser_sync(PP_Parser* parser);
static PP_Token* pp_parser_consume_token(PP_Parser* parser, PP_TokenType expected_type);

/* Forward declarations for parser phases */
static PP_AST_Node* pp_parser_create_program_node(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_statement(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_block(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_if_statement(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_while_statement(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_for_statement(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_return_statement(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_variable_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_variable_decl(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_param_decl(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_expression_ext(PP_Parser* parser, unsigned char min_bp);
static PP_AST_Node* pp_parser_create_expression_or_assignment(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_literal(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_type(PP_Parser* parser);
static PP_AST_Node* pp_parser_parse_collection_type(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_struct_initializer(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_collection_initializer(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_function_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_lambda_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_extern_function_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_extern_lambda_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_struct_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_typedef_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_newtype_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_global_variable_declaration(PP_Parser* parser);
static int pp_parser_parse_param_list(PP_Parser* parser, PP_AST_Node*** out_params, unsigned int* out_count,
                                      unsigned int* out_capacity);
static int pp_parser_parse_signature(PP_Parser* parser, const char** out_name, PP_AST_Node*** out_params,
                                     unsigned int* out_param_count, unsigned int* out_param_capacity,
                                     PP_AST_Node** out_return_type);

PP_Parser* pp_parser_create(PP_Lexer* lexer) {
    PP_Parser* parser = NULL;
    PP_ErrorList* error_list = NULL;
    NULL_POINTER_ERROR_HANDLE(lexer);

    parser = (PP_Parser*)calloc(1, sizeof(PP_Parser));
    NULL_POINTER_ERROR_HANDLE(parser);

    error_list = pp_error_list_create();
    NULL_POINTER_ERROR_HANDLE(error_list);

    parser->code = lexer->code;
    lexer->code = NULL;
    parser->code_length = lexer->code_length;

    parser->tokens = lexer->tokens;
    lexer->tokens = NULL;
    parser->n_tokens = lexer->n_tokens;

    parser->filename = lexer->filename;

    parser->sp = lexer->sp;
    lexer->sp = NULL;

    parser->error_list = error_list;

    return parser;
error_cleanup:
    free(parser);
    return NULL;
}

int pp_parser_parse(PP_Parser* parser) {
    if (!parser) return 0;
    parser->program = pp_parser_create_program_node(parser);
    if (!parser->program) { return 0; }
    return 1;
}

void pp_parser_free(PP_Parser* parser) {
    if (!parser) return;
    free((char*)parser->code);
    free(parser->tokens);
    pp_error_list_free(parser->error_list);
    pp_ast_root_free(parser->program);
    free(parser);
}

static inline unsigned int pp_parser_get_line(const PP_Parser* parser) {
    if (!parser || parser->idx >= parser->n_tokens) return 0;
    return parser->tokens[parser->idx].line;
}

static inline unsigned int pp_parser_get_col(const PP_Parser* parser) {
    if (!parser || parser->idx >= parser->n_tokens) return 0;
    return parser->tokens[parser->idx].column;
}

static PP_TokenType pp_parser_peek_token_type(const PP_Parser* parser, unsigned int offset) {
    if (!parser) return PP_TT_UNKNOWN;
    size_t idx = parser->idx + offset;
    if (idx >= parser->n_tokens) return PP_TT_EOF;
    return parser->tokens[idx].token_type;
}

static int pp_parser_infix_binding_power(PP_TokenType token_type, unsigned char* left_bp, unsigned char* right_bp) {
    unsigned char lb = 0, rb = 0;
    switch (token_type) {
        case PP_TT_AMPERSAND_AMPERSAND:
            lb = PP_PRECEDENCE_LOGICAL_AND;
            rb = PP_PRECEDENCE_LOGICAL_AND + 1;
            break;
        case PP_TT_BAR_BAR:
            lb = PP_PRECEDENCE_LOGICAL_OR;
            rb = PP_PRECEDENCE_LOGICAL_OR + 1;
            break;
        case PP_TT_EQUAL_EQUAL:
        case PP_TT_EXCLAMATION_EQUAL:
            lb = PP_PRECEDENCE_EQUALITY;
            rb = PP_PRECEDENCE_EQUALITY + 1;
            break;
        case PP_TT_LESS:
        case PP_TT_GREATER:
        case PP_TT_LESS_EQUAL:
        case PP_TT_GREATER_EQUAL:
            lb = PP_PRECEDENCE_RELATIONAL;
            rb = PP_PRECEDENCE_RELATIONAL + 1;
            break;
        case PP_TT_BAR:
            lb = PP_PRECEDENCE_BITWISE_OR;
            rb = PP_PRECEDENCE_BITWISE_OR + 1;
            break;
        case PP_TT_CARET:
            lb = PP_PRECEDENCE_BITWISE_XOR;
            rb = PP_PRECEDENCE_BITWISE_XOR + 1;
            break;
        case PP_TT_AMPERSAND:
            lb = PP_PRECEDENCE_BITWISE_AND;
            rb = PP_PRECEDENCE_BITWISE_AND + 1;
            break;
        case PP_TT_LESS_LESS:
        case PP_TT_GREATER_GREATER:
            lb = PP_PRECEDENCE_BITSHIFT;
            rb = PP_PRECEDENCE_BITSHIFT + 1;
            break;
        case PP_TT_PLUS:
        case PP_TT_MINUS:
            lb = PP_PRECEDENCE_ADDITIVE;
            rb = PP_PRECEDENCE_ADDITIVE + 1;
            break;
        case PP_TT_STAR:
        case PP_TT_SLASH:
        case PP_TT_PERCENT:
            lb = PP_PRECEDENCE_MULTIPLICATIVE;
            rb = PP_PRECEDENCE_MULTIPLICATIVE + 1;
            break;
        case PP_TT_AS:
            lb = PP_PRECEDENCE_CAST;
            rb = PP_PRECEDENCE_CAST + 1;
            break;
        case PP_TT_PERIOD:
        case PP_TT_LEFT_PARENTHESIS:
        case PP_TT_LEFT_SQUARE_BRACKET:
            lb = PP_PRECEDENCE_CALL_ACCESS;
            rb = 0;
            break;
        default:
            return 0;
    }
    if (left_bp) *left_bp = lb;
    if (right_bp) *right_bp = rb;
    return 1;
}

static PP_Token* pp_parser_consume_token(PP_Parser* parser, PP_TokenType expected_type) {
    if (!parser) return NULL;
    if (parser->idx >= parser->n_tokens) {
        pp_error_list_push_error(parser->error_list, parser->filename, 0, 0, "Out of tokens");
        return NULL;
    }
    PP_Token* t = &parser->tokens[parser->idx];
    if (t->token_type == expected_type) {
        parser->idx++;
        return t;
    } else {
        const char* expected_name = pp_token_to_human_name(expected_type);
        const char* got_name = pp_token_to_human_name(t->token_type);
        pp_error_list_push_error(parser->error_list, parser->filename, t->line, t->column,
                                 "Expected token %s but got %s", expected_name ? expected_name : "unknown",
                                 got_name ? got_name : "unknown");
        parser->idx++;
        return NULL;
    }
}

static bool pp_parser_is_sync_token(PP_TokenType token_type) {
    switch (token_type) {
        case PP_TT_IF:
        case PP_TT_FUNC:
        case PP_TT_LAMBDA:
        case PP_TT_RETURN:
        case PP_TT_FOR:
        case PP_TT_WHILE:
        case PP_TT_TYPEDEF:
        case PP_TT_NEWTYPE:
        case PP_TT_SEMICOLON:
        case PP_TT_RIGHT_CURLY_BRACKET:
            return true;
        default:
            return false;
    }
}

static int pp_parser_sync(PP_Parser* parser) {
    if (!parser) return 0;

    if (pp_parser_peek_token_type(parser, 0) == PP_TT_EOF) { return 1; }

    if (pp_parser_is_sync_token(pp_parser_peek_token_type(parser, 0))) { parser->idx++; }

    while (parser->idx < parser->n_tokens && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        PP_TokenType peeked = pp_parser_peek_token_type(parser, 0);
        if (pp_parser_is_sync_token(peeked)) {
            if (peeked == PP_TT_SEMICOLON || peeked == PP_TT_RIGHT_CURLY_BRACKET) { parser->idx++; }
            return 1;
        }
        parser->idx++;
    }

    return 1;
}

static PP_AST_Node* pp_parser_create_program_node(PP_Parser* parser) {
    PP_AST_Node* program = NULL;
    PP_AST_Node** decls = NULL;
    PP_AST_Node* decl = NULL;
    unsigned int decl_count = 0;
    unsigned int capacity = 0;

    NULL_POINTER_ERROR_HANDLE(parser);

    program = pp_ast_node_create(PP_AST_ProgramNode, 0, 0);
    NULL_POINTER_ERROR_HANDLE(program);

    capacity = AST_VECTOR_INIT_CAP;
    decls = (PP_AST_Node**)calloc(capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(decls);

    while (parser->idx < parser->n_tokens) {
        PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
        if (tt == PP_TT_EOF) break;
        decl = NULL;
        switch (tt) {
            case PP_TT_EXTERN: {
                // Look ahead to see if next token is FUNC or LAMBDA for extern declaration
                PP_TokenType next_tt = pp_parser_peek_token_type(parser, 1);
                if (next_tt == PP_TT_FUNC) {
                    // consume EXTERN
                    pp_parser_consume_token(parser, PP_TT_EXTERN);
                    decl = pp_parser_create_extern_function_declaration(parser);
                } else if (next_tt == PP_TT_LAMBDA) {
                    pp_parser_consume_token(parser, PP_TT_EXTERN);
                    decl = pp_parser_create_extern_lambda_declaration(parser);
                } else {
                    pp_error_list_push_error(parser->error_list, parser->filename, pp_parser_get_line(parser),
                                             pp_parser_get_col(parser), "Expected 'func' or 'lambda' after 'extern'");
                    pp_parser_sync(parser);
                }
                break;
            }
            case PP_TT_FUNC:
                decl = pp_parser_create_function_declaration(parser);
                break;
            case PP_TT_LAMBDA:
                decl = pp_parser_create_lambda_declaration(parser);
                break;
            case PP_TT_STRUCT:
                decl = pp_parser_create_struct_declaration(parser);
                break;
            case PP_TT_TYPEDEF:
                decl = pp_parser_create_typedef_declaration(parser);
                break;
            case PP_TT_NEWTYPE:
                decl = pp_parser_create_newtype_declaration(parser);
                break;
            case PP_TT_LET:
                decl = pp_parser_create_global_variable_declaration(parser);
                break;
            default:
                pp_error_list_push_error(parser->error_list, parser->filename, pp_parser_get_line(parser),
                                         pp_parser_get_col(parser), "Unexpected token at global scope.");
                pp_parser_sync(parser);
                continue;
        }
        if (!decl) {
            pp_parser_sync(parser);
            continue;
        }
        if (decl_count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_Node** tmp = (PP_AST_Node**)realloc(decls, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            decls = tmp;
            capacity = new_cap;
        }
        decls[decl_count] = decl;
        decl = NULL;
        decl_count++;
    }

    program->program.decls = decls;
    program->program.decl_count = decl_count;
    program->program.capacity = capacity;
    return program;

error_cleanup:
    pp_ast_root_free(decl);
    if (decls) {
        for (unsigned int i = 0; i < decl_count; i++) { pp_ast_root_free(decls[i]); }
    }
    free(decls);
    pp_ast_root_free(program);
    return NULL;
}

static PP_AST_Node* pp_parser_create_identifier(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_Token* tok = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);

    {
        tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(tok);
    }
    node = pp_ast_node_create(PP_AST_IdentifierNode, tok->line, tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->identifier.identifier = tok->lexeme; /* use string pool pointer */
    tok = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_primary(PP_Parser* parser) {
    NULL_POINTER_ERROR_HANDLE(parser);
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    if (tt == PP_TT_IDENTIFIER && pp_parser_peek_token_type(parser, 1) == PP_TT_LEFT_CURLY_BRACKET) {
        return pp_parser_create_struct_initializer(parser);
    }
    switch (tt) {
        case PP_TT_IDENTIFIER:
            return pp_parser_create_identifier(parser);
        case PP_TT_INTEGER_LITERAL:
        case PP_TT_UNSIGNED_INTEGER_LITERAL:
        case PP_TT_FLOAT_LITERAL:
        case PP_TT_TRUE:
        case PP_TT_FALSE:
        case PP_TT_STRING_LITERAL:
        case PP_TT_STATIC_STRING_LITERAL:
            return pp_parser_create_literal(parser);
        case PP_TT_LEFT_PARENTHESIS: {
            pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
            PP_AST_Node* expr = NULL;
            expr = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
            pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
            return expr;
        }
        case PP_TT_LEFT_SQUARE_BRACKET:
            return pp_parser_create_collection_initializer(parser);
        default:
            return NULL;
    }
error_cleanup:
    return NULL;
}

static PP_AST_Node* pp_parser_create_literal(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_Token* tok = NULL;
    PP_TokenType tt;
    NULL_POINTER_ERROR_HANDLE(parser);

    tt = pp_parser_peek_token_type(parser, 0);
    {
        tok = pp_parser_consume_token(parser, tt);
        NULL_POINTER_ERROR_HANDLE(tok);
    }
    node = pp_ast_node_create(PP_AST_LiteralNode, tok->line, tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->literal.literal_type = tt;
    node->literal.lexeme = tok->lexeme; /* string pool */
    node->literal.decoration = NULL;
    tok = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_unary(PP_Parser* parser) {
    PP_AST_Node* operand = NULL;
    PP_AST_Node* node = NULL;
    PP_Token* op_tok = NULL;
    unsigned int line = 0;
    unsigned int col = 0;
    PP_TokenType tt;
    NULL_POINTER_ERROR_HANDLE(parser);

    tt = pp_parser_peek_token_type(parser, 0);
    if (tt == PP_TT_MINUS || tt == PP_TT_EXCLAMATION || tt == PP_TT_HASH) {
        {
            op_tok = pp_parser_consume_token(parser, tt);
            NULL_POINTER_ERROR_HANDLE(op_tok);
            line = op_tok->line;
            col = op_tok->column;
        }
        operand = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_UNARY);
        NULL_POINTER_ERROR_HANDLE(operand);
        node = pp_ast_node_create(PP_AST_UnaryExpressionNode, line, col);
        NULL_POINTER_ERROR_HANDLE(node);
        node->unary_expression.op = tt;
        node->unary_expression.operand = operand;
        node->unary_expression.decoration = NULL;
        operand = NULL;
        return node;
    }
    return pp_parser_create_primary(parser);
error_cleanup:
    pp_ast_root_free(operand);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_parse_collection_type(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* inner_type = NULL;
    PP_AST_Node* size_expr = NULL;
    const PP_Token* l_bracket_tok = NULL;
    const PP_Token* colon_tok = NULL;
    const PP_Token* r_bracket_tok = NULL;
    const PP_Token* base_tok = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);

    if (pp_parser_peek_token_type(parser, 0) == PP_TT_LEFT_SQUARE_BRACKET) {
        {
            l_bracket_tok = pp_parser_consume_token(parser, PP_TT_LEFT_SQUARE_BRACKET);
            NULL_POINTER_ERROR_HANDLE(l_bracket_tok);
        }
        inner_type = pp_parser_create_type(parser);
        NULL_POINTER_ERROR_HANDLE(inner_type);
        size_expr = NULL;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COLON) {
            {
                colon_tok = pp_parser_consume_token(parser, PP_TT_COLON);
                NULL_POINTER_ERROR_HANDLE(colon_tok);
            }
            size_expr = pp_parser_create_expression_or_assignment(parser);
            NULL_POINTER_ERROR_HANDLE(size_expr);
        }
        {
            r_bracket_tok = pp_parser_consume_token(parser, PP_TT_RIGHT_SQUARE_BRACKET);
            NULL_POINTER_ERROR_HANDLE(r_bracket_tok);
        }
        if (size_expr) {
            node = pp_ast_node_create(PP_AST_TypeArrayNode, l_bracket_tok->line, l_bracket_tok->column);
            NULL_POINTER_ERROR_HANDLE(node);
            node->type_array.element_type = inner_type;
            inner_type = NULL;
            node->type_array.size_expr = size_expr;
            size_expr = NULL;
        } else {
            node = pp_ast_node_create(PP_AST_TypeListNode, l_bracket_tok->line, l_bracket_tok->column);
            NULL_POINTER_ERROR_HANDLE(node);
            node->type_list.element_type = inner_type;
            inner_type = NULL;
        }
    } else {
        PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
        unsigned int line = pp_parser_get_line(parser);
        unsigned int col = pp_parser_get_col(parser);
        int kind = PP_AST_TYPE_KIND_IDENTIFIER;
        switch (tt) {
            case PP_TT_VOID:
                kind = PP_AST_TYPE_KIND_VOID;
                break;
            case PP_TT_INT8:
                kind = PP_AST_TYPE_KIND_INT8;
                break;
            case PP_TT_UINT8:
                kind = PP_AST_TYPE_KIND_UINT8;
                break;
            case PP_TT_INT16:
                kind = PP_AST_TYPE_KIND_INT16;
                break;
            case PP_TT_UINT16:
                kind = PP_AST_TYPE_KIND_UINT16;
                break;
            case PP_TT_INT32:
                kind = PP_AST_TYPE_KIND_INT32;
                break;
            case PP_TT_UINT32:
                kind = PP_AST_TYPE_KIND_UINT32;
                break;
            case PP_TT_INT64:
                kind = PP_AST_TYPE_KIND_INT64;
                break;
            case PP_TT_UINT64:
                kind = PP_AST_TYPE_KIND_UINT64;
                break;
            case PP_TT_INT128:
                kind = PP_AST_TYPE_KIND_INT128;
                break;
            case PP_TT_UINT128:
                kind = PP_AST_TYPE_KIND_UINT128;
                break;
            case PP_TT_FLOAT:
                kind = PP_AST_TYPE_KIND_FLOAT;
                break;
            case PP_TT_BOOL:
                kind = PP_AST_TYPE_KIND_BOOL;
                break;
            case PP_TT_STRING:
                kind = PP_AST_TYPE_KIND_STRING;
                break;
            case PP_TT_IDENTIFIER:
                kind = PP_AST_TYPE_KIND_IDENTIFIER;
                break;
            default:
                pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected type name");
                goto error_cleanup;
        }
        {
            base_tok = pp_parser_consume_token(parser, tt);
            NULL_POINTER_ERROR_HANDLE(base_tok);
        }
        node = pp_ast_node_create(PP_AST_TypeBaseNode, base_tok->line, base_tok->column);
        NULL_POINTER_ERROR_HANDLE(node);
        node->type_base.kind = kind;
        if (tt == PP_TT_IDENTIFIER) {
            node->type_base.identifier_name = base_tok->lexeme;
        } else {
            node->type_base.identifier_name = NULL;
        }
    }
    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(inner_type);
    pp_ast_root_free(size_expr);
    return NULL;
}

static PP_AST_Node* pp_parser_create_type(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* wrapper = NULL;
    const PP_Token* const_tok = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);
    bool is_const = false;
    unsigned int line = 0;
    unsigned int col = 0;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_CONST) {
        {
            const_tok = pp_parser_consume_token(parser, PP_TT_CONST);
            NULL_POINTER_ERROR_HANDLE(const_tok);
        }
        is_const = true;
        line = const_tok->line;
        col = const_tok->column;
    } else {
        line = pp_parser_get_line(parser);
        col = pp_parser_get_col(parser);
    }
    node = pp_parser_parse_collection_type(parser);
    NULL_POINTER_ERROR_HANDLE(node);
    bool is_ref = false;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_AMPERSAND) {
        pp_parser_consume_token(parser, PP_TT_AMPERSAND);
        is_ref = true;
    }
    wrapper = pp_ast_node_create(PP_AST_TypeExpressionNode, line, col);
    NULL_POINTER_ERROR_HANDLE(wrapper);
    wrapper->type_expression.is_const = is_const;
    wrapper->type_expression.is_reference = is_ref;
    wrapper->type_expression.type_node = node;
    node = NULL; /* ownership transferred */
    return wrapper;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(wrapper);
    return NULL;
}

static PP_AST_Node* pp_parser_create_expression_or_assignment(PP_Parser* parser) {
    PP_AST_Node* lhs = NULL;
    PP_AST_Node* rhs = NULL;
    PP_AST_Node* node = NULL;
    PP_Token* op_tok = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);
    lhs = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
    NULL_POINTER_ERROR_HANDLE(lhs);
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    if (!pp_token_type_is_assignment(tt)) { return lhs; }
    op_tok = pp_parser_consume_token(parser, tt);
    NULL_POINTER_ERROR_HANDLE(op_tok);
    rhs = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
    NULL_POINTER_ERROR_HANDLE(rhs);
    node = pp_ast_node_create(PP_AST_AssignmentNode, op_tok->line, op_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->assignment.lhs = lhs;
    node->assignment.op = tt;
    node->assignment.rhs = rhs;
    lhs = NULL;
    rhs = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(lhs);
    pp_ast_root_free(rhs);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_statement(PP_Parser* parser) {
    PP_AST_Node* expr = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    switch (tt) {
        case PP_TT_LEFT_CURLY_BRACKET:
            return pp_parser_create_block(parser);
        case PP_TT_RETURN:
            return pp_parser_create_return_statement(parser);
        case PP_TT_IF:
            return pp_parser_create_if_statement(parser);
        case PP_TT_WHILE:
            return pp_parser_create_while_statement(parser);
        case PP_TT_FOR:
            return pp_parser_create_for_statement(parser);
        case PP_TT_LET:
            return pp_parser_create_variable_declaration(parser);
        default: {
            /* Expression statement */
            expr = pp_parser_create_expression_or_assignment(parser);
            NULL_POINTER_ERROR_HANDLE(expr);
            if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
                pp_error_list_push_error(parser->error_list, parser->filename, pp_parser_get_line(parser),
                                         pp_parser_get_col(parser), "Expected ';' after expression statement");
                pp_parser_sync(parser);
            }
            return expr;
        }
    }
error_cleanup:
    pp_ast_root_free(expr);
    return NULL;
}

static PP_AST_Node* pp_parser_create_block(PP_Parser* parser) {
    PP_AST_Node* block = NULL;
    PP_AST_Node** stmts = NULL;
    PP_AST_Node* stmt = NULL;
    unsigned int capacity = 0;
    unsigned int count = 0;
    unsigned int line = 0;
    unsigned int col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    // left curly brace
    {
        PP_Token* lbrace_tok = pp_parser_consume_token(parser, PP_TT_LEFT_CURLY_BRACKET);
        NULL_POINTER_ERROR_HANDLE(lbrace_tok);
        line = lbrace_tok->line;
        col = lbrace_tok->column;
    }

    block = pp_ast_node_create(PP_AST_StatementListNode, line, col);
    NULL_POINTER_ERROR_HANDLE(block);

    capacity = AST_VECTOR_INIT_CAP;
    count = 0;
    stmts = (PP_AST_Node**)calloc(capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(stmts);

    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_CURLY_BRACKET &&
           pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        stmt = pp_parser_create_statement(parser);
        if (!stmt) break;
        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_Node** tmp = (PP_AST_Node**)realloc(stmts, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            stmts = tmp;
            capacity = new_cap;
        }
        stmts[count] = stmt;
        stmt = NULL;
        count++;
    }

    // right curly brace
    {
        PP_Token* rbrace_tok = pp_parser_consume_token(parser, PP_TT_RIGHT_CURLY_BRACKET);
        NULL_POINTER_ERROR_HANDLE(rbrace_tok);
    }

    block->statement_list.stmts = stmts;
    block->statement_list.stmt_count = count;
    block->statement_list.capacity = capacity;
    return block;
error_cleanup:
    pp_ast_root_free(stmt);
    if (stmts) {
        for (unsigned int i = 0; i < count; i++) pp_ast_root_free(stmts[i]);
    }
    free(stmts);
    pp_ast_root_free(block);
    return NULL;
}

static PP_AST_Node* pp_parser_create_expression_ext(PP_Parser* parser, unsigned char min_bp) {
    PP_AST_Node* lhs = NULL;
    PP_AST_Node* rhs = NULL;
    PP_AST_Node* node = NULL;
    PP_AST_Node* index_expr = NULL;
    PP_AST_Node* type_node = NULL;
    PP_AST_Node** arguments = NULL;
    PP_AST_Node* arg = NULL;
    PP_Token* op_tok = NULL;
    PP_Token* member_tok = NULL;
    unsigned int arg_count = 0;
    unsigned int arg_capacity = 0;
    unsigned char left_bp = 0;
    unsigned char right_bp = 0;
    PP_TokenType op_type;
    NULL_POINTER_ERROR_HANDLE(parser);

    lhs = pp_parser_create_unary(parser);
    NULL_POINTER_ERROR_HANDLE(lhs);

    while (1) {
        op_type = pp_parser_peek_token_type(parser, 0);
        if (!pp_parser_infix_binding_power(op_type, &left_bp, &right_bp)) break;
        if (left_bp < min_bp) break;

        {
            op_tok = pp_parser_consume_token(parser, op_type);
            NULL_POINTER_ERROR_HANDLE(op_tok);
        }

        /* Handle postfix call / index / member */
        if (op_type == PP_TT_LEFT_PARENTHESIS || op_type == PP_TT_LEFT_SQUARE_BRACKET || op_type == PP_TT_PERIOD) {
            if (op_type == PP_TT_LEFT_PARENTHESIS) {
                arg_count = 0;
                arg_capacity = AST_VECTOR_INIT_CAP;
                arguments = (PP_AST_Node**)calloc(arg_capacity, sizeof(PP_AST_Node*));
                NULL_POINTER_ERROR_HANDLE(arguments);
                while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_PARENTHESIS &&
                       pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
                    arg = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
                    NULL_POINTER_ERROR_HANDLE(arg);
                    if (arg_count >= arg_capacity) {
                        unsigned int new_cap = arg_capacity * 2;
                        PP_AST_Node** tmp = (PP_AST_Node**)realloc(arguments, new_cap * sizeof(PP_AST_Node*));
                        NULL_POINTER_ERROR_HANDLE(tmp);
                        arguments = tmp;
                        arg_capacity = new_cap;
                    }
                    arguments[arg_count] = arg;
                    arg = NULL;
                    arg_count++;
                    if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
                        {
                            PP_Token* comma_tok = pp_parser_consume_token(parser, PP_TT_COMMA);
                            NULL_POINTER_ERROR_HANDLE(comma_tok);
                        }
                    } else {
                        break;
                    }
                }
                {
                    PP_Token* right_paren = pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
                    NULL_POINTER_ERROR_HANDLE(right_paren);
                }
                node = pp_ast_node_create(PP_AST_FunctionCallNode, op_tok->line, op_tok->column);
                NULL_POINTER_ERROR_HANDLE(node);
                node->function_call.callee = lhs;
                node->function_call.arguments = arguments;
                node->function_call.arg_count = arg_count;
                node->function_call.arg_capacity = arg_capacity;
                node->function_call.decoration = NULL;
                lhs = node;
                arguments = NULL;
            } else if (op_type == PP_TT_LEFT_SQUARE_BRACKET) {
                index_expr = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
                NULL_POINTER_ERROR_HANDLE(index_expr);
                {
                    PP_Token* right_bracket = pp_parser_consume_token(parser, PP_TT_RIGHT_SQUARE_BRACKET);
                    NULL_POINTER_ERROR_HANDLE(right_bracket);
                }
                node = pp_ast_node_create(PP_AST_ArrayListAccessNode, op_tok->line, op_tok->column);
                NULL_POINTER_ERROR_HANDLE(node);
                node->array_list_access.expr = lhs;
                node->array_list_access.index = index_expr;
                node->array_list_access.decoration = NULL;
                lhs = node;
                index_expr = NULL;
            } else if (op_type == PP_TT_PERIOD) {
                {
                    member_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
                    NULL_POINTER_ERROR_HANDLE(member_tok);
                }
                node = pp_ast_node_create(PP_AST_MemberAccessNode, op_tok->line, op_tok->column);
                NULL_POINTER_ERROR_HANDLE(node);
                node->member_access.object = lhs;
                node->member_access.member = member_tok->lexeme;
                node->member_access.decoration = NULL;
                lhs = node;
                member_tok = NULL;
            }
            continue;
        }

        if (op_type == PP_TT_AS) {
            /* Cast expression */
            type_node = pp_parser_create_type(parser);
            NULL_POINTER_ERROR_HANDLE(type_node);
            node = pp_ast_node_create(PP_AST_CastExpressionNode, op_tok->line, op_tok->column);
            NULL_POINTER_ERROR_HANDLE(node);
            node->cast_expression.expression = lhs;
            node->cast_expression.type_expr = type_node;
            node->cast_expression.decoration = NULL;
            lhs = node;
            type_node = NULL;
            continue;
        }

        rhs = pp_parser_create_expression_ext(parser, right_bp ? right_bp : 0);
        NULL_POINTER_ERROR_HANDLE(rhs);
        node = pp_ast_node_create(PP_AST_BinaryExpressionNode, op_tok->line, op_tok->column);
        NULL_POINTER_ERROR_HANDLE(node);
        node->binary_expression.op = op_type;
        node->binary_expression.left = lhs;
        node->binary_expression.right = rhs;
        node->binary_expression.decoration = NULL;
        lhs = node;
        rhs = NULL;
    }
    return lhs;
error_cleanup:
    pp_ast_root_free(lhs);
    pp_ast_root_free(rhs);
    pp_ast_root_free(node);
    pp_ast_root_free(index_expr);
    pp_ast_root_free(type_node);
    pp_ast_root_free(arg);
    if (arguments) {
        for (unsigned int i = 0; i < arg_count; i++) { pp_ast_root_free(arguments[i]); }
    }
    free(arguments);
    return NULL;
}

static PP_AST_Node* pp_parser_create_return_statement(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* expr = NULL;
    PP_Token* return_tok = NULL;
    PP_Token* semicolon = NULL;
    unsigned int line = 0;
    unsigned int col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    // return token
    {
        return_tok = pp_parser_consume_token(parser, PP_TT_RETURN);
        NULL_POINTER_ERROR_HANDLE(return_tok);
        line = return_tok->line;
        col = return_tok->column;
    }

    // expression (optional)
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_SEMICOLON) {
        expr = NULL;
    } else {
        expr = pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(expr);
    }

    // semicolon
    {
        semicolon = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semicolon);
    }

    node = pp_ast_node_create(PP_AST_ReturnStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);

    node->return_statement.expr = expr;
    expr = NULL;

    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(expr);
    return NULL;
}

static PP_AST_Node* pp_parser_create_if_statement(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* cond = NULL;
    PP_AST_Node* then_branch = NULL;
    PP_AST_Node* else_branch = NULL;
    PP_Token* if_tok = NULL;
    PP_Token* left_paren = NULL;
    PP_Token* right_paren = NULL;
    PP_Token* else_tok = NULL;
    unsigned int line = 0;
    unsigned int col = 0;

    NULL_POINTER_ERROR_HANDLE(parser);

    // if token
    {
        if_tok = pp_parser_consume_token(parser, PP_TT_IF);
        NULL_POINTER_ERROR_HANDLE(if_tok);
        line = if_tok->line;
        col = if_tok->column;
    }

    // left parenthesis
    {
        left_paren = pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
        NULL_POINTER_ERROR_HANDLE(left_paren);
    }

    // Condition
    cond = pp_parser_create_expression_or_assignment(parser);
    NULL_POINTER_ERROR_HANDLE(cond);

    // right parenthesis
    {
        right_paren = pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
        NULL_POINTER_ERROR_HANDLE(right_paren);
    }

    // then branch - must be a block per grammar
    then_branch = pp_parser_create_block(parser);
    NULL_POINTER_ERROR_HANDLE(then_branch);

    // else branch (optional)
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_ELSE) {
        // else token
        {
            else_tok = pp_parser_consume_token(parser, PP_TT_ELSE);
            NULL_POINTER_ERROR_HANDLE(else_tok);
        }

        // after else, could be block or if statement
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_LEFT_CURLY_BRACKET) {
            else_branch = pp_parser_create_block(parser);
            NULL_POINTER_ERROR_HANDLE(else_branch);
        } else if (pp_parser_peek_token_type(parser, 0) == PP_TT_IF) {
            else_branch = pp_parser_create_if_statement(parser);
            NULL_POINTER_ERROR_HANDLE(else_branch);
        }
        // else else_branch remains NULL (allowed)
    }

    node = pp_ast_node_create(PP_AST_IfStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);

    node->if_statement.cond = cond;
    cond = NULL;
    node->if_statement.then_branch = then_branch;
    then_branch = NULL;
    node->if_statement.else_branch = else_branch;
    else_branch = NULL;

    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(cond);
    pp_ast_root_free(then_branch);
    pp_ast_root_free(else_branch);
    return NULL;
}

static PP_AST_Node* pp_parser_create_while_statement(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* cond = NULL;
    PP_AST_Node* body = NULL;
    PP_Token* while_tok = NULL;
    PP_Token* left_paren = NULL;
    PP_Token* right_paren = NULL;
    unsigned int line = 0;
    unsigned int col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    // while token
    {
        while_tok = pp_parser_consume_token(parser, PP_TT_WHILE);
        NULL_POINTER_ERROR_HANDLE(while_tok);
        line = while_tok->line;
        col = while_tok->column;
    }

    // left parenthesis
    {
        left_paren = pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
        NULL_POINTER_ERROR_HANDLE(left_paren);
    }

    // condition
    cond = pp_parser_create_expression_or_assignment(parser);
    NULL_POINTER_ERROR_HANDLE(cond);

    // right parenthesis
    {
        right_paren = pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
        NULL_POINTER_ERROR_HANDLE(right_paren);
    }

    // body
    body = pp_parser_create_statement(parser);
    NULL_POINTER_ERROR_HANDLE(body);

    node = pp_ast_node_create(PP_AST_WhileStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);

    node->while_statement.cond = cond;
    cond = NULL;
    node->while_statement.body = body;
    body = NULL;

    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(cond);
    pp_ast_root_free(body);
    return NULL;
}

static PP_AST_Node* pp_parser_create_for_statement(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* init = NULL;
    PP_AST_Node* cond = NULL;
    PP_AST_Node* update = NULL;
    PP_AST_Node* body = NULL;
    PP_Token* for_tok = NULL;
    PP_Token* left_paren = NULL;
    PP_Token* semicolon1 = NULL;
    PP_Token* semicolon2 = NULL;
    PP_Token* right_paren = NULL;
    unsigned int line = 0;
    unsigned int col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    // for token
    for_tok = pp_parser_consume_token(parser, PP_TT_FOR);
    NULL_POINTER_ERROR_HANDLE(for_tok);
    line = for_tok->line;
    col = for_tok->column;

    // left parenthesis
    left_paren = pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
    NULL_POINTER_ERROR_HANDLE(left_paren);

    // init (optional)
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_SEMICOLON) {
        init = pp_parser_peek_token_type(parser, 0) == PP_TT_LET ? pp_parser_create_variable_decl(parser)
                                                                 : pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(init);
    }

    // first semicolon
    semicolon1 = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
    NULL_POINTER_ERROR_HANDLE(semicolon1);

    // cond (optional)
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_SEMICOLON) {
        // Condition is expression only, no assignment
        cond = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
        NULL_POINTER_ERROR_HANDLE(cond);
    }

    // second semicolon
    semicolon2 = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
    NULL_POINTER_ERROR_HANDLE(semicolon2);

    // update (optional)
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_PARENTHESIS) {
        update = pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(update);
    }

    // right parenthesis
    right_paren = pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
    NULL_POINTER_ERROR_HANDLE(right_paren);

    // body
    body = pp_parser_create_statement(parser);
    NULL_POINTER_ERROR_HANDLE(body);

    node = pp_ast_node_create(PP_AST_ForStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);

    node->for_statement.init = init;
    init = NULL;
    node->for_statement.cond = cond;
    cond = NULL;
    node->for_statement.update = update;
    update = NULL;
    node->for_statement.body = body;
    body = NULL;

    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(init);
    pp_ast_root_free(cond);
    pp_ast_root_free(update);
    pp_ast_root_free(body);
    return NULL;
}

static PP_AST_Node* pp_parser_create_param_decl(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* type_expr = NULL;
    PP_Token* ident_tok = NULL;
    PP_Token* colon_colon_tok = NULL;

    NULL_POINTER_ERROR_HANDLE(parser);

    {
        ident_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(ident_tok);
    }

    {
        colon_colon_tok = pp_parser_consume_token(parser, PP_TT_COLON_COLON);
        NULL_POINTER_ERROR_HANDLE(colon_colon_tok);
    }

    type_expr = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(type_expr);

    node = pp_ast_node_create(PP_AST_ParamDeclarationNode, ident_tok->line, ident_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->param_declaration.identifier = (char*)ident_tok->lexeme;
    node->param_declaration.type_expr = type_expr;
    type_expr = NULL;
    ident_tok = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(type_expr);
    return NULL;
}

static PP_AST_Node* pp_parser_create_variable_decl(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* var_decl = NULL;
    PP_AST_Node* initializer = NULL;
    PP_Token* decl_tok = NULL;
    unsigned int line = 0, col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);
    {
        PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
        if (tt != PP_TT_LET) return NULL;
        decl_tok = pp_parser_consume_token(parser, PP_TT_LET);
        NULL_POINTER_ERROR_HANDLE(decl_tok);
        line = decl_tok->line;
        col = decl_tok->column;
    }
    var_decl = pp_parser_create_param_decl(parser);
    NULL_POINTER_ERROR_HANDLE(var_decl);
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_EQUAL) {
        {
            PP_Token* eq_tok = pp_parser_consume_token(parser, PP_TT_EQUAL);
            NULL_POINTER_ERROR_HANDLE(eq_tok);
        }
        initializer = pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(initializer);
    }
    node = pp_ast_node_create(PP_AST_VariableDeclarationNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->variable_declaration.var_decl = var_decl;
    var_decl = NULL;
    node->variable_declaration.initializer = initializer;
    initializer = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(var_decl);
    pp_ast_root_free(initializer);
    return NULL;
}

static PP_AST_Node* pp_parser_create_variable_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);
    node = pp_parser_create_variable_decl(parser);
    NULL_POINTER_ERROR_HANDLE(node);
    {
        PP_Token* semi_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semi_tok);
    }
    return node;
error_cleanup:
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_global_variable_declaration(PP_Parser* parser) {
    return pp_parser_create_variable_declaration(parser);
}

static PP_AST_Node* pp_parser_create_function_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* return_type = NULL;
    PP_AST_Node* body = NULL;
    PP_AST_Node** params = NULL;
    PP_Token* func_tok = NULL;
    const char* name = NULL;
    unsigned int param_count = 0, param_capacity = 0;
    NULL_POINTER_ERROR_HANDLE(parser);
    {
        func_tok = pp_parser_consume_token(parser, PP_TT_FUNC);
        NULL_POINTER_ERROR_HANDLE(func_tok);
    }
    if (!pp_parser_parse_signature(parser, &name, &params, &param_count, &param_capacity, &return_type)) {
        goto error_cleanup;
    }
    body = pp_parser_create_block(parser);
    NULL_POINTER_ERROR_HANDLE(body);
    node = pp_ast_node_create(PP_AST_FunctionDeclarationNode, func_tok->line, func_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->function_declaration.name = name;
    node->function_declaration.params = params;
    params = NULL;
    node->function_declaration.param_count = param_count;
    node->function_declaration.param_capacity = param_capacity;
    node->function_declaration.return_type = return_type;
    return_type = NULL;
    node->function_declaration.body = body;
    body = NULL;
    return node;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < param_count; i++) pp_ast_root_free(params[i]);
    }
    free(params);
    pp_ast_root_free(return_type);
    pp_ast_root_free(body);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_lambda_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* return_type = NULL;
    PP_AST_Node* body = NULL;
    PP_AST_Node** params = NULL;
    PP_Token* lambda_tok = NULL;
    const char* name = NULL;
    unsigned int param_count = 0, param_capacity = 0;
    NULL_POINTER_ERROR_HANDLE(parser);
    {
        lambda_tok = pp_parser_consume_token(parser, PP_TT_LAMBDA);
        NULL_POINTER_ERROR_HANDLE(lambda_tok);
    }
    if (!pp_parser_parse_signature(parser, &name, &params, &param_count, &param_capacity, &return_type)) {
        goto error_cleanup;
    }
    body = pp_parser_create_block(parser);
    NULL_POINTER_ERROR_HANDLE(body);
    node = pp_ast_node_create(PP_AST_LambdaDeclarationNode, lambda_tok->line, lambda_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->function_declaration.name = name;
    node->function_declaration.params = params;
    params = NULL;
    node->function_declaration.param_count = param_count;
    node->function_declaration.param_capacity = param_capacity;
    node->function_declaration.return_type = return_type;
    return_type = NULL;
    node->function_declaration.body = body;
    body = NULL;
    return node;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < param_count; i++) pp_ast_root_free(params[i]);
    }
    free(params);
    pp_ast_root_free(return_type);
    pp_ast_root_free(body);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_extern_function_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* return_type = NULL;
    PP_AST_Node** params = NULL;
    PP_Token* func_tok = NULL;
    const char* name = NULL;
    unsigned int param_count = 0, param_capacity = 0;
    NULL_POINTER_ERROR_HANDLE(parser);
    {
        func_tok = pp_parser_consume_token(parser, PP_TT_FUNC);
        NULL_POINTER_ERROR_HANDLE(func_tok);
    }
    if (!pp_parser_parse_signature(parser, &name, &params, &param_count, &param_capacity, &return_type)) {
        goto error_cleanup;
    }
    {
        PP_Token* semi_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semi_tok);
    }
    node = pp_ast_node_create(PP_AST_ExternFunctionDeclarationNode, func_tok->line, func_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->extern_function_declaration.name = name;
    node->extern_function_declaration.params = params;
    params = NULL;
    node->extern_function_declaration.param_count = param_count;
    node->extern_function_declaration.param_capacity = param_capacity;
    node->extern_function_declaration.return_type = return_type;
    return_type = NULL;
    return node;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < param_count; i++) pp_ast_root_free(params[i]);
    }
    free(params);
    pp_ast_root_free(return_type);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_extern_lambda_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* return_type = NULL;
    PP_AST_Node** params = NULL;
    PP_Token* lambda_tok = NULL;
    const char* name = NULL;
    unsigned int param_count = 0, param_capacity = 0;
    NULL_POINTER_ERROR_HANDLE(parser);
    {
        lambda_tok = pp_parser_consume_token(parser, PP_TT_LAMBDA);
        NULL_POINTER_ERROR_HANDLE(lambda_tok);
    }
    if (!pp_parser_parse_signature(parser, &name, &params, &param_count, &param_capacity, &return_type)) {
        goto error_cleanup;
    }
    {
        PP_Token* semi_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semi_tok);
    }
    node = pp_ast_node_create(PP_AST_ExternLambdaDeclarationNode, lambda_tok->line, lambda_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->extern_lambda_declaration.name = name;
    node->extern_lambda_declaration.params = params;
    params = NULL;
    node->extern_lambda_declaration.param_count = param_count;
    node->extern_lambda_declaration.param_capacity = param_capacity;
    node->extern_lambda_declaration.return_type = return_type;
    return_type = NULL;
    return node;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < param_count; i++) pp_ast_root_free(params[i]);
    }
    free(params);
    pp_ast_root_free(return_type);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_struct_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_Token* struct_tok = NULL;
    PP_Token* name_tok = NULL;
    PP_AST_StructField* fields = NULL;
    unsigned int capacity = 0;
    unsigned int count = 0;
    PP_AST_Node* param_decl = NULL;
    PP_AST_Node* initializer = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);

    {
        struct_tok = pp_parser_consume_token(parser, PP_TT_STRUCT);
        NULL_POINTER_ERROR_HANDLE(struct_tok);
    }
    {
        name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(name_tok);
    }
    {
        PP_Token* left_brace = pp_parser_consume_token(parser, PP_TT_LEFT_CURLY_BRACKET);
        NULL_POINTER_ERROR_HANDLE(left_brace);
    }
    capacity = AST_VECTOR_INIT_CAP;
    count = 0;
    fields = (PP_AST_StructField*)calloc(capacity, sizeof(PP_AST_StructField));
    NULL_POINTER_ERROR_HANDLE(fields);
    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_CURLY_BRACKET &&
           pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        param_decl = pp_parser_create_param_decl(parser);
        if (!param_decl) break;
        initializer = NULL;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_EQUAL) {
            {
                PP_Token* equal_tok = pp_parser_consume_token(parser, PP_TT_EQUAL);
                NULL_POINTER_ERROR_HANDLE(equal_tok);
            }
            initializer = pp_parser_create_expression_or_assignment(parser);
            NULL_POINTER_ERROR_HANDLE(initializer);
        }
        {
            PP_Token* semicolon_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
            NULL_POINTER_ERROR_HANDLE(semicolon_tok);
        }
        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_StructField* tmp = (PP_AST_StructField*)realloc(fields, new_cap * sizeof(PP_AST_StructField));
            NULL_POINTER_ERROR_HANDLE(tmp);
            fields = tmp;
            capacity = new_cap;
        }
        fields[count].field = param_decl;
        fields[count].initializer = initializer;
        param_decl = NULL;
        initializer = NULL;
        count++;
    }
    {
        PP_Token* right_brace = pp_parser_consume_token(parser, PP_TT_RIGHT_CURLY_BRACKET);
        NULL_POINTER_ERROR_HANDLE(right_brace);
    }
    node = pp_ast_node_create(PP_AST_StructDeclarationNode, struct_tok->line, struct_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->struct_declaration.name = name_tok->lexeme;
    node->struct_declaration.fields = fields;
    node->struct_declaration.field_count = count;
    node->struct_declaration.field_capacity = capacity;
    fields = NULL;
    name_tok = NULL;
    struct_tok = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(param_decl);
    pp_ast_root_free(initializer);
    if (fields) {
        for (unsigned int i = 0; i < count; i++) {
            pp_ast_root_free(fields[i].field);
            pp_ast_root_free(fields[i].initializer);
        }
    }
    free(fields);
    return NULL;
}

static PP_AST_Node* pp_parser_create_typedef_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* base_type = NULL;
    PP_Token* typedef_tok = NULL;
    PP_Token* name_tok = NULL;
    PP_Token* semicolon_tok = NULL;
    NULL_POINTER_ERROR_HANDLE(parser);

    {
        typedef_tok = pp_parser_consume_token(parser, PP_TT_TYPEDEF);
        NULL_POINTER_ERROR_HANDLE(typedef_tok);
    }
    base_type = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(base_type);
    {
        name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(name_tok);
    }
    {
        semicolon_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semicolon_tok);
    }
    node = pp_ast_node_create(PP_AST_TypedefDeclarationNode, typedef_tok->line, typedef_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->typedef_declaration.base_type = base_type;
    node->typedef_declaration.identifier = name_tok->lexeme;
    base_type = NULL;
    name_tok = NULL;
    typedef_tok = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(base_type);
    pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_newtype_declaration(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* base_type = NULL;
    PP_Token* newtype_tok = NULL;
    PP_Token* name_tok = NULL;
    PP_Token* semicolon_tok = NULL;
    unsigned int line = 0;
    unsigned int col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    {
        newtype_tok = pp_parser_consume_token(parser, PP_TT_NEWTYPE);
        NULL_POINTER_ERROR_HANDLE(newtype_tok);
        line = newtype_tok->line;
        col = newtype_tok->column;
    }

    base_type = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(base_type);

    {
        name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(name_tok);
    }

    {
        semicolon_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semicolon_tok);
    }

    node = pp_ast_node_create(PP_AST_NewtypeDeclarationNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->newtype_declaration.base_type = base_type;
    node->newtype_declaration.identifier = name_tok->lexeme;
    base_type = NULL;
    name_tok = NULL;

    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(base_type);
    return NULL;
}

static PP_AST_Node* pp_parser_create_struct_initializer(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_Token* ident_tok = NULL;
    PP_AST_StructInitEntry* entries = NULL;
    PP_AST_Node* member_id_node = NULL;
    PP_AST_Node* member_val = NULL;
    unsigned int capacity = 0;
    unsigned int count = 0;
    unsigned int line = 0;
    unsigned int col = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    {
        ident_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(ident_tok);
        line = ident_tok->line;
        col = ident_tok->column;
    }

    {
        PP_Token* left_brace = pp_parser_consume_token(parser, PP_TT_LEFT_CURLY_BRACKET);
        NULL_POINTER_ERROR_HANDLE(left_brace);
    }

    node = pp_ast_node_create(PP_AST_StructInitializerNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->struct_initializer.identifier = ident_tok->lexeme;

    capacity = AST_VECTOR_INIT_CAP;
    count = 0;
    entries = (PP_AST_StructInitEntry*)calloc(capacity, sizeof(PP_AST_StructInitEntry));
    NULL_POINTER_ERROR_HANDLE(entries);

    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_CURLY_BRACKET &&
           pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        {
            PP_Token* period_tok = pp_parser_consume_token(parser, PP_TT_PERIOD);
            NULL_POINTER_ERROR_HANDLE(period_tok);
        }
        PP_Token* member_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(member_tok);
        {
            PP_Token* equal_tok = pp_parser_consume_token(parser, PP_TT_EQUAL);
            NULL_POINTER_ERROR_HANDLE(equal_tok);
        }
        member_val = pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(member_val);

        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_StructInitEntry* tmp =
                (PP_AST_StructInitEntry*)realloc(entries, new_cap * sizeof(PP_AST_StructInitEntry));
            NULL_POINTER_ERROR_HANDLE(tmp);
            entries = tmp;
            capacity = new_cap;
        }
        member_id_node = pp_ast_node_create(PP_AST_IdentifierNode, member_tok->line, member_tok->column);
        NULL_POINTER_ERROR_HANDLE(member_id_node);
        member_id_node->identifier.identifier = member_tok->lexeme;
        entries[count].member_identifier = member_id_node;
        member_id_node = NULL;
        entries[count].member_value = member_val;
        member_val = NULL;
        count++;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
            {
                PP_Token* comma_tok = pp_parser_consume_token(parser, PP_TT_COMMA);
                NULL_POINTER_ERROR_HANDLE(comma_tok);
            }
        }
    }

    {
        PP_Token* right_brace = pp_parser_consume_token(parser, PP_TT_RIGHT_CURLY_BRACKET);
        NULL_POINTER_ERROR_HANDLE(right_brace);
    }

    node->struct_initializer.entries = entries;
    node->struct_initializer.entry_count = count;
    node->struct_initializer.entry_capacity = capacity;
    entries = NULL;
    ident_tok = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(member_id_node);
    pp_ast_root_free(member_val);
    pp_ast_root_free(node);
    if (entries) {
        for (unsigned int i = 0; i < count; i++) {
            pp_ast_root_free(entries[i].member_identifier);
            pp_ast_root_free(entries[i].member_value);
        }
    }
    free(entries);
    return NULL;
}

static PP_AST_Node* pp_parser_create_collection_initializer(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* type_expr = NULL;
    PP_AST_Node* size_expr = NULL;
    PP_AST_Node** elements = NULL;
    PP_AST_Node* elem = NULL;
    unsigned int capacity = 0;
    unsigned int count = 0;
    NULL_POINTER_ERROR_HANDLE(parser);

    {
        PP_Token* lbrack = pp_parser_consume_token(parser, PP_TT_LEFT_SQUARE_BRACKET);
        NULL_POINTER_ERROR_HANDLE(lbrack);
    }

    type_expr = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(type_expr);

    size_expr = NULL;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_COLON) {
        {
            PP_Token* colon_tok = pp_parser_consume_token(parser, PP_TT_COLON);
            NULL_POINTER_ERROR_HANDLE(colon_tok);
        }
        size_expr = pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(size_expr);
    }

    {
        PP_Token* semicolon_tok = pp_parser_consume_token(parser, PP_TT_SEMICOLON);
        NULL_POINTER_ERROR_HANDLE(semicolon_tok);
    }

    capacity = AST_VECTOR_INIT_CAP;
    count = 0;
    elements = (PP_AST_Node**)calloc(capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(elements);

    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_SQUARE_BRACKET &&
           pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        elem = pp_parser_create_expression_or_assignment(parser);
        NULL_POINTER_ERROR_HANDLE(elem);
        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_Node** tmp = (PP_AST_Node**)realloc(elements, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            elements = tmp;
            capacity = new_cap;
        }
        elements[count] = elem;
        elem = NULL;
        count++;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
            {
                PP_Token* comma_tok = pp_parser_consume_token(parser, PP_TT_COMMA);
                NULL_POINTER_ERROR_HANDLE(comma_tok);
            }
        }
    }

    {
        PP_Token* rbrack = pp_parser_consume_token(parser, PP_TT_RIGHT_SQUARE_BRACKET);
        NULL_POINTER_ERROR_HANDLE(rbrack);
    }

    node = pp_ast_node_create(PP_AST_CollectionInitializerNode, type_expr->line, type_expr->col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->collection_initializer.type_expr = type_expr;
    node->collection_initializer.size_expr = size_expr;
    node->collection_initializer.elements = elements;
    node->collection_initializer.element_count = count;
    node->collection_initializer.element_capacity = capacity;
    type_expr = NULL;
    size_expr = NULL;
    elements = NULL;
    return node;
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(type_expr);
    pp_ast_root_free(size_expr);
    pp_ast_root_free(elem);
    if (elements) {
        for (unsigned int i = 0; i < count; i++) { pp_ast_root_free(elements[i]); }
    }
    free(elements);
    return NULL;
}

static int pp_parser_parse_param_list(PP_Parser* parser, PP_AST_Node*** out_params, unsigned int* out_count,
                                      unsigned int* out_capacity) {
    PP_AST_Node** params = NULL;
    PP_AST_Node* param = NULL;
    PP_AST_Node** tmp = NULL;
    unsigned int new_cap = 0;
    if (parser == NULL || !out_params || !out_count || !out_capacity) return 0;
    *out_count = 0;
    *out_capacity = AST_VECTOR_INIT_CAP;
    params = (PP_AST_Node**)calloc(*out_capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(params);
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_RIGHT_PARENTHESIS) {
        *out_params = params;
        return 1;
    }
    while (1) {
        param = pp_parser_create_param_decl(parser);
        NULL_POINTER_ERROR_HANDLE(param);
        if (*out_count >= *out_capacity) {
            new_cap = *out_capacity * 2;
            tmp = (PP_AST_Node**)realloc(params, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            params = tmp;
            *out_capacity = new_cap;
        }
        params[*out_count] = param;
        (*out_count)++;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
            {
                PP_Token* comma_tok = pp_parser_consume_token(parser, PP_TT_COMMA);
                NULL_POINTER_ERROR_HANDLE(comma_tok);
            }
            continue;
        }
        break;
    }
    *out_params = params;
    return 1;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < *out_count; i++) pp_ast_root_free(params[i]);
    }
    free(params);
    return 0;
}

static int pp_parser_parse_signature(PP_Parser* parser, const char** out_name, PP_AST_Node*** out_params,
                                     unsigned int* out_param_count, unsigned int* out_param_capacity,
                                     PP_AST_Node** out_return_type) {
    PP_Token* name_tok = NULL;
    PP_Token* colon_colon_tok = NULL;
    PP_Token* left_paren_tok = NULL;
    PP_Token* right_paren_tok = NULL;
    PP_Token* arrow_tok = NULL;
    PP_AST_Node* return_type = NULL;
    PP_AST_Node** params = NULL;
    unsigned int param_count = 0;
    unsigned int param_capacity = 0;
    NULL_POINTER_ERROR_HANDLE(parser);
    NULL_POINTER_ERROR_HANDLE(out_name);
    NULL_POINTER_ERROR_HANDLE(out_params);
    NULL_POINTER_ERROR_HANDLE(out_param_count);
    NULL_POINTER_ERROR_HANDLE(out_param_capacity);
    NULL_POINTER_ERROR_HANDLE(out_return_type);
    {
        name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        NULL_POINTER_ERROR_HANDLE(name_tok);
    }
    {
        colon_colon_tok = pp_parser_consume_token(parser, PP_TT_COLON_COLON);
        NULL_POINTER_ERROR_HANDLE(colon_colon_tok);
    }
    {
        left_paren_tok = pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
        NULL_POINTER_ERROR_HANDLE(left_paren_tok);
    }
    if (!pp_parser_parse_param_list(parser, &params, &param_count, &param_capacity)) { goto error_cleanup; }
    {
        right_paren_tok = pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
        NULL_POINTER_ERROR_HANDLE(right_paren_tok);
    }
    {
        arrow_tok = pp_parser_consume_token(parser, PP_TT_RIGHT_ARROW);
        NULL_POINTER_ERROR_HANDLE(arrow_tok);
    }
    return_type = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(return_type);
    *out_name = name_tok->lexeme;
    *out_params = params;
    params = NULL;
    *out_param_count = param_count;
    *out_param_capacity = param_capacity;
    *out_return_type = return_type;
    return_type = NULL;
    return 1;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < param_count; i++) pp_ast_root_free(params[i]);
    }
    free(params);
    pp_ast_root_free(return_type);
    return 0;
}
