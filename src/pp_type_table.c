#include "pp_type_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define NULL_POINTER_ERROR_HANDLE(ptr) do { if ((ptr)==NULL) goto error_cleanup; } while(0)
#define ENSURE_CAP(vec, cap, count, type) \
    do { \
        if ((count) >= (cap)) { \
            size_t new_cap = (cap) ? (cap)*2 : 8; \
            void *tmp = realloc((vec), new_cap * sizeof(type)); \
            if (!tmp) goto error_cleanup; \
            (vec) = tmp; \
            (cap) = new_cap; \
        } \
    } while(0)

static int ensure_type_capacity(PP_GlobalTypeTable *gtt) {
    if (gtt->type_count >= gtt->type_capacity) {
        size_t new_cap = gtt->type_capacity ? gtt->type_capacity*2 : 8;
        void *tmp = realloc(gtt->types, new_cap * sizeof(PPType*));
        if (!tmp) return 0;
        gtt->types = tmp;
        gtt->type_capacity = new_cap;
    }
    return 1;
}
static int ensure_named_capacity(PP_GlobalTypeTable *gtt) {
    if (gtt->named_count >= gtt->named_capacity) {
        size_t new_cap = gtt->named_capacity ? gtt->named_capacity*2 : 8;
        void *tmp1 = realloc(gtt->named_names, new_cap * sizeof(const char*));
        void *tmp2 = realloc(gtt->named_types, new_cap * sizeof(PPType*));
        if (!tmp1 || !tmp2) { free(tmp1); free(tmp2); return 0; }
        gtt->named_names = tmp1;
        gtt->named_types = tmp2;
        gtt->named_capacity = new_cap;
    }
    return 1;
}
static int ensure_alias_capacity(PP_GlobalTypeTable *gtt) {
    if (gtt->alias_count >= gtt->alias_capacity) {
        size_t new_cap = gtt->alias_capacity ? gtt->alias_capacity*2 : 8;
        void *tmp1 = realloc(gtt->alias_names, new_cap * sizeof(const char*));
        void *tmp2 = realloc(gtt->alias_types, new_cap * sizeof(PPType*));
        if (!tmp1 || !tmp2) { free(tmp1); free(tmp2); return 0; }
        gtt->alias_names = tmp1;
        gtt->alias_types = tmp2;
        gtt->alias_capacity = new_cap;
    }
    return 1;
}

/* Minimal structural equality – pointer shortcut then kind match. Full recursive compare TODO */
bool pp_type_equal(const PPType *a, const PPType *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case PP_TK_Void:
        case PP_TK_Int32:
        case PP_TK_UInt32:
        case PP_TK_Float:
        case PP_TK_Bool:
            return true;

        case PP_TK_Struct:
            /* nominal equality – compare name,
               optionally layout -> Not applied */
            return (a->type.pp_type_struct.name == b->type.pp_type_struct.name) && (strcmp(a->type.pp_type_struct.name, b->type.pp_type_struct.name) == 0);

        case PP_TK_Array:
            /* compare size and recursively equal element_type */
            return (a->type.pp_type_array.size == b->type.pp_type_array.size) && pp_type_equal(a->type.pp_type_array.element_type, b->type.pp_type_array.element_type);

        case PP_TK_List:
            /* recursively equal element_type */
            return pp_type_equal(a->type.pp_type_list.element_type, b->type.pp_type_list.element_type);

        case PP_TK_Const:
            /* recursively equal const_of */
            return pp_type_equal(a->type.pp_type_const.const_of, b->type.pp_type_const.const_of);

        case PP_TK_Reference:
            /* recursively equal reference_to */
            return pp_type_equal(a->type.pp_type_reference.reference_to, b->type.pp_type_reference.reference_to);

        case PP_TK_Newtype:
            /* nominal equality – compare name
               optionally underlying -> For now, not applied. */
            return (a->type.pp_type_newtype.name == b->type.pp_type_newtype.name) && (strcmp(a->type.pp_type_newtype.name, b->type.pp_type_newtype.name) == 0);

        case PP_TK_Function:
            /* compare param_count, recursively equal return_type and each param_type */
            if (a->type.pp_type_function.param_count != b->type.pp_type_function.param_count) return false;
            if (!pp_type_equal(a->type.pp_type_function.return_type, b->type.pp_type_function.return_type)) return false;
            for (size_t i = 0; i < a->type.pp_type_function.param_count; i++) {
                if (!pp_type_equal(a->type.pp_type_function.param_types[i], b->type.pp_type_function.param_types[i])) return false;
            }
            return true;

        case PP_TK_Lambda:
            if (a->type.pp_type_lambda.param_count != b->type.pp_type_lambda.param_count) return false;
            if (!pp_type_equal(a->type.pp_type_lambda.return_type, b->type.pp_type_lambda.return_type)) return false;
            for (size_t i = 0; i < a->type.pp_type_lambda.param_count; i++) {
                if (!pp_type_equal(a->type.pp_type_lambda.param_types[i], b->type.pp_type_lambda.param_types[i])) return false;
            }
            return true;

        default:
            assert(0 && "PP Type Equal: Unreachable case.");
            abort();
    }
    return false;
}

PP_GlobalTypeTable* pp_global_type_table_create(void) {
    PP_GlobalTypeTable *gtt = calloc(1, sizeof(PP_GlobalTypeTable));
    NULL_POINTER_ERROR_HANDLE(gtt);
    return gtt;
error_cleanup:
    return NULL;
}

void pp_global_type_table_free(PP_GlobalTypeTable *gtt) {
    if (!gtt) return;
    for (size_t i = 0; i < gtt->type_count; i++) {
        pp_type_free(gtt->types[i]);
    }
    free(gtt->types);
    free(gtt->named_names);
    free(gtt->named_types);
    free(gtt->alias_names);
    free(gtt->alias_types);
    free(gtt);
}

int pp_global_type_table_push_internal(PP_GlobalTypeTable *gtt, PPType *type) {
    NULL_POINTER_ERROR_HANDLE(gtt);
    NULL_POINTER_ERROR_HANDLE(type);
    if (!ensure_type_capacity(gtt)) goto error_cleanup;
    gtt->types[gtt->type_count] = type;
    gtt->type_count++;
    return 1;
error_cleanup:
    return 0;
}

const PPType* pp_global_type_table_intern(PP_GlobalTypeTable *gtt, PPType *candidate) {
    if (!gtt || !candidate) return NULL;
    /* Search for existing equivalent */
    for (size_t i = 0; i < gtt->type_count; i++) {
        if (pp_type_equal(gtt->types[i], candidate)) {
            if (candidate != gtt->types[i]) {
                pp_type_free(candidate);
            }
            return gtt->types[i];
        }
    }
    /* Not found – take ownership */
    if (!pp_global_type_table_push_internal(gtt, candidate)) {
        return NULL;
    }
    return candidate;
}

int pp_global_type_table_register_named(PP_GlobalTypeTable *gtt, const char *name, const PPType *type) {
    if (!gtt || !name || !type) return 0;
    if (!ensure_named_capacity(gtt)) return 0;
    gtt->named_names[gtt->named_count] = name;
    gtt->named_types[gtt->named_count] = type;
    gtt->named_count++;
    return 1;
}

int pp_global_type_table_register_alias(PP_GlobalTypeTable *gtt, const char *alias_name, const PPType *type) {
    if (!gtt || !alias_name || !type) return 0;
    if (!ensure_alias_capacity(gtt)) return 0;
    gtt->alias_names[gtt->alias_count] = alias_name;
    gtt->alias_types[gtt->alias_count] = type;
    gtt->alias_count++;
    return 1;
}

const PPType* pp_global_type_table_find_by_name(PP_GlobalTypeTable *gtt, const char *name) {
    return (const PPType*) pp_global_type_table_find_by_name_mut(gtt, name);
}

PPType* pp_global_type_table_find_by_name_mut(PP_GlobalTypeTable *gtt, const char *name) {
    if (!gtt || !name) return NULL;
    for (size_t i = 0; i < gtt->named_count; i++) {
        if (gtt->named_names[i] == name || strcmp(gtt->named_names[i], name) == 0) {
            return (PPType*)gtt->named_types[i];
        }
    }
    return NULL;
}

const PPType* pp_global_type_table_find_alias(PP_GlobalTypeTable *gtt, const char *name) {
    if (!gtt || !name) return NULL;
    for (size_t i = 0; i < gtt->alias_count; i++) {
        if (gtt->alias_names[i] == name || strcmp(gtt->alias_names[i], name) == 0) {
            return gtt->alias_types[i];
        }
    }
    return NULL;
}

const PPType* pp_global_type_table_find_type(PP_GlobalTypeTable *gtt, const PPType *query) {
    if (!gtt || !query) return NULL;
    for (size_t i = 0; i < gtt->type_count; i++) {
        if (pp_type_equal(gtt->types[i], query)) {
            return gtt->types[i];
        }
    }
    return NULL;
}

const PPType* pp_type_decay(PP_GlobalTypeTable* gtt, const PPType* type) {
    PPType* tmp_type = NULL;
    if (!gtt || !type) goto error_cleanup;

    switch (type->kind) {
        case PP_TK_Reference:
            return pp_type_decay(gtt, type->type.pp_type_reference.reference_to);
        case PP_TK_Array:
            {
                const PPType* decayed_internal_type = pp_type_decay(gtt, type->type.pp_type_array.element_type);
                NULL_POINTER_ERROR_HANDLE(decayed_internal_type);

                // Re-create array wrapper.
                tmp_type = pp_type_create(PP_TK_Array);
                NULL_POINTER_ERROR_HANDLE(tmp_type);

                tmp_type->type.pp_type_array.size = type->type.pp_type_array.size;
                tmp_type->type.pp_type_array.element_type = decayed_internal_type;

                const PPType* decayed_array_type = pp_global_type_table_intern(gtt, tmp_type);
                tmp_type = NULL;
                NULL_POINTER_ERROR_HANDLE(decayed_array_type);

                return decayed_array_type;
            }
        case PP_TK_List:
            {
                const PPType* decayed_internal_type = pp_type_decay(gtt, type->type.pp_type_list.element_type);
                NULL_POINTER_ERROR_HANDLE(decayed_internal_type);

                // Re-create array wrapper.
                tmp_type = pp_type_create(PP_TK_List);
                NULL_POINTER_ERROR_HANDLE(tmp_type);

                tmp_type->type.pp_type_list.element_type = decayed_internal_type;

                const PPType* decayed_list_type = pp_global_type_table_intern(gtt, tmp_type);
                tmp_type = NULL;
                NULL_POINTER_ERROR_HANDLE(decayed_list_type);

                return decayed_list_type;
            }
        case PP_TK_Const:
            // Strip away constness
            return pp_type_decay(gtt, type->type.pp_type_const.const_of);
        case PP_TK_Bool:
        case PP_TK_Float:
        case PP_TK_Int32:
        case PP_TK_UInt32:
        case PP_TK_Void:
        case PP_TK_Struct:
        case PP_TK_Newtype:
            return type;
        default:
            assert(0 && "Not available (yet) for Function, Lambda");
            break;
    }

error_cleanup:
    return NULL;
}

/**
 * @brief Print the global type table.
 * @param gtt Pointer to the global type table.
 */
void pp_global_type_table_print(const PP_GlobalTypeTable *gtt) {
    if (gtt == NULL) {
        printf("NULL global type table\n");
        return;
    }

    printf("Global Type Table:\n");
    printf("  Named types (struct/newtype) (%zu/%zu):\n", gtt->named_count, gtt->named_capacity);
    for (size_t i = 0; i < gtt->named_count; i++) {
        printf("    [%zu] Name: %s, Type: %p\n", i, gtt->named_names[i], (void*)gtt->named_types[i]);
    }
    printf("  Alias types (typedef) (%zu/%zu):\n", gtt->alias_count, gtt->alias_capacity);
    for (size_t i = 0; i < gtt->alias_count; i++) {
        printf("    [%zu] Alias: %s, Type: %p\n", i, gtt->alias_names[i], (void*)gtt->alias_types[i]);
    }
    printf("  Interned types (%zu/%zu):\n", gtt->type_count, gtt->type_capacity);
    for (size_t i = 0; i < gtt->type_count; i++) {
        printf("    [%zu] Type: %p\n", i, (void*)gtt->types[i]);
    }
}
