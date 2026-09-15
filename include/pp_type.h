#ifndef PP_TYPE_H
#define PP_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Forward declare PP_AST_Node for PPStructField
struct PP_AST_Node;
struct PPType;

typedef struct {
    const char* field_name;
    const struct PPType* field_type;
    const struct PP_AST_Node* default_initializer;
} PPStructField;

typedef enum {
    PP_TK_Void,
    PP_TK_Int32,
    PP_TK_Float,
    PP_TK_Bool,
    PP_TK_Struct,
    PP_TK_Array,
    PP_TK_List,
    PP_TK_Const,
    PP_TK_Reference,
    PP_TK_Newtype,
    PP_TK_Function,
    PP_TK_Lambda
} PPTypeKind;

typedef struct PPType {
    PPTypeKind kind;
    union {
        struct {
            const char* name;
            // layout (List of PPStructField)
        } pp_type_struct;

        struct {
            struct PPType* element_type;
            size_t size;
        } pp_type_array;

        struct {
            struct PPType* element_type;
        } pp_type_list;

        struct {
            struct PPType* const_of;
        } pp_type_const;

        struct {
            struct PPType* reference_to;
        } pp_type_reference;

        struct {
            const char* name;
            struct PPType* underlying;
        } pp_type_newtype;

        struct {
            struct PPType* return_type;
            // param_types: Vector of types
        } pp_type_function;

        struct {
            struct PPType* return_type;
            // param_types: Vector of types
        } pp_type_lambda;
    } type;
} PPType;

#ifdef __cplusplus
}
#endif

#endif  /* PP_TYPE_H */
