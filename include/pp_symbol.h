#ifndef PP_SYMBOL_H
#define PP_SYMBOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "pp_type.h"

typedef struct PPSymbol {
    const PPType* type;
    bool is_constexpr;
    bool is_escapable_ref;
    bool is_global;
} PPSymbol;

/**
 * @brief Create a PPSymbol
 *
 * @param type Interned type pointer from global type table
 * @param is_constexpr Whether symbol is constexpr
 * @param is_escapable_ref Whether reference is escapable
 * @param is_global Whether symbol is declared in the global scope
 * @return PPSymbol* Allocated symbol on success, NULL on failure
 */
PPSymbol* pp_symbol_create(const PPType* type, bool is_constexpr, bool is_escapable_ref, bool is_global);

/**
 * @brief Free a PPSymbol
 *
 * @param symbol Pointer to PPSymbol
 */
void pp_symbol_free(PPSymbol* symbol);

#ifdef __cplusplus
}
#endif

#endif /* PP_SYMBOL_H */
