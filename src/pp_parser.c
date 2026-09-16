#include "pp_parser.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pp_error.h"
#include "pp_ast.h"

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr) == NULL) goto error_cleanup; } while (0)

typedef enum {
    PP_PRECEDENCE_NONE = 0,
    PP_PRECEDENCE_EQUALITY = 10,
    PP_PRECEDENCE_RELATIONAL = 20,
    PP_PRECEDENCE_BITWISE_OR = 30,
    PP_PRECEDENCE_BITWISE_XOR = 40,
    PP_PRECEDENCE_BITWISE_AND = 50,
    PP_PRECEDENCE_ADDITIVE = 60,
    PP_PRECEDENCE_MULTIPLICATIVE = 70,
    PP_PRECEDENCE_CAST = 80,
    PP_PRECEDENCE_UNARY = 90,
    PP_PRECEDENCE_CALL_ACCESS = 100,
    PP_PRECEDENCE_PRIMARY = 110
} PP_Precedence;

static inline unsigned int pp_parser_get_line(const PP_Parser* parser);
static inline unsigned int pp_parser_get_col(const PP_Parser* parser);
static PP_TokenType pp_parser_peek_token_type(const PP_Parser* parser, unsigned int offset);
static int pp_parser_infix_binding_power(PP_TokenType token_type, unsigned char *left_bp, unsigned char *right_bp);
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
static PP_AST_Node* pp_parser_create_param_decl(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_expression_ext(PP_Parser* parser, unsigned char min_bp);
static PP_AST_Node* pp_parser_create_expression_or_assignment(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_literal(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_type(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_struct_initializer(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_collection_initializer(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_function_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_lambda_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_struct_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_typedef_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_newtype_declaration(PP_Parser* parser);
static PP_AST_Node* pp_parser_create_global_variable_declaration(PP_Parser* parser);
static int pp_parser_parse_param_list(PP_Parser* parser, PP_AST_Node*** out_params, unsigned int* out_count, unsigned int* out_capacity);

PP_Parser* pp_parser_create(PP_Lexer* lexer) {
    PP_Parser* parser = NULL;
    PP_ErrorList* error_list = NULL;
    if (!lexer) return NULL;
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
    if (parser) {
        free(parser);
    }
    return NULL;
}

int pp_parser_parse(PP_Parser* parser) {
    if (!parser) return 0;
    parser->program = pp_parser_create_program_node(parser);
    if (!parser->program) {
        return 0;
    }
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

static int pp_parser_infix_binding_power(PP_TokenType token_type, unsigned char *left_bp, unsigned char *right_bp) {
    unsigned char lb = 0, rb = 0;
    switch (token_type) {
        case PP_TT_EQUAL_EQUAL:
        case PP_TT_EXCLAMATION_EQUAL:
            lb = PP_PRECEDENCE_EQUALITY; rb = PP_PRECEDENCE_EQUALITY + 1; break;
        case PP_TT_LESS:
        case PP_TT_GREATER:
        case PP_TT_LESS_EQUAL:
        case PP_TT_GREATER_EQUAL:
            lb = PP_PRECEDENCE_RELATIONAL; rb = PP_PRECEDENCE_RELATIONAL + 1; break;
        case PP_TT_BAR:
            lb = PP_PRECEDENCE_BITWISE_OR; rb = PP_PRECEDENCE_BITWISE_OR + 1; break;
        case PP_TT_CARET:
            lb = PP_PRECEDENCE_BITWISE_XOR; rb = PP_PRECEDENCE_BITWISE_XOR + 1; break;
        case PP_TT_AMPERSAND:
            lb = PP_PRECEDENCE_BITWISE_AND; rb = PP_PRECEDENCE_BITWISE_AND + 1; break;
        case PP_TT_PLUS:
        case PP_TT_MINUS:
            lb = PP_PRECEDENCE_ADDITIVE; rb = PP_PRECEDENCE_ADDITIVE + 1; break;
        case PP_TT_STAR:
        case PP_TT_SLASH:
        case PP_TT_PERCENT:
            lb = PP_PRECEDENCE_MULTIPLICATIVE; rb = PP_PRECEDENCE_MULTIPLICATIVE + 1; break;
        case PP_TT_AS:
            lb = PP_PRECEDENCE_CAST; rb = PP_PRECEDENCE_CAST + 1; break;
        case PP_TT_PERIOD:
        case PP_TT_LEFT_PARENTHESIS:
        case PP_TT_LEFT_SQUARE_BRACKET:
            lb = PP_PRECEDENCE_CALL_ACCESS; rb = 0; break;
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
        pp_error_list_push_error(parser->error_list, parser->filename, t->line, t->column, "Expected token %d but got %d", expected_type, t->token_type);
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
    while (parser->idx < parser->n_tokens) {
        PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
        if (pp_parser_is_sync_token(tt)) {
            if (tt == PP_TT_SEMICOLON || tt == PP_TT_RIGHT_CURLY_BRACKET) {
                parser->idx++;
            }
            return 1;
        }
        parser->idx++;
    }
    return 0;
}

static PP_AST_Node* pp_parser_create_program_node(PP_Parser* parser) {
    PP_AST_Node* program = NULL;
    PP_AST_Node** decls = NULL;
    unsigned int decl_count = 0;
    unsigned int capacity = 0;

    if (!parser) goto error_cleanup;

    program = pp_ast_node_create(PP_AST_ProgramNode, 0, 0);
    NULL_POINTER_ERROR_HANDLE(program);

    capacity = AST_VECTOR_INIT_CAP;
    decls = (PP_AST_Node**)calloc(capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(decls);

    while (parser->idx < parser->n_tokens) {
        PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
        if (tt == PP_TT_EOF) break;
        PP_AST_Node* decl = NULL;
        switch (tt) {
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
        decls[decl_count++] = decl;
    }

    program->program.decls = decls;
    program->program.decl_count = decl_count;
    program->program.capacity = capacity;
    return program;

error_cleanup:
    free(decls);
    if (program) pp_ast_root_free(program);
    return NULL;
}

static PP_AST_Node* pp_parser_create_identifier(PP_Parser* parser) {
    PP_Token *tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!tok) return NULL;
    PP_AST_Node* node = pp_ast_node_create(PP_AST_IdentifierNode, tok->line, tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->identifier.identifier = tok->lexeme; /* use string pool pointer */
    return node;
error_cleanup:
    if (node) pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_primary(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    if (tt == PP_TT_IDENTIFIER && pp_parser_peek_token_type(parser, 1) == PP_TT_LEFT_CURLY_BRACKET) {
        return pp_parser_create_struct_initializer(parser);
    }
    switch (tt) {
        case PP_TT_IDENTIFIER:
            return pp_parser_create_identifier(parser);
        case PP_TT_INTEGER_LITERAL:
        case PP_TT_FLOAT_LITERAL:
        case PP_TT_TRUE:
        case PP_TT_FALSE:
        case PP_TT_STRING_LITERAL:
            return pp_parser_create_literal(parser);
        case PP_TT_LEFT_PARENTHESIS: {
            pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
            PP_AST_Node* expr = NULL;
            /* need forward decl for expression */
            extern PP_AST_Node* pp_parser_create_expression_ext(PP_Parser* parser, unsigned char min_bp);
            expr = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
            pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
            return expr;
        }
        case PP_TT_LEFT_SQUARE_BRACKET:
            return pp_parser_create_collection_initializer(parser);
        default:
            return NULL;
    }
}

static PP_AST_Node* pp_parser_create_literal(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    PP_Token *tok = pp_parser_consume_token(parser, tt);
    if (!tok) return NULL;
    PP_AST_Node* node = pp_ast_node_create(PP_AST_LiteralNode, tok->line, tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->literal.literal_type = tt;
    node->literal.lexeme = (char*)tok->lexeme; /* string pool */
    node->literal.decoration = NULL;
    return node;
error_cleanup:
    if (node) pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_unary(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    if (tt == PP_TT_MINUS || tt == PP_TT_EXCLAMATION) {
        PP_Token *op_tok = pp_parser_consume_token(parser, tt);
        if (!op_tok) return NULL;
        unsigned int line = op_tok->line;
        unsigned int col = op_tok->column;
        PP_AST_Node* operand = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_UNARY);
        if (!operand) return NULL;
        PP_AST_Node* node = pp_ast_node_create(PP_AST_UnaryExpressionNode, line, col);
        NULL_POINTER_ERROR_HANDLE(node);
        node->unary_expression.op = tt;
        node->unary_expression.operand = operand;
        node->unary_expression.decoration = NULL;
        return node;
error_cleanup:
        if (operand) pp_ast_root_free(operand);
        return NULL;
    }
    return pp_parser_create_primary(parser);
}

static PP_AST_Node* pp_parser_create_type(PP_Parser* parser) {
    if (!parser) return NULL;
    bool is_const = false;
    unsigned int line = 0, col = 0;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_CONST) {
        PP_Token* const_tok = pp_parser_consume_token(parser, PP_TT_CONST);
        if (const_tok) { line = const_tok->line; col = const_tok->column; }
        is_const = true;
    } else {
        line = pp_parser_get_line(parser);
        col = pp_parser_get_col(parser);
    }
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    PP_Token* tok = NULL;
    int kind = 4; /* identifier default */
    if (tt == PP_TT_VOID) { kind = 0; tok = pp_parser_consume_token(parser, PP_TT_VOID); }
    else if (tt == PP_TT_INT32) { kind = 1; tok = pp_parser_consume_token(parser, PP_TT_INT32); }
    else if (tt == PP_TT_FLOAT) { kind = 2; tok = pp_parser_consume_token(parser, PP_TT_FLOAT); }
    else if (tt == PP_TT_BOOL) { kind = 3; tok = pp_parser_consume_token(parser, PP_TT_BOOL); }
    else if (tt == PP_TT_IDENTIFIER) { kind = 4; tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER); }
    else {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected type name");
        return NULL;
    }
    /* Array specifier */
    PP_AST_ArrayListSpecifier* array_spec = NULL;
    while (pp_parser_peek_token_type(parser, 0) == PP_TT_LEFT_SQUARE_BRACKET) {
        pp_parser_consume_token(parser, PP_TT_LEFT_SQUARE_BRACKET);
        PP_AST_Node* size_expr = NULL;
        if (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_SQUARE_BRACKET && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
            size_expr = pp_parser_create_expression_or_assignment(parser);
        }
        if (!pp_parser_consume_token(parser, PP_TT_RIGHT_SQUARE_BRACKET)) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ']' to close array specifier");
        }
        PP_AST_ArrayListSpecifier* spec = (PP_AST_ArrayListSpecifier*)calloc(1, sizeof(PP_AST_ArrayListSpecifier));
        if (spec) {
            if (size_expr) {
                spec->kind = PP_ARRAY_SPEC_SIZE;
                spec->size_expr = size_expr;
            } else {
                spec->kind = PP_ARRAY_SPEC_EMPTY;
                spec->size_expr = NULL;
            }
            /* simple chaining: replace previous, real implementation would nest */
            if (array_spec) free(array_spec);
            array_spec = spec;
        }
    }
    bool is_ref = false;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_AMPERSAND) {
        pp_parser_consume_token(parser, PP_TT_AMPERSAND);
        is_ref = true;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_TypeExpressionNode, tok ? tok->line : 0, tok ? tok->column : 0);
    NULL_POINTER_ERROR_HANDLE(node);
    node->type_expression.is_const = is_const;
    node->type_expression.is_reference = is_ref;
    node->type_expression.identifier_name = (tt == PP_TT_IDENTIFIER && tok) ? (char*)tok->lexeme : NULL;
    node->type_expression.kind = kind;
    node->type_expression.array_spec = array_spec;
    return node;
error_cleanup:
    if (array_spec) {
        /* free any size_expr owned by spec before freeing spec */
        if (array_spec->size_expr) pp_ast_root_free(array_spec->size_expr);
        free(array_spec);
    }
    return NULL;
}

static PP_AST_Node* pp_parser_create_expression_or_assignment(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_AST_Node* lhs = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
    if (!lhs) return NULL;
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    switch (tt) {
        case PP_TT_EQUAL:
        case PP_TT_PLUS_EQUAL:
        case PP_TT_MINUS_EQUAL:
        case PP_TT_STAR_EQUAL:
        case PP_TT_SLASH_EQUAL:
        case PP_TT_PERCENT_EQUAL:
        case PP_TT_AMPERSAND_EQUAL:
        case PP_TT_BAR_EQUAL:
        case PP_TT_CARET_EQUAL: {
            PP_Token* op_tok = pp_parser_consume_token(parser, tt);
            if (!op_tok) {
                pp_ast_root_free(lhs);
                return NULL;
            }
            PP_AST_Node* rhs = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
            if (!rhs) {
                pp_ast_root_free(lhs);
                return NULL;
            }
            PP_AST_Node* node = pp_ast_node_create(PP_AST_AssignmentNode, op_tok->line, op_tok->column);
            if (!node) {
                pp_ast_root_free(lhs);
                pp_ast_root_free(rhs);
                return NULL;
            }
            node->assignment.lhs = lhs;
            node->assignment.op = tt;
            node->assignment.rhs = rhs;
            return node;
        }
        default:
            return lhs;
    }
}

static PP_AST_Node* pp_parser_create_statement(PP_Parser* parser) {
    if (!parser) return NULL;
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
            PP_AST_Node* expr = pp_parser_create_expression_or_assignment(parser);
            if (!expr) {
                pp_parser_sync(parser);
                return NULL;
            }
            if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
                pp_error_list_push_error(parser->error_list, parser->filename, pp_parser_get_line(parser), pp_parser_get_col(parser), "Expected ';' after expression statement");
                pp_parser_sync(parser);
            }
            return expr;
        }
    }
}

static PP_AST_Node* pp_parser_create_block(PP_Parser* parser) {
    if (!parser) return NULL;
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_LEFT_CURLY_BRACKET) return NULL;
    PP_Token* lbrace = pp_parser_consume_token(parser, PP_TT_LEFT_CURLY_BRACKET);
    unsigned int line = lbrace ? lbrace->line : 0;
    unsigned int col = lbrace ? lbrace->column : 0;
    PP_AST_Node* block = pp_ast_node_create(PP_AST_StatementListNode, line, col);
    NULL_POINTER_ERROR_HANDLE(block);
    unsigned int capacity = AST_VECTOR_INIT_CAP;
    unsigned int count = 0;
    PP_AST_Node** stmts = (PP_AST_Node**)calloc(capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(stmts);
    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_CURLY_BRACKET && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        PP_AST_Node* stmt = pp_parser_create_statement(parser);
        if (!stmt) break;
        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_Node** tmp = (PP_AST_Node**)realloc(stmts, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            stmts = tmp;
            capacity = new_cap;
        }
        stmts[count++] = stmt;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_CURLY_BRACKET)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '}' to close block");
        goto error_cleanup;
    }
    block->statement_list.stmts = stmts;
    block->statement_list.stmt_count = count;
    block->statement_list.capacity = capacity;
    return block;
error_cleanup:
    for (unsigned int i = 0; i < count; ++i) pp_ast_root_free(stmts[i]);
    free(stmts);
    pp_ast_root_free(block);
    return NULL;
}

static PP_AST_Node* pp_parser_create_expression_ext(PP_Parser* parser, unsigned char min_bp) {
    if (!parser) return NULL;
    PP_AST_Node* lhs = pp_parser_create_unary(parser);
    if (!lhs) return NULL;

    while (1) {
        PP_TokenType op_type = pp_parser_peek_token_type(parser, 0);
        unsigned char left_bp = 0, right_bp = 0;
        if (!pp_parser_infix_binding_power(op_type, &left_bp, &right_bp)) break;
        if (left_bp < min_bp) break;

        PP_Token *op_tok = pp_parser_consume_token(parser, op_type);
        if (!op_tok) {
            pp_ast_root_free(lhs);
            return NULL;
        }

        /* Handle postfix call / index / member */
        if (op_type == PP_TT_LEFT_PARENTHESIS || op_type == PP_TT_LEFT_SQUARE_BRACKET || op_type == PP_TT_PERIOD) {
            if (op_type == PP_TT_LEFT_PARENTHESIS) {
                unsigned int arg_count = 0;
                unsigned int arg_capacity = AST_VECTOR_INIT_CAP;
                PP_AST_Node** arguments = (PP_AST_Node**)calloc(arg_capacity, sizeof(PP_AST_Node*));
                if (!arguments) {
                    pp_ast_root_free(lhs);
                    return NULL;
                }
                while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_PARENTHESIS && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
                    PP_AST_Node* arg = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
                    if (!arg) {
                        for (unsigned int i = 0; i < arg_count; ++i) pp_ast_root_free(arguments[i]);
                        free(arguments);
                        pp_ast_root_free(lhs);
                        return NULL;
                    }
                    arguments[arg_count++] = arg;
                    if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
                        pp_parser_consume_token(parser, PP_TT_COMMA);
                    } else {
                        break;
                    }
                }
                pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS);
                PP_AST_Node* node = pp_ast_node_create(PP_AST_FunctionCallNode, op_tok->line, op_tok->column);
                if (!node) {
                    for (unsigned int i = 0; i < arg_count; ++i) pp_ast_root_free(arguments[i]);
                    free(arguments);
                    pp_ast_root_free(lhs);
                    return NULL;
                }
                node->function_call.callee = lhs;
                node->function_call.arguments = arguments;
                node->function_call.arg_count = arg_count;
                node->function_call.arg_capacity = arg_capacity;
                node->function_call.decoration = NULL;
                lhs = node;
            } else if (op_type == PP_TT_LEFT_SQUARE_BRACKET) {
                PP_AST_Node* index_expr = pp_parser_create_expression_ext(parser, PP_PRECEDENCE_NONE);
                if (!index_expr) {
                    pp_ast_root_free(lhs);
                    return NULL;
                }
                pp_parser_consume_token(parser, PP_TT_RIGHT_SQUARE_BRACKET);
                PP_AST_Node* node = pp_ast_node_create(PP_AST_ArrayListAccessNode, op_tok->line, op_tok->column);
                if (!node) {
                    pp_ast_root_free(lhs);
                    pp_ast_root_free(index_expr);
                    return NULL;
                }
                node->array_list_access.expr = lhs;
                node->array_list_access.index = index_expr;
                node->array_list_access.decoration = NULL;
                lhs = node;
            } else if (op_type == PP_TT_PERIOD) {
                PP_Token* member_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
                if (!member_tok) {
                    pp_ast_root_free(lhs);
                    return NULL;
                }
                PP_AST_Node* node = pp_ast_node_create(PP_AST_MemberAccessNode, op_tok->line, op_tok->column);
                if (!node) {
                    pp_ast_root_free(lhs);
                    return NULL;
                }
                node->member_access.object = lhs;
                node->member_access.member = (char*)member_tok->lexeme;
                node->member_access.decoration = NULL;
                lhs = node;
            }
            continue;
        }

        if (op_type == PP_TT_AS) {
            /* Cast expression */
            extern PP_AST_Node* pp_parser_create_type(PP_Parser* parser);
            PP_AST_Node* type_node = pp_parser_create_type(parser);
            if (!type_node) {
                pp_ast_root_free(lhs);
                return NULL;
            }
            PP_AST_Node* node = pp_ast_node_create(PP_AST_CastExpressionNode, op_tok->line, op_tok->column);
            if (!node) {
                pp_ast_root_free(lhs);
                pp_ast_root_free(type_node);
                return NULL;
            }
            node->cast_expression.expression = lhs;
            node->cast_expression.type_expr = type_node;
            node->cast_expression.decoration = NULL;
            lhs = node;
            continue;
        }

        PP_AST_Node* rhs = pp_parser_create_expression_ext(parser, right_bp ? right_bp : 0);
        if (!rhs) {
            pp_ast_root_free(lhs);
            return NULL;
        }
        PP_AST_Node* node = pp_ast_node_create(PP_AST_BinaryExpressionNode, op_tok->line, op_tok->column);
        if (!node) {
            pp_ast_root_free(lhs);
            pp_ast_root_free(rhs);
            return NULL;
        }
        node->binary_expression.op = op_type;
        node->binary_expression.left = lhs;
        node->binary_expression.right = rhs;
        node->binary_expression.decoration = NULL;
        lhs = node;
    }
    return lhs;
}

static PP_AST_Node* pp_parser_create_return_statement(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* tok = pp_parser_consume_token(parser, PP_TT_RETURN);
    if (!tok) return NULL;
    unsigned int line = tok->line;
    unsigned int col = tok->column;
    PP_AST_Node* expr = pp_parser_create_expression_or_assignment(parser);
    if (!expr) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected expression after return");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ';' after return expression");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_ReturnStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->return_statement.expr = expr;
    return node;
error_cleanup:
    if (expr) pp_ast_root_free(expr);
    return NULL;
}

static PP_AST_Node* pp_parser_create_if_statement(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* tok = pp_parser_consume_token(parser, PP_TT_IF);
    if (!tok) return NULL;
    unsigned int line = tok->line;
    unsigned int col = tok->column;
    if (!pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '(' after if");
        goto error_cleanup;
    }
    PP_AST_Node* cond = pp_parser_create_expression_or_assignment(parser);
    if (!cond) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected condition expression in if");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ')' after if condition");
        goto error_cleanup;
    }
    PP_AST_Node* then_branch = pp_parser_create_statement(parser);
    if (!then_branch) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected statement after if condition");
        goto error_cleanup;
    }
    PP_AST_Node* else_branch = NULL;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_ELSE) {
        pp_parser_consume_token(parser, PP_TT_ELSE);
        else_branch = pp_parser_create_statement(parser);
        if (!else_branch) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected statement after else");
            goto error_cleanup;
        }
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_IfStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->if_statement.cond = cond;
    node->if_statement.then_branch = then_branch;
    node->if_statement.else_branch = else_branch;
    return node;
error_cleanup:
    if (cond) pp_ast_root_free(cond);
    if (then_branch) pp_ast_root_free(then_branch);
    if (else_branch) pp_ast_root_free(else_branch);
    return NULL;
}

static PP_AST_Node* pp_parser_create_while_statement(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* tok = pp_parser_consume_token(parser, PP_TT_WHILE);
    if (!tok) return NULL;
    unsigned int line = tok->line;
    unsigned int col = tok->column;
    if (!pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '(' after while");
        goto error_cleanup;
    }
    PP_AST_Node* cond = pp_parser_create_expression_or_assignment(parser);
    if (!cond) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected condition expression in while");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ')' after while condition");
        goto error_cleanup;
    }
    PP_AST_Node* body = pp_parser_create_statement(parser);
    if (!body) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected statement after while condition");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_WhileStatementNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->while_statement.cond = cond;
    node->while_statement.body = body;
    return node;
error_cleanup:
    if (cond) pp_ast_root_free(cond);
    if (body) pp_ast_root_free(body);
    return NULL;
}

static PP_AST_Node* pp_parser_create_for_statement(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* for_tok = pp_parser_consume_token(parser, PP_TT_FOR);
    if (!for_tok) return NULL;
    unsigned int line = for_tok->line;
    unsigned int col = for_tok->column;
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_LEFT_PARENTHESIS) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '(' after for");
        goto error_cleanup;
    }
    pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
    PP_AST_Node* init = NULL;
    PP_AST_Node* cond = NULL;
    PP_AST_Node* update = NULL;
    /* init expression or declaration */
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_SEMICOLON) {
        init = pp_parser_create_expression_or_assignment(parser);
        if (!init) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected init expression in for");
            goto error_cleanup;
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ';' after for init");
        goto error_cleanup;
    }
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_SEMICOLON) {
        cond = pp_parser_create_expression_or_assignment(parser);
        if (!cond) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected condition expression in for");
            goto error_cleanup;
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ';' after for condition");
        goto error_cleanup;
    }
    if (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_PARENTHESIS) {
        update = pp_parser_create_expression_or_assignment(parser);
        if (!update) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected update expression in for");
            goto error_cleanup;
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ')' after for header");
        goto error_cleanup;
    }
    PP_AST_Node* body = pp_parser_create_statement(parser);
    if (!body) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected statement after for header");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_ForStatementNode, 0, 0);
    if (!node) goto error_cleanup;
    node->for_statement.init = init;
    node->for_statement.cond = cond;
    node->for_statement.update = update;
    node->for_statement.body = body;
    return node;
error_cleanup:
    if (init) pp_ast_root_free(init);
    if (cond) pp_ast_root_free(cond);
    if (update) pp_ast_root_free(update);
    if (body) pp_ast_root_free(body);
    return NULL;
}

static PP_AST_Node* pp_parser_create_param_decl(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* ident_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!ident_tok) return NULL;
    if (!pp_parser_consume_token(parser, PP_TT_COLON_COLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, ident_tok->line, ident_tok->column, "Expected '::' after parameter name");
        goto error_cleanup;
    }
    PP_AST_Node* type_expr = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(type_expr);
    PP_AST_Node* node = pp_ast_node_create(PP_AST_ParamDeclarationNode, ident_tok->line, ident_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->param_declaration.identifier = (char*)ident_tok->lexeme;
    node->param_declaration.type_expr = type_expr;
    return node;
error_cleanup:
    if (type_expr) pp_ast_root_free(type_expr);
    return NULL;
}

static PP_AST_Node* pp_parser_create_variable_declaration(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_TokenType tt = pp_parser_peek_token_type(parser, 0);
    if (tt != PP_TT_LET) return NULL;
    PP_Token* decl_tok = pp_parser_consume_token(parser, tt);
    if (!decl_tok) return NULL;
    unsigned int line = decl_tok->line;
    unsigned int col = decl_tok->column;
    PP_AST_Node* var_decl = pp_parser_create_param_decl(parser);
    if (!var_decl) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected variable declarator after let");
        goto error_cleanup;
    }
    PP_AST_Node* initializer = NULL;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_EQUAL) {
        pp_parser_consume_token(parser, PP_TT_EQUAL);
        initializer = pp_parser_create_expression_or_assignment(parser);
        if (!initializer) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected initializer expression");
            goto error_cleanup;
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ';' after variable declaration");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_VariableDeclarationNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->variable_declaration.var_decl = var_decl;
    node->variable_declaration.initializer = initializer;
    return node;
error_cleanup:
    if (var_decl) pp_ast_root_free(var_decl);
    if (initializer) pp_ast_root_free(initializer);
    return NULL;
}

static PP_AST_Node* pp_parser_create_global_variable_declaration(PP_Parser* parser) {
    return pp_parser_create_variable_declaration(parser);
}

static PP_AST_Node* pp_parser_create_function_declaration(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* func_tok = pp_parser_consume_token(parser, PP_TT_FUNC);
    if (!func_tok) return NULL;
    PP_Token* name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!name_tok) return NULL;
    if (!pp_parser_consume_token(parser, PP_TT_COLON_COLON)) { /* error */ }
    if (!pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, func_tok ? func_tok->line : 0, func_tok ? func_tok->column : 0, "Expected '(' after function name");
        return NULL;
    }
    PP_AST_Node** params = NULL;
    unsigned int param_count = 0, param_capacity = 0;
    if (!pp_parser_parse_param_list(parser, &params, &param_count, &param_capacity)) {
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, func_tok->line, func_tok->column, "Expected ')' after parameter list");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_ARROW)) {
        pp_error_list_push_error(parser->error_list, parser->filename, func_tok->line, func_tok->column, "Expected '->' after parameter list");
        goto error_cleanup;
    }
    PP_AST_Node* return_type = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(return_type);
    PP_AST_Node* body = pp_parser_create_block(parser);
    NULL_POINTER_ERROR_HANDLE(body);
    PP_AST_Node* node = pp_ast_node_create(PP_AST_FunctionDeclarationNode, func_tok->line, func_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->function_declaration.name = (char*)name_tok->lexeme;
    node->function_declaration.params = params;
    node->function_declaration.param_count = param_count;
    node->function_declaration.param_capacity = param_capacity;
    node->function_declaration.return_type = return_type;
    node->function_declaration.body = body;
    return node;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < param_count; ++i) pp_ast_root_free(params[i]);
        free(params);
    }
    if (return_type) pp_ast_root_free(return_type);
    if (body) pp_ast_root_free(body);
    return NULL;
}

static PP_AST_Node* pp_parser_create_lambda_declaration(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* lambda_tok = pp_parser_consume_token(parser, PP_TT_LAMBDA);
    if (!lambda_tok) return NULL;
    PP_Token* name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!name_tok) {
        pp_error_list_push_error(parser->error_list, parser->filename, lambda_tok->line, lambda_tok->column, "Expected identifier after lambda keyword");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_COLON_COLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, lambda_tok->line, lambda_tok->column, "Expected '::' after lambda name");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, lambda_tok->line, lambda_tok->column, "Expected '(' after lambda name");
        goto error_cleanup;
    }
    PP_AST_Node** params = NULL;
    unsigned int param_count = 0, param_capacity = 0;
    if (!pp_parser_parse_param_list(parser, &params, &param_count, &param_capacity)) {
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_PARENTHESIS)) {
        pp_error_list_push_error(parser->error_list, parser->filename, lambda_tok->line, lambda_tok->column, "Expected ')' after lambda parameter list");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_ARROW)) {
        pp_error_list_push_error(parser->error_list, parser->filename, lambda_tok->line, lambda_tok->column, "Expected '->' after lambda parameter list");
        goto error_cleanup;
    }
    PP_AST_Node* return_type = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(return_type);
    PP_AST_Node* body = pp_parser_create_block(parser);
    NULL_POINTER_ERROR_HANDLE(body);
    PP_AST_Node* node = pp_ast_node_create(PP_AST_LambdaDeclarationNode, lambda_tok->line, lambda_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->function_declaration.name = (char*)name_tok->lexeme;
    node->function_declaration.params = params;
    node->function_declaration.param_count = param_count;
    node->function_declaration.param_capacity = param_capacity;
    node->function_declaration.return_type = return_type;
    node->function_declaration.body = body;
    return node;
error_cleanup:
    if (params) { for (unsigned int i=0;i<param_count;++i) pp_ast_root_free(params[i]); free(params); }
    if (return_type) pp_ast_root_free(return_type);
    if (body) pp_ast_root_free(body);
    return NULL;
}

static PP_AST_Node* pp_parser_create_struct_declaration(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* struct_tok = pp_parser_consume_token(parser, PP_TT_STRUCT);
    if (!struct_tok) return NULL;
    PP_Token* name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!name_tok) {
        pp_error_list_push_error(parser->error_list, parser->filename, struct_tok->line, struct_tok->column, "Expected identifier after struct keyword");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_LEFT_CURLY_BRACKET)) {
        pp_error_list_push_error(parser->error_list, parser->filename, struct_tok->line, struct_tok->column, "Expected '{' after struct name");
        goto error_cleanup;
    }
    unsigned int capacity = AST_VECTOR_INIT_CAP;
    unsigned int count = 0;
    PP_AST_StructField* fields = (PP_AST_StructField*)calloc(capacity, sizeof(PP_AST_StructField));
    NULL_POINTER_ERROR_HANDLE(fields);
    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_CURLY_BRACKET && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        PP_AST_Node* param_decl = pp_parser_create_param_decl(parser);
        if (!param_decl) break;
        PP_AST_Node* initializer = NULL;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_EQUAL) {
            pp_parser_consume_token(parser, PP_TT_EQUAL);
            initializer = pp_parser_create_expression_or_assignment(parser);
        }
        if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
            /* error recovery */
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
        count++;
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_CURLY_BRACKET)) {
        pp_error_list_push_error(parser->error_list, parser->filename, struct_tok->line, struct_tok->column, "Expected '}' to close struct declaration");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_StructDeclarationNode, struct_tok->line, struct_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->struct_declaration.name = (char*)name_tok->lexeme;
    node->struct_declaration.fields = fields;
    node->struct_declaration.field_count = count;
    node->struct_declaration.field_capacity = capacity;
    return node;
error_cleanup:
    if (fields) {
        for (unsigned int i = 0; i < count; ++i) {
            pp_ast_root_free(fields[i].field);
            if (fields[i].initializer) pp_ast_root_free(fields[i].initializer);
        }
        free(fields);
    }
    return NULL;
}

static PP_AST_Node* pp_parser_create_typedef_declaration(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* typedef_tok = pp_parser_consume_token(parser, PP_TT_TYPEDEF);
    if (!typedef_tok) return NULL;
    PP_AST_Node* base_type = pp_parser_create_type(parser);
    NULL_POINTER_ERROR_HANDLE(base_type);
    PP_Token* name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!name_tok) {
        pp_error_list_push_error(parser->error_list, parser->filename, typedef_tok->line, typedef_tok->column, "Expected identifier after typedef base type");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, typedef_tok->line, typedef_tok->column, "Expected ';' after typedef declaration");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_TypedefDeclarationNode, typedef_tok->line, typedef_tok->column);
    NULL_POINTER_ERROR_HANDLE(node);
    node->typedef_declaration.base_type = base_type;
    node->typedef_declaration.identifier = (char*)name_tok->lexeme;
    return node;
error_cleanup:
    if (base_type) pp_ast_root_free(base_type);
    return NULL;
}

static PP_AST_Node* pp_parser_create_newtype_declaration(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* newtype_tok = pp_parser_consume_token(parser, PP_TT_NEWTYPE);
    if (!newtype_tok) return NULL;
    unsigned int line = newtype_tok->line;
    unsigned int col = newtype_tok->column;
    PP_AST_Node* base_type = pp_parser_create_type(parser);
    if (!base_type) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected type after newtype");
        goto error_cleanup;
    }
    PP_Token* name_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!name_tok) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected identifier after newtype base type");
        goto error_cleanup;
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ';' after newtype declaration");
        /* recover */
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_NewtypeDeclarationNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->newtype_declaration.base_type = base_type;
    node->newtype_declaration.identifier = (char*)name_tok->lexeme;
    return node;
error_cleanup:
    if (base_type) pp_ast_root_free(base_type);
    return NULL;
}

static PP_AST_Node* pp_parser_create_struct_initializer(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* ident_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
    if (!ident_tok) return NULL;
    unsigned int line = ident_tok->line;
    unsigned int col = ident_tok->column;
    if (!pp_parser_consume_token(parser, PP_TT_LEFT_CURLY_BRACKET)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '{' after struct initializer identifier");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_StructInitializerNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->struct_initializer.identifier = (char*)ident_tok->lexeme;
    unsigned int capacity = AST_VECTOR_INIT_CAP;
    unsigned int count = 0;
    PP_AST_StructInitEntry* entries = (PP_AST_StructInitEntry*)calloc(capacity, sizeof(PP_AST_StructInitEntry));
    NULL_POINTER_ERROR_HANDLE(entries);
    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_CURLY_BRACKET && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        if (!pp_parser_consume_token(parser, PP_TT_PERIOD)) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '.' before struct initializer member");
            break;
        }
        PP_Token* member_tok = pp_parser_consume_token(parser, PP_TT_IDENTIFIER);
        if (!member_tok) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected identifier after '.' in struct initializer");
            break;
        }
        if (!pp_parser_consume_token(parser, PP_TT_EQUAL)) {
            pp_error_list_push_error(parser->error_list, parser->filename, member_tok->line, member_tok->column, "Expected '=' after struct initializer member name");
            break;
        }
        PP_AST_Node* member_val = pp_parser_create_expression_or_assignment(parser);
        if (!member_val) {
            pp_error_list_push_error(parser->error_list, parser->filename, member_tok->line, member_tok->column, "Expected expression for struct initializer member value");
            break;
        }
        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_StructInitEntry* tmp = (PP_AST_StructInitEntry*)realloc(entries, new_cap * sizeof(PP_AST_StructInitEntry));
            NULL_POINTER_ERROR_HANDLE(tmp);
            entries = tmp;
            capacity = new_cap;
        }
        /* Create simple nodes for member identifier */
        PP_AST_Node* member_id_node = pp_ast_node_create(PP_AST_IdentifierNode, member_tok->line, member_tok->column);
        NULL_POINTER_ERROR_HANDLE(member_id_node);
        member_id_node->identifier.identifier = (char*)member_tok->lexeme;
        entries[count].member_identifier = member_id_node;
        entries[count].member_value = member_val;
        count++;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
            pp_parser_consume_token(parser, PP_TT_COMMA);
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_CURLY_BRACKET)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected '}' to close struct initializer");
        goto error_cleanup;
    }
    node->struct_initializer.entries = entries;
    node->struct_initializer.entry_count = count;
    node->struct_initializer.entry_capacity = capacity;
    return node;
error_cleanup:
    if (entries) {
        for (unsigned int i = 0; i < count; ++i) {
            if (entries[i].member_identifier) pp_ast_root_free(entries[i].member_identifier);
            if (entries[i].member_value) pp_ast_root_free(entries[i].member_value);
        }
        free(entries);
    }
    if (node) pp_ast_root_free(node);
    return NULL;
}

static PP_AST_Node* pp_parser_create_collection_initializer(PP_Parser* parser) {
    if (!parser) return NULL;
    PP_Token* lbrack = pp_parser_consume_token(parser, PP_TT_LEFT_SQUARE_BRACKET);
    if (!lbrack) return NULL;
    unsigned int line = lbrack->line;
    unsigned int col = lbrack->column;
    PP_AST_Node* type_expr = pp_parser_create_type(parser);
    if (!type_expr) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected type in collection initializer");
        goto error_cleanup;
    }
    PP_AST_Node* size_expr = NULL;
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_COLON) {
        pp_parser_consume_token(parser, PP_TT_COLON);
        size_expr = pp_parser_create_expression_or_assignment(parser);
        if (!size_expr) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected expression after ':' in collection initializer");
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_SEMICOLON)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ';' after collection type and size");
        goto error_cleanup;
    }
    /* parse element list until closing ] */
    unsigned int capacity = AST_VECTOR_INIT_CAP;
    unsigned int count = 0;
    PP_AST_Node** elements = (PP_AST_Node**)calloc(capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(elements);
    while (pp_parser_peek_token_type(parser, 0) != PP_TT_RIGHT_SQUARE_BRACKET && pp_parser_peek_token_type(parser, 0) != PP_TT_EOF) {
        PP_AST_Node* elem = pp_parser_create_expression_or_assignment(parser);
        if (!elem) {
            pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected expression in collection initializer");
            goto error_cleanup;
        }
        if (count >= capacity) {
            unsigned int new_cap = capacity * 2;
            PP_AST_Node** tmp = (PP_AST_Node**)realloc(elements, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            elements = tmp;
            capacity = new_cap;
        }
        elements[count++] = elem;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
            pp_parser_consume_token(parser, PP_TT_COMMA);
        }
    }
    if (!pp_parser_consume_token(parser, PP_TT_RIGHT_SQUARE_BRACKET)) {
        pp_error_list_push_error(parser->error_list, parser->filename, line, col, "Expected ']' to close collection initializer");
        goto error_cleanup;
    }
    PP_AST_Node* node = pp_ast_node_create(PP_AST_CollectionInitializerNode, type_expr->line, type_expr->col);
    NULL_POINTER_ERROR_HANDLE(node);
    node->collection_initializer.type_expr = type_expr;
    node->collection_initializer.size_expr = size_expr;
    node->collection_initializer.elements = elements;
    node->collection_initializer.element_count = count;
    node->collection_initializer.element_capacity = capacity;
    return node;
error_cleanup:
    if (type_expr) pp_ast_root_free(type_expr);
    if (size_expr) pp_ast_root_free(size_expr);
    if (elements) {
        for (unsigned int i = 0; i < count; ++i) pp_ast_root_free(elements[i]);
        free(elements);
    }
    return NULL;
}

static int pp_parser_parse_param_list(PP_Parser* parser, PP_AST_Node*** out_params, unsigned int* out_count, unsigned int* out_capacity) {
    if (!parser || !out_params || !out_count || !out_capacity) return 0;
    *out_count = 0;
    *out_capacity = AST_VECTOR_INIT_CAP;
    PP_AST_Node** params = (PP_AST_Node**)calloc(*out_capacity, sizeof(PP_AST_Node*));
    NULL_POINTER_ERROR_HANDLE(params);
    if (pp_parser_peek_token_type(parser, 0) == PP_TT_RIGHT_PARENTHESIS) {
        *out_params = params;
        return 1;
    }
    while (1) {
        PP_AST_Node* param = pp_parser_create_param_decl(parser);
        NULL_POINTER_ERROR_HANDLE(param);
        if (*out_count >= *out_capacity) {
            unsigned int new_cap = *out_capacity * 2;
            PP_AST_Node** tmp = (PP_AST_Node**)realloc(params, new_cap * sizeof(PP_AST_Node*));
            NULL_POINTER_ERROR_HANDLE(tmp);
            params = tmp;
            *out_capacity = new_cap;
        }
        params[*out_count] = param;
        (*out_count)++;
        if (pp_parser_peek_token_type(parser, 0) == PP_TT_COMMA) {
            pp_parser_consume_token(parser, PP_TT_COMMA);
            continue;
        }
        break;
    }
    *out_params = params;
    return 1;
error_cleanup:
    if (params) {
        for (unsigned int i = 0; i < *out_count; ++i) pp_ast_root_free(params[i]);
        free(params);
    }
    return 0;
}


