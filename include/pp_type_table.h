#ifndef PP_TYPE_TABLE_H
#define PP_TYPE_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "pp_type.h"

typedef struct {
    PPType** types;
    size_t type_count;
    size_t type_capacity;

    const char** named_names;
    const PPType** named_types;
    size_t named_count;
    size_t named_capacity;

    const char** alias_names;
    const PPType** alias_types;
    size_t alias_count;
    size_t alias_capacity;
} PP_GlobalTypeTable;

/**
 * @brief Create a new global type table.
 */
PP_GlobalTypeTable* pp_global_type_table_create(void);

/**
 * @brief Free a global type table and all owned types.
 */
void pp_global_type_table_free(PP_GlobalTypeTable* gtt);

/**
 * @brief Intern a type. If an equivalent type already exists, return it and free the candidate.
 * Otherwise transfer ownership of candidate into the table and return it.
 * Returns canonical pointer on success, NULL on failure.
 */
const PPType* pp_global_type_table_intern(PP_GlobalTypeTable* gtt, PPType* candidate);

/**
 * @brief Internal push without interning check. Ownership of type is transferred.
 * Not for public use.
 */
int pp_global_type_table_push_internal(PP_GlobalTypeTable* gtt, PPType* type);

/**
 * @brief Register a named type. Named types are distinct types with an associated name
 * (e.g., struct, newtype). Ownership of type is transferred to the table.
 */
int pp_global_type_table_register_named(PP_GlobalTypeTable* gtt, const char* name, const PPType* type);

/**
 * @brief Register an alias name → existing type. Alias does not own the type.
 */
int pp_global_type_table_register_alias(PP_GlobalTypeTable* gtt, const char* alias_name, const PPType* type);

/**
 * @brief Find a named type by name. Returns NULL if not found.
 */
const PPType* pp_global_type_table_find_by_name(PP_GlobalTypeTable* gtt, const char* name);

/**
 * @brief Find a named type by name, mutable pointer. Returns NULL if not found.
 */
PPType* pp_global_type_table_find_by_name_mut(PP_GlobalTypeTable* gtt, const char* name);

/**
 * @brief Find an alias type by name. Returns NULL if not found.
 */
const PPType* pp_global_type_table_find_alias(PP_GlobalTypeTable* gtt, const char* name);

/**
 * @brief Find a type by structural equality. Linear search with pointer shortcut.
 * For now returns first matching kind; full structural compare is TODO.
 */
const PPType* pp_global_type_table_find_type(PP_GlobalTypeTable* gtt, const PPType* query);

/**
 * @brief Check structural equality of two types.
 */
bool pp_type_equal(const PPType* a, const PPType* b);

/**
 * @brief Strip away const and references from a type, interning if necessary.
 */
const PPType* pp_type_decay(PP_GlobalTypeTable* gtt, const PPType* type);

/**
 * @brief Resolve the underlying machine type for a possibly chained newtype.
 * Walks through PP_TK_Newtype nodes until a non-newtype type is reached.
 */
const PPType* pp_type_resolve_machine_type(const PPType* t);

/**
 * @brief Print the global type table.
 * @param gtt Pointer to the global type table.
 */
void pp_global_type_table_print(const PP_GlobalTypeTable* gtt);

#ifdef __cplusplus
}
#endif

#endif /* PP_TYPE_TABLE_H */
