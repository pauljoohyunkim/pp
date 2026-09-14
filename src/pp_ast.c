#include "pp_ast.h"
#include <stdlib.h>
#include <stdio.h>

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr) == NULL) goto error_cleanup; } while(0)

PP_AST_Decoration* pp_ast_decoration_create(const void* resolved_type, PP_ValueCategory value_category, bool is_constexpr, unsigned int scope_level) {
    PP_AST_Decoration* d = (PP_AST_Decoration*)calloc(1, sizeof(PP_AST_Decoration));
    if (!d) return NULL;
    d->resolved_type = resolved_type;
    d->value_category = value_category;
    d->is_constexpr = is_constexpr;
    (void)scope_level;
    return d;
}

void pp_ast_decoration_free(PP_AST_Decoration* decor) {
    if (decor) free(decor);
}

PP_AST_Node* pp_ast_node_create(PP_AST_NodeType node_type, unsigned int line, unsigned int col) {
    PP_AST_Node* node = (PP_AST_Node*)calloc(1, sizeof(PP_AST_Node));
    if (!node) return NULL;
    node->node_type = node_type;
    node->line = line;
    node->col = col;
    return node;
}

static void pp_ast_root_free_internal(PP_AST_Node* node) {
    if (!node) return;
    switch (node->node_type) {
        case PP_AST_ProgramNode: {
            for (unsigned int i = 0; i < node->program.decl_count; i++) {
                pp_ast_root_free_internal(node->program.decls[i]);
            }
            free(node->program.decls);
            break;
        }
        case PP_AST_FunctionDeclarationNode:
        case PP_AST_LambdaDeclarationNode: {
            for (unsigned int i = 0; i < node->function.param_count; i++) {
                pp_ast_root_free_internal(node->function.params[i]);
            }
            free(node->function.params);
            free(node->function.name);
            pp_ast_root_free_internal(node->function.return_type);
            pp_ast_root_free_internal(node->function.body);
            break;
        }
        case PP_AST_StatementListNode: {
            for (unsigned int i = 0; i < node->statement_list.stmt_count; i++) {
                pp_ast_root_free_internal(node->statement_list.stmts[i]);
            }
            free(node->statement_list.stmts);
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
            break;
        }
        case PP_AST_BinaryExpressionNode: {
            pp_ast_root_free_internal(node->binary_expression.left);
            pp_ast_root_free_internal(node->binary_expression.right);
            if (node->binary_expression.decoration) pp_ast_decoration_free(node->binary_expression.decoration);
            break;
        }
        case PP_AST_UnaryExpressionNode: {
            pp_ast_root_free_internal(node->unary_expression.operand);
            if (node->unary_expression.decoration) pp_ast_decoration_free(node->unary_expression.decoration);
            break;
        }
        case PP_AST_CastExpressionNode: {
            pp_ast_root_free_internal(node->cast_expression.expression);
            pp_ast_root_free_internal(node->cast_expression.type_expr);
            if (node->cast_expression.decoration) pp_ast_decoration_free(node->cast_expression.decoration);
            break;
        }
        case PP_AST_LiteralNode: {
            free(node->literal.lexeme);
            if (node->literal.decoration) pp_ast_decoration_free(node->literal.decoration);
            break;
        }
        case PP_AST_IdentifierNode: {
            free(node->identifier.identifier);
            if (node->identifier.decoration) pp_ast_decoration_free(node->identifier.decoration);
            break;
        }
        case PP_AST_FunctionCallNode: {
            pp_ast_root_free_internal(node->function_call.callee);
            for (unsigned int i = 0; i < node->function_call.arg_count; i++) {
                pp_ast_root_free_internal(node->function_call.arguments[i]);
            }
            free(node->function_call.arguments);
            if (node->function_call.decoration) pp_ast_decoration_free(node->function_call.decoration);
            break;
        }
        case PP_AST_MemberAccessNode: {
            pp_ast_root_free_internal(node->member_access.object);
            free(node->member_access.member);
            if (node->member_access.decoration) pp_ast_decoration_free(node->member_access.decoration);
            break;
        }
        case PP_AST_ArrayListAccessNode: {
            pp_ast_root_free_internal(node->array_list_access.expr);
            pp_ast_root_free_internal(node->array_list_access.index);
            break;
        }
        case PP_AST_ParamDeclarationNode: {
            free(node->param_declaration.identifier);
            pp_ast_root_free_internal(node->param_declaration.type_expr);
            break;
        }
        case PP_AST_TypeExpressionNode: {
            free(node->type_expression.identifier_name);
            pp_ast_root_free_internal(node->type_expression.element_type);
            pp_ast_root_free_internal(node->type_expression.array_spec.size_expr);
            break;
        }
        case PP_AST_StructDeclarationNode: {
            free(node->struct_declaration.name);
            for (unsigned int i = 0; i < node->struct_declaration.field_count; i++) {
                pp_ast_root_free_internal(node->struct_declaration.fields[i].field);
                pp_ast_root_free_internal(node->struct_declaration.fields[i].initializer);
            }
            free(node->struct_declaration.fields);
            break;
        }
        case PP_AST_StructInitializerNode: {
            free(node->struct_initializer.identifier);
            for (unsigned int i = 0; i < node->struct_initializer.entry_count; i++) {
                pp_ast_root_free_internal(node->struct_initializer.entries[i].member_identifier);
                pp_ast_root_free_internal(node->struct_initializer.entries[i].member_value);
            }
            free(node->struct_initializer.entries);
            if (node->struct_initializer.decoration) pp_ast_decoration_free(node->struct_initializer.decoration);
            break;
        }
        case PP_AST_CollectionInitializerNode: {
            pp_ast_root_free_internal(node->collection_initializer.type_expr);
            pp_ast_root_free_internal(node->collection_initializer.size_expr);
            for (unsigned int i = 0; i < node->collection_initializer.element_count; i++) {
                pp_ast_root_free_internal(node->collection_initializer.elements[i]);
            }
            free(node->collection_initializer.elements);
            if (node->collection_initializer.decoration) pp_ast_decoration_free(node->collection_initializer.decoration);
            break;
        }
        default:
            break;
    }
    free(node);
}

void pp_ast_root_free(PP_AST_Node* node) {
    pp_ast_root_free_internal(node);
}

void pp_ast_root_print(const PP_AST_Node* node, unsigned int depth) {
    if (!node) return;
    for (unsigned int i = 0; i < depth; i++) printf("\t");
    switch (node->node_type) {
        case PP_AST_ProgramNode:
            printf("Program\n");
            for (unsigned int i = 0; i < node->program.decl_count; i++) pp_ast_root_print(node->program.decls[i], depth+1);
            break;
        case PP_AST_StructDeclarationNode:
            printf("StructDeclaration %s\n", node->struct_declaration.name ? node->struct_declaration.name : "");
            for (unsigned int i = 0; i < node->struct_declaration.field_count; i++) {
                PP_AST_StructField* f = &node->struct_declaration.fields[i];
                pp_ast_root_print(f->field, depth+1);
                if (f->initializer) {
                    for (unsigned int k = 0; k < depth+2; k++) printf("\t");
                    printf("Initializer\n");
                    pp_ast_root_print(f->initializer, depth+2);
                }
            }
            break;
        case PP_AST_VariableDeclarationNode:
            printf("VariableDeclaration\n");
            pp_ast_root_print(node->variable_declaration.var_decl, depth+1);
            if (node->variable_declaration.initializer) pp_ast_root_print(node->variable_declaration.initializer, depth+1);
            break;
        case PP_AST_AssignmentNode:
            printf("Assignment\n");
            pp_ast_root_print(node->assignment.lhs, depth+1);
            pp_ast_root_print(node->assignment.rhs, depth+1);
            break;
        case PP_AST_ReturnStatementNode:
            printf("ReturnStatement\n");
            if (node->return_statement.expr) pp_ast_root_print(node->return_statement.expr, depth+1);
            break;
        case PP_AST_IfStatementNode:
            printf("IfStatement\n");
            pp_ast_root_print(node->if_statement.cond, depth+1);
            pp_ast_root_print(node->if_statement.then_branch, depth+1);
            if (node->if_statement.else_branch) pp_ast_root_print(node->if_statement.else_branch, depth+1);
            break;
        case PP_AST_WhileStatementNode:
            printf("WhileStatement\n");
            pp_ast_root_print(node->while_statement.cond, depth+1);
            pp_ast_root_print(node->while_statement.body, depth+1);
            break;
        case PP_AST_ForStatementNode:
            printf("ForStatement\n");
            if (node->for_statement.init) pp_ast_root_print(node->for_statement.init, depth+1);
            if (node->for_statement.cond) pp_ast_root_print(node->for_statement.cond, depth+1);
            if (node->for_statement.update) pp_ast_root_print(node->for_statement.update, depth+1);
            pp_ast_root_print(node->for_statement.body, depth+1);
            break;
        case PP_AST_FunctionDeclarationNode:
        case PP_AST_LambdaDeclarationNode:
            printf("Function %s\n", node->function.name ? node->function.name : "");
            for (unsigned int i = 0; i < node->function.param_count; i++) pp_ast_root_print(node->function.params[i], depth+1);
            if (node->function.return_type) pp_ast_root_print(node->function.return_type, depth+1);
            if (node->function.body) pp_ast_root_print(node->function.body, depth+1);
            break;
        case PP_AST_StatementListNode:
            printf("StatementList\n");
            for (unsigned int i = 0; i < node->statement_list.stmt_count; i++) pp_ast_root_print(node->statement_list.stmts[i], depth+1);
            break;
        case PP_AST_BinaryExpressionNode:
            printf("BinaryExpression\n");
            pp_ast_root_print(node->binary_expression.left, depth+1);
            pp_ast_root_print(node->binary_expression.right, depth+1);
            break;
        case PP_AST_UnaryExpressionNode:
            printf("UnaryExpression\n");
            pp_ast_root_print(node->unary_expression.operand, depth+1);
            break;
        case PP_AST_CastExpressionNode:
            printf("CastExpression\n");
            pp_ast_root_print(node->cast_expression.expression, depth+1);
            pp_ast_root_print(node->cast_expression.type_expr, depth+1);
            break;
        case PP_AST_LiteralNode:
            printf("Literal %s\n", node->literal.lexeme ? node->literal.lexeme : "");
            break;
        case PP_AST_IdentifierNode:
            printf("Identifier %s\n", node->identifier.identifier ? node->identifier.identifier : "");
            break;
        case PP_AST_FunctionCallNode:
            printf("FunctionCall\n");
            pp_ast_root_print(node->function_call.callee, depth+1);
            for (unsigned int i = 0; i < node->function_call.arg_count; i++) pp_ast_root_print(node->function_call.arguments[i], depth+1);
            break;
        case PP_AST_MemberAccessNode:
            printf("MemberAccess %s\n", node->member_access.member ? node->member_access.member : "");
            pp_ast_root_print(node->member_access.object, depth+1);
            break;
        case PP_AST_ArrayListAccessNode:
            printf("ArrayListAccess\n");
            pp_ast_root_print(node->array_list_access.expr, depth+1);
            pp_ast_root_print(node->array_list_access.index, depth+1);
            break;
        case PP_AST_ParamDeclarationNode:
            printf("Param %s\n", node->param_declaration.identifier ? node->param_declaration.identifier : "");
            if (node->param_declaration.type_expr) pp_ast_root_print(node->param_declaration.type_expr, depth+1);
            break;
        case PP_AST_TypeExpressionNode:
            printf("TypeExpression\n");
            break;
        case PP_AST_StructInitializerNode:
            printf("StructInitializer %s\n", node->struct_initializer.identifier ? node->struct_initializer.identifier : "");
            for (unsigned int i = 0; i < node->struct_initializer.entry_count; i++) {
                pp_ast_root_print(node->struct_initializer.entries[i].member_identifier, depth+1);
                pp_ast_root_print(node->struct_initializer.entries[i].member_value, depth+1);
            }
            break;
        case PP_AST_CollectionInitializerNode:
            printf("CollectionInitializer\n");
            if (node->collection_initializer.type_expr) pp_ast_root_print(node->collection_initializer.type_expr, depth+1);
            if (node->collection_initializer.size_expr) pp_ast_root_print(node->collection_initializer.size_expr, depth+1);
            for (unsigned int i = 0; i < node->collection_initializer.element_count; i++) pp_ast_root_print(node->collection_initializer.elements[i], depth+1);
            break;
        default:
            printf("NodeType %d\n", node->node_type);
            break;
    }
}
