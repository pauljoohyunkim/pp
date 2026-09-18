#ifndef PP_STRING_POOL_H
#define PP_STRING_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct {
    const char** strings;
    unsigned int count;
    unsigned int capacity;
} PP_StringPool;

/**
 * @brief Allocate a new string pool.
 *
 * Usage: Create once per lexer/parser instance and free when done.
 *
 * @return PP_StringPool* Pointer to initialized pool on success, NULL on
 * failure.
 */
PP_StringPool* pp_string_pool_create(void);

/**
 * @brief Free a string pool and all strings it owns.
 *
 * Usage: Call when the pool is no longer needed; frees internal array and each
 * duplicated string.
 *
 * @param sp Pointer to PP_StringPool. Safe to pass NULL.
 */
void pp_string_pool_free(PP_StringPool* sp);

/**
 * @brief Intern a string slice into the pool.
 *
 * Usage: Pass a pointer into source code and its length. The pool deduplicates
 * identical contents and returns a stable const char* that can be stored in
 * tokens. The returned pointer remains valid until pp_string_pool_free is
 * called.
 *
 * @param sp Pointer to PP_StringPool
 * @param text Pointer to the source bytes, not required to be null-terminated.
 * @param length Length of the slice in bytes.
 * @return const char* Pointer inside the pool, or NULL on error / zero length.
 *
 * @note The text is copied and null-terminated internally. Duplicate pushes
 * return the existing pointer without allocating.
 */
const char* pp_string_pool_push(PP_StringPool* sp, const char* text,
                                size_t length);

#ifdef __cplusplus
}
#endif

#endif /* PP_STRING_POOL_H */
