#ifndef PP_TYPE_H
#define PP_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

// Forward declare PP_AST_Node for PPStructField
struct PP_AST_Node;
struct PPType;

typedef struct {
    const char* field_name;
    const struct PPType* field_type;
    const struct PP_AST_Node* default_initializer;
} PPStructField;

typedef struct {
    PPStructField* fields;
    size_t field_count;
} PP_StructLayout;

typedef enum {
    PP_TK_Void,
    PP_TK_Int32,
    PP_TK_UInt32,
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
            const PP_StructLayout* layout;
        } pp_type_struct;

        struct {
            const struct PPType* element_type;
            size_t size;
        } pp_type_array;

        struct {
            const struct PPType* element_type;
        } pp_type_list;

        struct {
            const struct PPType* const_of;
        } pp_type_const;

        struct {
            const struct PPType* reference_to;
        } pp_type_reference;

        struct {
            const char* name;
            const struct PPType* underlying;
        } pp_type_newtype;

        struct {
            const struct PPType* return_type;
            const struct PPType** param_types;
            size_t param_count;
        } pp_type_function;

        struct {
            const struct PPType* return_type;
            const struct PPType** param_types;
            size_t param_count;
        } pp_type_lambda;
    } type;
} PPType;

/* Construction / Destruction */
/** @brief Create a new PPType with the given kind. */
PPType* pp_type_create(PPTypeKind kind);
/** @brief Free a PPType. Shallow free – internal owned allocations such as
 * struct layouts and param arrays are freed, referenced types remain owned by
 * the global type table. */
void pp_type_free(PPType* type);

/** @brief Create an empty struct layout. */
PP_StructLayout* pp_struct_layout_create(void);
/** @brief Free a struct layout and its fields array. */
void pp_struct_layout_free(PP_StructLayout* layout);
/** @brief Append a field to a struct layout. Reallocates the fields array.
 * Returns 1 on success, 0 on failure. */
int pp_struct_layout_push_field(PP_StructLayout* layout, const char* field_name,
                                const PPType* field_type,
                                const struct PP_AST_Node* default_initializer);

/** @brief Append a parameter type to a function or lambda type. Reallocates the
 * param_types array. Returns 1 on success, 0 on failure. */
int pp_type_push_param(PPType* type, const PPType* param_type);

bool pp_type_is_const(const PPType* type);
bool pp_type_is_reference(const PPType* type);
bool pp_type_is_numerical(const PPType* type);

#ifdef __cplusplus
}
#endif

#endif /* PP_TYPE_H */
