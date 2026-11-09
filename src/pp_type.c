#include "pp_type.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pp_type_table.h"

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

#define PP_TYPE_VISIT_STACK_MAX 128
#if PP_TYPE_VISIT_STACK_MAX < 256
#warning "PP_TYPE_VISIT_STACK_MAX is < 256; consider increasing for deep type graphs"
#endif

static bool pp_type_needs_drop_impl(const PPType* type, const PPType** stack, int depth);
static bool pp_type_is_copyable_impl(const PPType* type, const PPType** stack, int depth);
static bool pp_type_is_trivially_copyable_impl(const PPType* type, const PPType** stack, int depth);

/** @brief Create a new PPType with the given kind. */
PPType* pp_type_create(PPTypeKind kind) {
    PPType* type = NULL;
    type = (PPType*)calloc(1, sizeof(PPType));
    NULL_POINTER_ERROR_HANDLE(type);
    type->kind = kind;
    return type;
error_cleanup:
    free(type);
    return NULL;
}

/** @brief Free a PPType. Shallow free – internal owned allocations such as
 * struct layouts and param arrays are freed, referenced types remain owned by
 * the global type table. */
void pp_type_free(PPType* type) {
    if (type != NULL) {
        switch (type->kind) {
            case PP_TK_Struct:
                /* Layout is owned by the type in this context; free it if
                 * present */
                if (type->type.pp_type_struct.layout != NULL) {
                    pp_struct_layout_free((PP_StructLayout*)type->type.pp_type_struct.layout);
                }
                break;
            case PP_TK_Function:
            case PP_TK_Lambda:
            case PP_TK_Operator:
                free(type->type.pp_type_function.param_types);
                break;
            default:
                break;
        }
        free(type);
    }
}

/** @brief Create an empty struct layout. */
PP_StructLayout* pp_struct_layout_create(void) {
    PP_StructLayout* layout = NULL;
    layout = (PP_StructLayout*)calloc(1, sizeof(PP_StructLayout));
    NULL_POINTER_ERROR_HANDLE(layout);
    layout->fields = NULL;
    layout->field_count = 0;
    return layout;
error_cleanup:
    free(layout);
    return NULL;
}

/** @brief Free a struct layout and its fields array. */
void pp_struct_layout_free(PP_StructLayout* layout) {
    if (layout != NULL) {
        free(layout->fields);
        layout->fields = NULL;
        free(layout);
    }
}

/** @brief Append a field to a struct layout. Reallocates the fields array.
 * Returns 1 on success, 0 on failure. */
int pp_struct_layout_push_field(PP_StructLayout* layout, const char* field_name, const PPType* field_type,
                                const struct PP_AST_Node* default_initializer) {
    if (layout == NULL) return 0;
    PPStructField* new_fields = NULL;
    size_t new_count = layout->field_count + 1;
    new_fields = (PPStructField*)realloc(layout->fields, new_count * sizeof(PPStructField));
    if (new_fields == NULL) return 0;
    layout->fields = new_fields;
    layout->fields[layout->field_count].field_name = field_name;
    layout->fields[layout->field_count].field_type = field_type;
    layout->fields[layout->field_count].default_initializer = default_initializer;
    layout->field_count = new_count;
    return 1;
}

/** @brief Append a parameter type to a function or lambda type. Reallocates the
 * param_types array. Returns 1 on success, 0 on failure. */
int pp_type_push_param(PPType* type, const PPType* param_type) {
    if (type == NULL || param_type == NULL) return 0;
    if (type->kind != PP_TK_Function && type->kind != PP_TK_Lambda && type->kind != PP_TK_Operator) return 0;
    size_t new_count = type->type.pp_type_function.param_count + 1;
    const PPType** new_params =
        (const PPType**)realloc(type->type.pp_type_function.param_types, new_count * sizeof(PPType*));
    if (new_params == NULL) return 0;
    type->type.pp_type_function.param_types = new_params;
    type->type.pp_type_function.param_types[type->type.pp_type_function.param_count] = param_type;
    type->type.pp_type_function.param_count = new_count;
    return 1;
}

bool pp_type_is_const(const PPType* type) {
    if (type == NULL) return false;

    switch (type->kind) {
        case PP_TK_Reference:
            return pp_type_is_const(type->type.pp_type_reference.reference_to);
        case PP_TK_Array:
            return pp_type_is_const(type->type.pp_type_array.element_type);
        case PP_TK_List:
            return pp_type_is_const(type->type.pp_type_list.element_type);
        case PP_TK_Newtype:
            return pp_type_is_const(type->type.pp_type_newtype.underlying);
        case PP_TK_Const:
            return true;
        default:
            return false;
    }
}

bool pp_type_is_reference(const PPType* type) {
    if (type == NULL) return false;

    return type->kind == PP_TK_Reference;
}

bool pp_type_is_numerical(const PPType* type) {
    if (type == NULL) return false;

    switch (type->kind) {
        case PP_TK_Float:
        case PP_TK_Int8:
        case PP_TK_UInt8:
        case PP_TK_Int16:
        case PP_TK_UInt16:
        case PP_TK_Int32:
        case PP_TK_UInt32:
        case PP_TK_Int64:
        case PP_TK_UInt64:
        case PP_TK_Int128:
        case PP_TK_UInt128:
            return true;
        default:
            return false;
    }
}

bool pp_type_is_unsigned_integer(const PPType* type) {
    if (!type) return false;
    switch (type->kind) {
        case PP_TK_UInt8:
        case PP_TK_UInt16:
        case PP_TK_UInt32:
        case PP_TK_UInt64:
        case PP_TK_UInt128:
            return true;
        default:
            return false;
    }
}

bool pp_type_needs_drop(const PPType* type) {
    if (type == NULL) return false;
    const PPType* stack[PP_TYPE_VISIT_STACK_MAX] = {0};
    return pp_type_needs_drop_impl(type, stack, 0);
}

static bool pp_type_needs_drop_impl(const PPType* type, const PPType** stack, int depth) {
    if (type == NULL) return false;
    if (depth > 0) {
        for (int i = 0; i < depth; i++) {
            if (stack[i] == type) return false;
        }
    }
    if (depth >= PP_TYPE_VISIT_STACK_MAX) {
        fprintf(stderr, "WARN: pp_type_needs_drop depth >= %d\n", PP_TYPE_VISIT_STACK_MAX);
        return false;
    }
    stack[depth] = type;

    switch (type->kind) {
        case PP_TK_List:
            return true;
        case PP_TK_String:
            return true;
        case PP_TK_Const:
            return pp_type_needs_drop_impl(type->type.pp_type_const.const_of, stack, depth + 1);
        case PP_TK_Array:
            return pp_type_needs_drop_impl(type->type.pp_type_array.element_type, stack, depth + 1);
        case PP_TK_Newtype:
            return pp_type_needs_drop_impl(type->type.pp_type_newtype.underlying, stack, depth + 1);
        case PP_TK_Struct: {
            const PP_StructLayout* layout = type->type.pp_type_struct.layout;
            if (!layout) return false;
            for (size_t i = 0; i < layout->field_count; i++) {
                if (pp_type_needs_drop_impl(layout->fields[i].field_type, stack, depth + 1)) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool pp_type_is_trivially_copyable(const PPType* type) {
    if (type == NULL) return false;
    const PPType* stack[PP_TYPE_VISIT_STACK_MAX] = {0};
    return pp_type_is_trivially_copyable_impl(type, stack, 0);
}

bool pp_type_is_copyable(const PPType* type) {
    if (type == NULL) return false;
    if (pp_type_is_reference(type)) return false;
    // Trivially copyable types are always copyable
    if (pp_type_is_trivially_copyable(type)) return true;
    // Types that need drop must be checked for deep-copy support
    if (!pp_type_needs_drop(type)) return true;  // No ownership, trivially copyable already handled

    const PPType* stack[PP_TYPE_VISIT_STACK_MAX] = {0};
    return pp_type_is_copyable_impl(type, stack, 0);
}

static bool pp_type_is_copyable_impl(const PPType* type, const PPType** stack, int depth) {
    if (type == NULL) return false;
    if (pp_type_is_reference(type)) return false;
    if (pp_type_is_trivially_copyable_impl(type, stack, depth)) return true;
    if (!pp_type_needs_drop_impl(type, stack, depth)) return true;

    if (depth > 0) {
        for (int i = 0; i < depth; i++) {
            if (stack[i] == type) return true;
        }
    }
    if (depth >= PP_TYPE_VISIT_STACK_MAX) {
        fprintf(stderr, "WARN: pp_type_is_copyable depth >= %d\n", PP_TYPE_VISIT_STACK_MAX);
        return true;
    }
    stack[depth] = type;

    switch (type->kind) {
        case PP_TK_List: {
            const PPType* elem = type->type.pp_type_list.element_type;
            return pp_type_is_copyable_impl(elem, stack, depth + 1);
        }
        case PP_TK_Struct: {
            const PP_StructLayout* layout = type->type.pp_type_struct.layout;
            if (!layout) return false;
            for (size_t i = 0; i < layout->field_count; i++) {
                const PPType* ft = layout->fields[i].field_type;
                if (!pp_type_is_copyable_impl(ft, stack, depth + 1)) return false;
            }
            return true;
        }
        case PP_TK_Newtype:
            return pp_type_is_copyable_impl(type->type.pp_type_newtype.underlying, stack, depth + 1);
        case PP_TK_Const:
            return pp_type_is_copyable_impl(type->type.pp_type_const.const_of, stack, depth + 1);
        case PP_TK_Array:
            return pp_type_is_copyable_impl(type->type.pp_type_array.element_type, stack, depth + 1);
        default:
            return false;
    }
}

static bool pp_type_is_trivially_copyable_impl(const PPType* type, const PPType** stack, int depth) {
    if (type == NULL) return false;
    if (pp_type_is_reference(type)) return false;
    if (depth > 0) {
        for (int i = 0; i < depth; i++) {
            if (stack[i] == type) return true;
        }
    }
    if (depth >= PP_TYPE_VISIT_STACK_MAX) {
        fprintf(stderr, "WARN: pp_type_is_trivially_copyable depth >= %d\n", PP_TYPE_VISIT_STACK_MAX);
        return true;
    }
    stack[depth] = type;

    switch (type->kind) {
        case PP_TK_Int8:
        case PP_TK_UInt8:
        case PP_TK_Int16:
        case PP_TK_UInt16:
        case PP_TK_Int32:
        case PP_TK_UInt32:
        case PP_TK_Int64:
        case PP_TK_UInt64:
        case PP_TK_Int128:
        case PP_TK_UInt128:
        case PP_TK_Float:
        case PP_TK_Bool:
        case PP_TK_Void:
            return true;
        case PP_TK_Const:
            return pp_type_is_trivially_copyable_impl(type->type.pp_type_const.const_of, stack, depth + 1);
        case PP_TK_Newtype:
            return pp_type_is_trivially_copyable_impl(type->type.pp_type_newtype.underlying, stack, depth + 1);
        case PP_TK_Array:
            return pp_type_is_trivially_copyable_impl(type->type.pp_type_array.element_type, stack, depth + 1);
        case PP_TK_Struct: {
            const PP_StructLayout* layout = type->type.pp_type_struct.layout;
            if (!layout) return false;
            for (size_t i = 0; i < layout->field_count; i++) {
                const PPType* ft = layout->fields[i].field_type;
                if (!pp_type_is_trivially_copyable_impl(ft, stack, depth + 1)) return false;
                if (pp_type_needs_drop_impl(ft, stack, depth + 1)) return false;
            }
            return true;
        }
        case PP_TK_List:
            return false;
        case PP_TK_String:
            return true;
        default:
            return false;
    }
}

PPTypeKind pp_type_parse_int_literal_suffix(const char* lexeme, char* out_numeric) {
    if (!lexeme) return PP_TK_Void;
    size_t len = strlen(lexeme);
    if (len == 0) {
        if (out_numeric) out_numeric[0] = '\0';
        return PP_TK_Int32;
    }
    size_t pos = 0;
    while (pos < len && isdigit((unsigned char)lexeme[pos])) pos++;
    bool is_unsigned = false;
    if (pos < len && tolower((unsigned char)lexeme[pos]) == 'u') {
        is_unsigned = true;
        pos++;
    }
    size_t suffix_len = 0;
    if (len >= pos + 2) {
        char c1 = tolower((unsigned char)lexeme[len - 2]);
        char c2 = tolower((unsigned char)lexeme[len - 1]);
        if ((c1 == 's' && c2 == 's') || (c1 == 'l' && c2 == 'l')) { suffix_len = 2; }
    }
    if (suffix_len == 0 && len > pos) {
        char last = tolower((unsigned char)lexeme[len - 1]);
        if (last == 's' || last == 'l') suffix_len = 1;
    }
    PPTypeKind kind;
    if (suffix_len == 0) {
        kind = is_unsigned ? PP_TK_UInt32 : PP_TK_Int32;
    } else if (suffix_len == 2) {
        char s = tolower((unsigned char)lexeme[len - 2]);
        if (s == 's')
            kind = is_unsigned ? PP_TK_UInt8 : PP_TK_Int8;
        else if (s == 'l')
            kind = is_unsigned ? PP_TK_UInt128 : PP_TK_Int128;
        else
            kind = is_unsigned ? PP_TK_UInt32 : PP_TK_Int32;
    } else {
        char s = tolower((unsigned char)lexeme[len - 1]);
        if (s == 's')
            kind = is_unsigned ? PP_TK_UInt16 : PP_TK_Int16;
        else if (s == 'l')
            kind = is_unsigned ? PP_TK_UInt64 : PP_TK_Int64;
        else
            kind = is_unsigned ? PP_TK_UInt32 : PP_TK_Int32;
    }
    if (out_numeric) {
        size_t num_len = pos - (is_unsigned ? 1 : 0);
        if (num_len > len) num_len = 0;
        if (num_len > 0) {
            memcpy(out_numeric, lexeme, num_len);
            out_numeric[num_len] = '\0';
        } else {
            out_numeric[0] = '\0';
        }
    }
    return kind;
}
