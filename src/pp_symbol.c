#include "pp_symbol.h"

#include <stdlib.h>

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

/**
 * @brief Create a PPSymbol
 */
PPSymbol* pp_symbol_create(const PPType* type, bool is_constexpr, bool is_escapable_ref) {
    PPSymbol* symbol = NULL;

    symbol = (PPSymbol*)calloc(1, sizeof(PPSymbol));
    NULL_POINTER_ERROR_HANDLE(symbol);

    symbol->type = type;
    symbol->is_constexpr = is_constexpr;
    symbol->is_escapable_ref = is_escapable_ref;

    return symbol;

error_cleanup:
    free(symbol);
    return NULL;
}

/**
 * @brief Free a PPSymbol
 */
void pp_symbol_free(PPSymbol* symbol) {
    if (symbol != NULL) {
        free(symbol);
    }
}
