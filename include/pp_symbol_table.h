#ifndef PP_SYMBOL_TABLE_H
#define PP_SYMBOL_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "pp_symbol.h"

typedef struct PPSymbolTable {
    struct PPSymbolTable* parent;
    const char** names;
    PPSymbol** symbols;
    size_t count;
    size_t capacity;
    unsigned int scope_level;
} PPSymbolTable;

/**
 * @brief Create a new symbol table
 *
 * @param scope_level Scope level for this table
 * @return PPSymbolTable* New table on success, NULL on failure
 */
PPSymbolTable* pp_symbol_table_create(unsigned int scope_level);

/**
 * @brief Free a symbol table and its symbols
 *
 * @param st Pointer to table
 */
void pp_symbol_table_free(PPSymbolTable* st);

/**
 * @brief Push a symbol into the table
 *
 * Ownership of symbol is transferred to the table.
 *
 * @param st Symbol table
 * @param name Interned name from string pool
 * @param symbol Symbol to insert
 * @return int 1 on success, 0 on failure
 */
int pp_symbol_table_push(PPSymbolTable* st, const char* name, PPSymbol* symbol);

/**
 * @brief Lookup a symbol by name
 *
 * @param st Symbol table
 * @param name Name to look up
 * @param cascade If true, search parent chain
 * @return const PPSymbol* Found symbol or NULL
 */
const PPSymbol* pp_symbol_table_lookup(const PPSymbolTable* st, const char* name, bool cascade);

#ifdef __cplusplus
}
#endif

#endif /* PP_SYMBOL_TABLE_H */
