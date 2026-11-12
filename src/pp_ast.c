#include "pp_ast.h"

#include <stdio.h>
#include <stdlib.h>

#include "pp_tokens.h"

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

PP_AST_Decoration* pp_ast_decoration_create(const void* resolved_type, PP_ValueCategory value_category,
                                            bool is_constexpr, bool is_escapable_ref) {
    PP_AST_Decoration* d = (PP_AST_Decoration*)calloc(1, sizeof(PP_AST_Decoration));
    if (!d) return NULL;
    d->resolved_type = resolved_type;
    d->value_category = value_category;
    d->is_constexpr = is_constexpr;
    d->is_escapable_ref = is_escapable_ref;
    return d;
}

void pp_ast_decoration_free(PP_AST_Decoration* decor) { free(decor); }

PP_AST_Node* pp_ast_node_create(PP_AST_NodeType node_type, unsigned int line, unsigned int col) {
    PP_AST_Node* node = (PP_AST_Node*)calloc(1, sizeof(PP_AST_Node));
    if (!node) return NULL;
    node->node_type = node_type;
    node->line = line;
    node->col = col;
    return node;
}

const PP_AST_Decoration* pp_ast_get_decoration(const PP_AST_Node* node) {
    if (!node) return NULL;
    switch (node->node_type) {
        case PP_AST_BinaryExpressionNode:
            return node->binary_expression.decoration;
        case PP_AST_UnaryExpressionNode:
            return node->unary_expression.decoration;
        case PP_AST_CastExpressionNode:
            return node->cast_expression.decoration;
        case PP_AST_LiteralNode:
            return node->literal.decoration;
        case PP_AST_NullLiteralNode:
            return node->null_literal.decoration;
        case PP_AST_IdentifierNode:
            return node->identifier.decoration;
        case PP_AST_FunctionCallNode:
            return node->function_call.decoration;
        case PP_AST_MemberAccessNode:
            return node->member_access.decoration;
        case PP_AST_ArrayListAccessNode:
            return node->array_list_access.decoration;
        case PP_AST_StructInitializerNode:
            return node->struct_initializer.decoration;
        case PP_AST_CollectionInitializerNode:
            return node->collection_initializer.decoration;
        default:
            return NULL;
    }
}

static void pp_ast_root_free_internal(PP_AST_Node* node) {
    if (!node) return;
    switch (node->node_type) {
        case PP_AST_ProgramNode: {
            if (node->program.decls) {
                for (unsigned int i = 0; i < node->program.decl_count; i++) {
                    pp_ast_root_free_internal(node->program.decls[i]);
                }
                free(node->program.decls);
            }
            break;
        }
        case PP_AST_FunctionDeclarationNode:
        case PP_AST_LambdaDeclarationNode:
        case PP_AST_OperatorDeclarationNode: {
            if (node->function_declaration.params) {
                for (unsigned int i = 0; i < node->function_declaration.param_count; i++) {
                    pp_ast_root_free_internal(node->function_declaration.params[i]);
                }
                free(node->function_declaration.params);
            }
            /* function_declaration.name is pool-owned const char* – do not free */
            pp_ast_root_free_internal(node->function_declaration.return_type);
            pp_ast_root_free_internal(node->function_declaration.body);
            pp_symbol_table_free(node->function_declaration.param_symbol_table);
            break;
        }
        case PP_AST_ExternFunctionDeclarationNode:
        case PP_AST_ExternLambdaDeclarationNode:
        case PP_AST_ExternOperatorDeclarationNode: {
            if (node->extern_function_declaration.params) {
                for (unsigned int i = 0; i < node->extern_function_declaration.param_count; i++) {
                    pp_ast_root_free_internal(node->extern_function_declaration.params[i]);
                }
                free(node->extern_function_declaration.params);
            }
            pp_ast_root_free_internal(node->extern_function_declaration.return_type);
            break;
        }
        case PP_AST_StatementListNode: {
            if (node->statement_list.stmts) {
                for (unsigned int i = 0; i < node->statement_list.stmt_count; i++) {
                    pp_ast_root_free_internal(node->statement_list.stmts[i]);
                }
                free(node->statement_list.stmts);
            }
            pp_symbol_table_free(node->statement_list.symbol_table);
            break;
        }
        case PP_AST_VariableDeclarationNode: {
            pp_ast_root_free_internal(node->variable_declaration.var_decl);
            pp_ast_root_free_internal(node->variable_declaration.initializer);
            break;
        }
        case PP_AST_AssignmentNode: {
            pp_ast_root_free_internal(node->assignment.lhs);
            pp_ast_root_free_internal(node->assignment.rhs);
            break;
        }
        case PP_AST_ReturnStatementNode: {
            pp_ast_root_free_internal(node->return_statement.expr);
            break;
        }
        case PP_AST_IfStatementNode: {
            pp_ast_root_free_internal(node->if_statement.cond);
            pp_ast_root_free_internal(node->if_statement.then_branch);
            pp_ast_root_free_internal(node->if_statement.else_branch);
            break;
        }
        case PP_AST_WhileStatementNode: {
            pp_ast_root_free_internal(node->while_statement.cond);
            pp_ast_root_free_internal(node->while_statement.body);
            break;
        }
        case PP_AST_ForStatementNode: {
            pp_ast_root_free_internal(node->for_statement.init);
            pp_ast_root_free_internal(node->for_statement.cond);
            pp_ast_root_free_internal(node->for_statement.update);
            pp_ast_root_free_internal(node->for_statement.body);
            pp_symbol_table_free(node->for_statement.for_init_symbol_table);
            break;
        }
        case PP_AST_BinaryExpressionNode: {
            pp_ast_root_free_internal(node->binary_expression.left);
            pp_ast_root_free_internal(node->binary_expression.right);
            pp_ast_decoration_free(node->binary_expression.decoration);
            break;
        }
        case PP_AST_UnaryExpressionNode: {
            pp_ast_root_free_internal(node->unary_expression.operand);
            pp_ast_decoration_free(node->unary_expression.decoration);
            break;
        }
        case PP_AST_CastExpressionNode: {
            pp_ast_root_free_internal(node->cast_expression.expression);
            pp_ast_root_free_internal(node->cast_expression.type_expr);
            pp_ast_decoration_free(node->cast_expression.decoration);
            break;
        }
        case PP_AST_LiteralNode: {
            /* literal.lexeme is pool-owned const char* – do not free */
            pp_ast_decoration_free(node->literal.decoration);
            break;
        }
        case PP_AST_NullLiteralNode: {
            pp_ast_root_free_internal(node->null_literal.type_node);
            pp_ast_decoration_free(node->null_literal.decoration);
            break;
        }
        case PP_AST_IdentifierNode: {
            /* identifier.identifier is pool-owned const char* – do not free */
            pp_ast_decoration_free(node->identifier.decoration);
            break;
        }
        case PP_AST_FunctionCallNode: {
            pp_ast_root_free_internal(node->function_call.callee);
            for (unsigned int i = 0; i < node->function_call.arg_count; i++) {
                pp_ast_root_free_internal(node->function_call.arguments[i]);
            }
            free(node->function_call.arguments);
            pp_ast_decoration_free(node->function_call.decoration);
            break;
        }
        case PP_AST_MemberAccessNode: {
            pp_ast_root_free_internal(node->member_access.object);
            /* member_access.member is pool-owned const char* – do not free */
            pp_ast_decoration_free(node->member_access.decoration);
            break;
        }
        case PP_AST_ArrayListAccessNode: {
            pp_ast_root_free_internal(node->array_list_access.expr);
            pp_ast_root_free_internal(node->array_list_access.index);
            pp_ast_decoration_free(node->array_list_access.decoration);
            break;
        }
        case PP_AST_ParamDeclarationNode: {
            pp_ast_root_free_internal(node->param_declaration.type_expr);
            break;
        }
        case PP_AST_TypeExpressionNode: {
            /* type_expression.identifier_name is pool-owned const char* – do not free */
            pp_ast_root_free_internal(node->type_expression.type_node);
            break;
        }
        case PP_AST_TypeBaseNode: {
            /* type_base.identifier_name is pool-owned */
            break;
        }
        case PP_AST_TypeArrayNode: {
            pp_ast_root_free_internal(node->type_array.element_type);
            pp_ast_root_free_internal(node->type_array.size_expr);
            break;
        }
        case PP_AST_TypeListNode: {
            pp_ast_root_free_internal(node->type_list.element_type);
            break;
        }
        case PP_AST_TypePointerNode: {
            pp_ast_root_free_internal(node->type_pointer.pointee_type);
            break;
        }
        case PP_AST_TypedefDeclarationNode: {
            /* typedef_declaration.identifier is pool-owned const char* – do not free */
            pp_ast_root_free_internal(node->typedef_declaration.base_type);
            break;
        }
        case PP_AST_NewtypeDeclarationNode: {
            /* newtype_declaration.identifier is pool-owned const char* – do not free */
            pp_ast_root_free_internal(node->newtype_declaration.base_type);
            break;
        }
        case PP_AST_StructDeclarationNode: {
            /* struct_declaration.name is pool-owned const char* – do not free */
            if (node->struct_declaration.fields) {
                for (unsigned int i = 0; i < node->struct_declaration.field_count; i++) {
                    pp_ast_root_free_internal(node->struct_declaration.fields[i].field);
                    pp_ast_root_free_internal(node->struct_declaration.fields[i].initializer);
                }
                free(node->struct_declaration.fields);
            }
            break;
        }
        case PP_AST_StructInitializerNode: {
            /* struct_initializer.identifier is pool-owned const char* – do not free */
            if (node->struct_initializer.entries) {
                for (unsigned int i = 0; i < node->struct_initializer.entry_count; i++) {
                    pp_ast_root_free_internal(node->struct_initializer.entries[i].member_identifier);
                    pp_ast_root_free_internal(node->struct_initializer.entries[i].member_value);
                }
                free(node->struct_initializer.entries);
            }
            pp_ast_decoration_free(node->struct_initializer.decoration);
            break;
        }
        case PP_AST_CollectionInitializerNode: {
            pp_ast_root_free_internal(node->collection_initializer.type_expr);
            pp_ast_root_free_internal(node->collection_initializer.size_expr);
            if (node->collection_initializer.elements) {
                for (unsigned int i = 0; i < node->collection_initializer.element_count; i++) {
                    pp_ast_root_free_internal(node->collection_initializer.elements[i]);
                }
                free(node->collection_initializer.elements);
            }
            pp_ast_decoration_free(node->collection_initializer.decoration);
            break;
        }
        default:
            break;
    }
    free(node);
}

void pp_ast_root_free(PP_AST_Node* node) { pp_ast_root_free_internal(node); }

void pp_ast_root_print(const PP_AST_Node* node, unsigned int depth) {
    if (!node) return;
    for (unsigned int i = 0; i < depth; i++) printf("\t");
    switch (node->node_type) {
        case PP_AST_ProgramNode:
            printf("Program\n");
            for (unsigned int i = 0; i < node->program.decl_count; i++)
                pp_ast_root_print(node->program.decls[i], depth + 1);
            break;
        case PP_AST_StructDeclarationNode:
            printf("StructDeclaration %s\n", node->struct_declaration.name ? node->struct_declaration.name : "");
            for (unsigned int i = 0; i < node->struct_declaration.field_count; i++) {
                PP_AST_StructField* f = &node->struct_declaration.fields[i];
                pp_ast_root_print(f->field, depth + 1);
                if (f->initializer) {
                    for (unsigned int k = 0; k < depth + 2; k++) printf("\t");
                    printf("Initializer\n");
                    pp_ast_root_print(f->initializer, depth + 2);
                }
            }
            break;
        case PP_AST_VariableDeclarationNode:
            printf("VariableDeclaration\n");
            pp_ast_root_print(node->variable_declaration.var_decl, depth + 1);
            if (node->variable_declaration.initializer)
                pp_ast_root_print(node->variable_declaration.initializer, depth + 1);
            break;
        case PP_AST_AssignmentNode: {
            const char* op_name = pp_token_to_human_name(node->assignment.op);
            printf("Assignment %s\n", op_name ? op_name : "?");
        }
            pp_ast_root_print(node->assignment.lhs, depth + 1);
            pp_ast_root_print(node->assignment.rhs, depth + 1);
            break;
        case PP_AST_ReturnStatementNode:
            printf("ReturnStatement\n");
            if (node->return_statement.expr) pp_ast_root_print(node->return_statement.expr, depth + 1);
            break;
        case PP_AST_IfStatementNode:
            printf("IfStatement\n");
            pp_ast_root_print(node->if_statement.cond, depth + 1);
            pp_ast_root_print(node->if_statement.then_branch, depth + 1);
            if (node->if_statement.else_branch) pp_ast_root_print(node->if_statement.else_branch, depth + 1);
            break;
        case PP_AST_WhileStatementNode:
            printf("WhileStatement\n");
            pp_ast_root_print(node->while_statement.cond, depth + 1);
            pp_ast_root_print(node->while_statement.body, depth + 1);
            break;
        case PP_AST_ForStatementNode:
            printf("ForStatement\n");
            if (node->for_statement.init) pp_ast_root_print(node->for_statement.init, depth + 1);
            if (node->for_statement.cond) pp_ast_root_print(node->for_statement.cond, depth + 1);
            if (node->for_statement.update) pp_ast_root_print(node->for_statement.update, depth + 1);
            pp_ast_root_print(node->for_statement.body, depth + 1);
            break;
        case PP_AST_FunctionDeclarationNode:
        case PP_AST_LambdaDeclarationNode:
        case PP_AST_OperatorDeclarationNode:
            printf("Function %s\n", node->function_declaration.name ? node->function_declaration.name : "");
            for (unsigned int i = 0; i < node->function_declaration.param_count; i++)
                pp_ast_root_print(node->function_declaration.params[i], depth + 1);
            if (node->function_declaration.return_type)
                pp_ast_root_print(node->function_declaration.return_type, depth + 1);
            if (node->function_declaration.body) pp_ast_root_print(node->function_declaration.body, depth + 1);
            if (node->function_declaration.param_symbol_table)
                pp_symbol_table_free(node->function_declaration.param_symbol_table);
            break;
        case PP_AST_ExternFunctionDeclarationNode:
        case PP_AST_ExternLambdaDeclarationNode:
        case PP_AST_ExternOperatorDeclarationNode:
            printf("Extern Function %s\n",
                   node->extern_function_declaration.name ? node->extern_function_declaration.name : "");
            for (unsigned int i = 0; i < node->extern_function_declaration.param_count; i++)
                pp_ast_root_print(node->extern_function_declaration.params[i], depth + 1);
            if (node->extern_function_declaration.return_type)
                pp_ast_root_print(node->extern_function_declaration.return_type, depth + 1);
            break;
        case PP_AST_StatementListNode:
            printf("StatementList\n");
            for (unsigned int i = 0; i < node->statement_list.stmt_count; i++)
                pp_ast_root_print(node->statement_list.stmts[i], depth + 1);
            pp_symbol_table_free(node->statement_list.symbol_table);
            break;
        case PP_AST_BinaryExpressionNode: {
            const char* op_name = pp_token_to_human_name(node->binary_expression.op);
            printf("BinaryExpression %s\n", op_name ? op_name : "?");
        }
            pp_ast_root_print(node->binary_expression.left, depth + 1);
            pp_ast_root_print(node->binary_expression.right, depth + 1);
            break;
        case PP_AST_UnaryExpressionNode: {
            const char* op_name = pp_token_to_human_name(node->unary_expression.op);
            printf("UnaryExpression %s\n", op_name ? op_name : "?");
        }
            pp_ast_root_print(node->unary_expression.operand, depth + 1);
            break;
        case PP_AST_CastExpressionNode:
            printf("CastExpression\n");
            pp_ast_root_print(node->cast_expression.expression, depth + 1);
            pp_ast_root_print(node->cast_expression.type_expr, depth + 1);
            break;
        case PP_AST_LiteralNode:
            printf("Literal %s\n", node->literal.lexeme ? node->literal.lexeme : "");
            break;
        case PP_AST_NullLiteralNode:
            printf("NullLiteral\n");
            if (node->null_literal.type_node) pp_ast_root_print(node->null_literal.type_node, depth + 1);
            break;
        case PP_AST_IdentifierNode:
            printf("Identifier %s\n", node->identifier.identifier ? node->identifier.identifier : "");
            break;
        case PP_AST_FunctionCallNode:
            printf("FunctionCall\n");
            pp_ast_root_print(node->function_call.callee, depth + 1);
            for (unsigned int i = 0; i < node->function_call.arg_count; i++)
                pp_ast_root_print(node->function_call.arguments[i], depth + 1);
            break;
        case PP_AST_MemberAccessNode:
            printf("MemberAccess %s\n", node->member_access.member ? node->member_access.member : "");
            pp_ast_root_print(node->member_access.object, depth + 1);
            break;
        case PP_AST_ArrayListAccessNode:
            printf("ArrayListAccess\n");
            pp_ast_root_print(node->array_list_access.expr, depth + 1);
            pp_ast_root_print(node->array_list_access.index, depth + 1);
            break;
        case PP_AST_ParamDeclarationNode:
            printf("Param %s\n", node->param_declaration.identifier ? node->param_declaration.identifier : "");
            if (node->param_declaration.type_expr) pp_ast_root_print(node->param_declaration.type_expr, depth + 1);
            break;
        case PP_AST_TypeExpressionNode:
            printf("TypeExpression\n");
            if (node->type_expression.type_node) pp_ast_root_print(node->type_expression.type_node, depth + 1);
            break;
        case PP_AST_TypeBaseNode:
            printf("TypeBase\n");
            break;
        case PP_AST_TypeArrayNode:
            printf("TypeArray\n");
            pp_ast_root_print(node->type_array.element_type, depth + 1);
            if (node->type_array.size_expr) pp_ast_root_print(node->type_array.size_expr, depth + 1);
            break;
        case PP_AST_TypeListNode:
            printf("TypeList\n");
            pp_ast_root_print(node->type_list.element_type, depth + 1);
            break;
        case PP_AST_TypePointerNode:
            printf("TypePointer\n");
            pp_ast_root_print(node->type_pointer.pointee_type, depth + 1);
            break;
        case PP_AST_StructInitializerNode:
            printf("StructInitializer %s\n",
                   node->struct_initializer.identifier ? node->struct_initializer.identifier : "");
            for (unsigned int i = 0; i < node->struct_initializer.entry_count; i++) {
                pp_ast_root_print(node->struct_initializer.entries[i].member_identifier, depth + 1);
                pp_ast_root_print(node->struct_initializer.entries[i].member_value, depth + 1);
            }
            break;
        case PP_AST_CollectionInitializerNode:
            printf("CollectionInitializer\n");
            if (node->collection_initializer.type_expr)
                pp_ast_root_print(node->collection_initializer.type_expr, depth + 1);
            if (node->collection_initializer.size_expr)
                pp_ast_root_print(node->collection_initializer.size_expr, depth + 1);
            for (unsigned int i = 0; i < node->collection_initializer.element_count; i++)
                pp_ast_root_print(node->collection_initializer.elements[i], depth + 1);
            break;
        default:
            printf("NodeType %d\n", node->node_type);
            break;
    }
}
