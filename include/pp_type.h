#ifndef PP_TYPE_H
#define PP_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

// Forward declare PP_AST_Node for PPStructField
struct PP_AST_Node;
struct PPSymbol;
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
    PP_TK_Int8,
    PP_TK_UInt8,
    PP_TK_Int16,
    PP_TK_UInt16,
    PP_TK_Int32,
    PP_TK_UInt32,
    PP_TK_Int64,
    PP_TK_UInt64,
    PP_TK_Int128,
    PP_TK_UInt128,
    PP_TK_Float,
    PP_TK_Bool,
    PP_TK_Struct,
    PP_TK_Array,
    PP_TK_List,
    PP_TK_Pointer,
    PP_TK_Const,
    PP_TK_Reference,
    PP_TK_Newtype,
    PP_TK_Function,
    PP_TK_String,
    PP_TK_Lambda,
    PP_TK_Operator,
} PPTypeKind;

typedef enum {
    PP_ENUM_KIND_NONE = 0,
    PP_ENUM_KIND_ENUM,
    PP_ENUM_KIND_FLAG,
} PP_EnumKind;

typedef struct {
    const char* name;
    unsigned long long value;
} PP_EnumMember;

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
            const struct PPType* pointee;
        } pp_type_pointer;

        struct {
            const struct PPType* const_of;
        } pp_type_const;

        struct {
            const struct PPType* reference_to;
        } pp_type_reference;

        struct {
            const char* name;
            const struct PPType* underlying;
            /* Non-NULL drop_func_name marks this newtype as a handle: the nominal
             * type owns a resource that the named function releases. The symbol
             * is bound in semantic Pass 2. */
            const char* drop_func_name;
            struct PPSymbol* drop_symbol;
            /* Enum / flag nominal types reuse PP_TK_Newtype storage. */
            PP_EnumKind enum_kind;
            PP_EnumMember* members;
            size_t member_count;
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

        struct {
            const struct PPType* return_type;
            const struct PPType** param_types;
            size_t param_count;
        } pp_type_operator;
    } type;
} PPType;

/* Construction / Destruction */
/** @brief Create a new PPType with the given kind. */
PPType* pp_type_create(PPTypeKind kind);
/** @brief Parse integer literal suffix from lexeme.
 * Returns PPTypeKind for the literal, or PP_TK_Void on failure.
 * If out_numeric is non-NULL, writes the numeric digits only into it.
 */
#define PP_INT_LITERAL_NUMBUF_SIZE 64
PPTypeKind pp_type_parse_int_literal_suffix(const char* lexeme, char* out_numeric);
/** @brief Return true if the type kind represents a primitive integer */
static inline bool pp_type_kind_is_integer(PPTypeKind k) {
    return k == PP_TK_Int8 || k == PP_TK_UInt8 || k == PP_TK_Int16 || k == PP_TK_UInt16 || k == PP_TK_Int32 ||
           k == PP_TK_UInt32 || k == PP_TK_Int64 || k == PP_TK_UInt64 || k == PP_TK_Int128 || k == PP_TK_UInt128;
}
/** @brief Return true if the type kind represents an unsigned integer */
static inline bool pp_type_kind_is_unsigned(PPTypeKind k) {
    return k == PP_TK_UInt8 || k == PP_TK_UInt16 || k == PP_TK_UInt32 || k == PP_TK_UInt64 || k == PP_TK_UInt128;
}
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
int pp_struct_layout_push_field(PP_StructLayout* layout, const char* field_name, const PPType* field_type,
                                const struct PP_AST_Node* default_initializer);

/** @brief Append a parameter type to a function or lambda type. Reallocates the
 * param_types array. Returns 1 on success, 0 on failure. */
int pp_type_push_param(PPType* type, const PPType* param_type);

bool pp_type_is_const(const PPType* type);
bool pp_type_is_reference(const PPType* type);
/** @brief A handle is a newtype with a bound drop function. Handles are
 * non-copyable, non-trivially-copyable and always need drop. */
bool pp_type_is_handle(const PPType* type);
bool pp_type_is_pointer(const PPType* type);
/** @brief Enum / flag nominal type queries. */
bool pp_type_is_enum(const PPType* type);
bool pp_type_is_flag(const PPType* type);
bool pp_type_is_enum_or_flag(const PPType* type);
const PP_EnumMember* pp_type_find_enum_member(const PPType* type, const char* name);
bool pp_type_contains_pointer(const PPType* type);
bool pp_type_is_numerical(const PPType* type);
bool pp_type_is_unsigned_integer(const PPType* type);
bool pp_type_needs_drop(const PPType* type);
bool pp_type_is_trivially_copyable(const PPType* type);
bool pp_type_is_copyable(const PPType* type);

/**
 * @brief Resolve the underlying machine type of a possibly chained newtype.
 * Walks through PP_TK_Newtype wrappers until a non-newtype type is reached.
 * Used for cast legality where nominal types are ignored.
 */
const PPType* pp_type_resolve_machine_type(const PPType* t);

#ifdef __cplusplus
}
#endif

#endif /* PP_TYPE_H */
