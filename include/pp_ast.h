#ifndef PP_AST_H
#define PP_AST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "pp_tokens.h"
#include "pp_string_pool.h"
#include "pp_type.h"

#ifndef AST_VECTOR_INIT_CAP
#define AST_VECTOR_INIT_CAP 8
#endif

typedef struct PP_AST_Node PP_AST_Node;

typedef enum {
    PP_VALUE_CATEGORY_LVALUE,
    PP_VALUE_CATEGORY_RVALUE
} PP_ValueCategory;

typedef struct {
    const PPType* resolved_type;
    PP_ValueCategory value_category;
    bool is_constexpr;
    bool is_escapable_ref;
    /* unsigned int scope_level; // reserved */
} PP_AST_Decoration;

typedef struct {
    PP_AST_Node* field;
    PP_AST_Node* initializer; /* optional */
} PP_AST_StructField;

typedef struct {
    PP_AST_Node* member_identifier;
    PP_AST_Node* member_value;
} PP_AST_StructInitEntry;

typedef enum {
    PP_ARRAY_SPEC_EMPTY,
    PP_ARRAY_SPEC_SIZE
} PP_ArraySpecKind;

typedef struct {
    PP_ArraySpecKind kind;
    PP_AST_Node* size_expr; /* valid if kind == PP_ARRAY_SPEC_SIZE */
} PP_AST_ArrayListSpecifier;

typedef enum {
    PP_AST_ProgramNode,
    PP_AST_StructDeclarationNode,
    PP_AST_VariableDeclarationNode,
    PP_AST_AssignmentNode,
    PP_AST_ReturnStatementNode,
    PP_AST_IfStatementNode,
    PP_AST_WhileStatementNode,
    PP_AST_ForStatementNode,
    PP_AST_TypeExpressionNode,
    PP_AST_TypedefDeclarationNode,
    PP_AST_NewtypeDeclarationNode,
    PP_AST_FunctionDeclarationNode,
    PP_AST_LambdaDeclarationNode,
    PP_AST_StatementListNode,
    PP_AST_BinaryExpressionNode,
    PP_AST_UnaryExpressionNode,
    PP_AST_CastExpressionNode,
    PP_AST_LiteralNode,
    PP_AST_IdentifierNode,
    PP_AST_FunctionCallNode,
    PP_AST_MemberAccessNode,
    PP_AST_ArrayListAccessNode,
    PP_AST_ParamDeclarationNode,
    PP_AST_StructInitializerNode,
    PP_AST_CollectionInitializerNode
} PP_AST_NodeType;

typedef enum {
    PP_AST_TYPE_KIND_VOID,
    PP_AST_TYPE_KIND_INT32,
    PP_AST_TYPE_KIND_FLOAT,
    PP_AST_TYPE_KIND_BOOL,
    PP_AST_TYPE_KIND_IDENTIFIER
} PP_AST_TypeKind;

struct PP_AST_Node {
    PP_AST_NodeType node_type;
    unsigned int line;
    unsigned int col;

    union {
        struct { /* Program */
            PP_AST_Node** decls;
            unsigned int decl_count;
            unsigned int capacity;
        } program;

        struct { /* StructDeclaration */
            const char* name;
            PP_AST_StructField* fields;
            unsigned int field_count;
            unsigned int field_capacity;
        } struct_declaration;

        struct { /* VariableDeclaration */
            PP_AST_Node* var_decl;      /* ParamDeclaration */
            PP_AST_Node* initializer;   /* optional expression */
        } variable_declaration;

        struct { /* Assignment */
            PP_AST_Node* lhs;
            PP_TokenType op; /* assignment op token type */
            PP_AST_Node* rhs;
        } assignment;

        struct { /* ReturnStatement */
            PP_AST_Node* expr;
        } return_statement;

        struct { /* IfStatement */
            PP_AST_Node* cond;
            PP_AST_Node* then_branch; /* StatementList */
            PP_AST_Node* else_branch; /* optional */
        } if_statement;

        struct { /* WhileStatement */
            PP_AST_Node* cond;
            PP_AST_Node* body; /* StatementList */
        } while_statement;

        struct { /* ForStatement */
            PP_AST_Node* init;    /* optional VariableDeclaration or Assignment */
            PP_AST_Node* cond;    /* optional expression */
            PP_AST_Node* update;  /* optional expression/assignment */
            PP_AST_Node* body;    /* StatementList */
        } for_statement;

        struct { /* TypeExpression */
            bool is_const;
            bool is_reference;
            const char* identifier_name; /* identifier for named types, NULL for primitives */
            PP_AST_TypeKind kind;
            PP_AST_ArrayListSpecifier* array_spec; /* NULL = plain type */
        } type_expression;

        struct { /* TypedefDeclaration */
            PP_AST_Node* base_type;
            const char* identifier;
        } typedef_declaration;

        struct { /* NewtypeDeclaration */
            PP_AST_Node* base_type;
            const char* identifier;
        } newtype_declaration;

        struct { /* FunctionDeclaration */
            const char* name;
            PP_AST_Node** params; /* ParamDeclaration nodes */
            unsigned int param_count;
            unsigned int param_capacity;
            PP_AST_Node* return_type;
            PP_AST_Node* body; /* StatementList */
        } function_declaration;

        struct { /* StatementList */
            PP_AST_Node** stmts;
            unsigned int stmt_count;
            unsigned int capacity;
        } statement_list;

        struct { /* BinaryExpression */
            PP_TokenType op;
            PP_AST_Node* left;
            PP_AST_Node* right;
            PP_AST_Decoration* decoration;
        } binary_expression;

        struct { /* UnaryExpression */
            PP_TokenType op;
            PP_AST_Node* operand;
            PP_AST_Decoration* decoration;
        } unary_expression;

        struct { /* CastExpression */
            PP_AST_Node* expression;
            PP_AST_Node* type_expr;
            PP_AST_Decoration* decoration;
        } cast_expression;

        struct { /* Literal */
            PP_TokenType literal_type;
            const char* lexeme;
            PP_AST_Decoration* decoration;
        } literal;

        struct { /* Identifier */
            const char* identifier;
            PP_AST_Decoration* decoration;
        } identifier;

        struct { /* FunctionCall */
            PP_AST_Node* callee;
            PP_AST_Node** arguments;
            unsigned int arg_count;
            unsigned int arg_capacity;
            PP_AST_Decoration* decoration;
        } function_call;

        struct { /* MemberAccess */
            PP_AST_Node* object;
            const char* member;
            PP_AST_Decoration* decoration;
        } member_access;

        struct { /* ArrayListAccess */
            PP_AST_Node* expr;
            PP_AST_Node* index;
            PP_AST_Decoration* decoration;
        } array_list_access;

        struct { /* ParamDeclaration */
            const char* identifier;
            PP_AST_Node* type_expr;
            //const PPType* resolved_type;
        } param_declaration;

        struct { /* StructInitializer */
            const char* identifier;
            PP_AST_StructInitEntry* entries;
            unsigned int entry_count;
            unsigned int entry_capacity;
            PP_AST_Decoration* decoration;
        } struct_initializer;

        struct { /* CollectionInitializer */
            PP_AST_Node* type_expr;
            PP_AST_Node* size_expr; /* optional */
            PP_AST_Node** elements;
            unsigned int element_count;
            unsigned int element_capacity;
            PP_AST_Decoration* decoration;
        } collection_initializer;
    };
};

PP_AST_Decoration* pp_ast_decoration_create(const void* resolved_type, PP_ValueCategory value_category, bool is_constexpr, bool is_escapable_ref);
void pp_ast_decoration_free(PP_AST_Decoration* decor);

const PP_AST_Decoration* pp_ast_get_decoration(const PP_AST_Node *node);

PP_AST_Node* pp_ast_node_create(PP_AST_NodeType node_type, unsigned int line, unsigned int col);
void pp_ast_root_free(PP_AST_Node* node);
void pp_ast_root_print(const PP_AST_Node* node, unsigned int depth);

#ifdef __cplusplus
}
#endif

#endif /* PP_AST_H */
