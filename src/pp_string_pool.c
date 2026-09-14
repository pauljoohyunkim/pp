#include "pp_string_pool.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

static inline char* create_null_terminated_string(const char* text,
                                                  size_t length) {
    char* nt_str = NULL;
    if (text == NULL || length == 0) goto error_cleanup;

    nt_str = (char*)malloc(sizeof(char) * (length + 1));
    NULL_POINTER_ERROR_HANDLE(nt_str);

    memcpy(nt_str, text, length);
    nt_str[length] = '\0';

    return nt_str;

error_cleanup:
    return NULL;
}

PP_StringPool* pp_string_pool_create(void) {
    const char** strings = NULL;
    PP_StringPool* sp = NULL;

    sp = (PP_StringPool*)calloc(1, sizeof(PP_StringPool));
    NULL_POINTER_ERROR_HANDLE(sp);

    sp->capacity = 8;
    strings = (const char**)calloc(sp->capacity, sizeof(const char*));
    NULL_POINTER_ERROR_HANDLE(strings);

    sp->strings = strings;
    strings = NULL;

    return sp;
error_cleanup:
    free(strings);
    pp_string_pool_free(sp);
    return NULL;
}

void pp_string_pool_free(PP_StringPool* sp) {
    if (sp != NULL) {
        for (unsigned int i = 0; i < sp->count; i++) {
            free((void*)sp->strings[i]);
            sp->strings[i] = NULL;
        }
        free(sp->strings);
    }
    free(sp);
}

const char* pp_string_pool_push(PP_StringPool* sp, const char* text,
                                size_t length) {
    const char* nt_str = NULL;
    const char** new_strings = NULL;
    NULL_POINTER_ERROR_HANDLE(sp);
    NULL_POINTER_ERROR_HANDLE(text);
    if (length == 0) goto error_cleanup;

    // Search pool
    for (unsigned int i = 0; i < sp->count; i++) {
        if (length == strlen(sp->strings[i]) &&
            strncmp(sp->strings[i], text, length) == 0) {
            nt_str = sp->strings[i];
            break;
        }
    }

    if (nt_str == NULL) {
        // Create string first so we don't mutate the pool on allocation
        // failure.
        nt_str = create_null_terminated_string(text, length);
        NULL_POINTER_ERROR_HANDLE(nt_str);

        // Grow capacity only after we know the string is valid.
        if (sp->capacity == sp->count) {
            new_strings = (const char**)realloc(
                sp->strings, sizeof(const char*) * sp->capacity * 2);
            NULL_POINTER_ERROR_HANDLE(new_strings);

            sp->strings = new_strings;
            new_strings = NULL;
            sp->capacity *= 2;
        }

        // Transfer string into pool
        sp->strings[sp->count] = nt_str;
        sp->count++;
    }

    // Return the pointer to inside string pool
    return nt_str;
error_cleanup:
    free((void*)nt_str);
    free(new_strings);
    return NULL;
}
