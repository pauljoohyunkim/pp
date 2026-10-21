#include "pp_symbol_table.h"

#include <stdlib.h>
#include <string.h>

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

static const char* pp_name_match(const char* a, const char* b) {
    if (a == b) return a;
    return strcmp(a, b) == 0 ? a : NULL;
}

PPSymbolTable* pp_symbol_table_create(unsigned int scope_level) {
    PPSymbolTable* st = NULL;
    const char** names = NULL;
    PPSymbol** symbols = NULL;

    st = (PPSymbolTable*)calloc(1, sizeof(PPSymbolTable));
    NULL_POINTER_ERROR_HANDLE(st);

    st->scope_level = scope_level;
    st->parent = NULL;
    st->capacity = 8;
    st->count = 0;

    names = (const char**)calloc(st->capacity, sizeof(const char*));
    NULL_POINTER_ERROR_HANDLE(names);

    symbols = (PPSymbol**)calloc(st->capacity, sizeof(PPSymbol*));
    NULL_POINTER_ERROR_HANDLE(symbols);

    st->names = names;
    names = NULL;
    st->symbols = symbols;
    symbols = NULL;

    return st;

error_cleanup:
    free(names);
    free(symbols);
    free(st);
    return NULL;
}

void pp_symbol_table_free(PPSymbolTable* st) {
    if (st != NULL) {
        if (st->symbols != NULL) {
            for (size_t i = 0; i < st->count; i++) {
                pp_symbol_free(st->symbols[i]);
                st->symbols[i] = NULL;
            }
        }
        free(st->names);
        free(st->symbols);
    }
    free(st);
}

int pp_symbol_table_push(PPSymbolTable* st, const char* name, PPSymbol* symbol) {
    const char** new_names = NULL;
    PPSymbol** new_symbols = NULL;

    NULL_POINTER_ERROR_HANDLE(st);
    NULL_POINTER_ERROR_HANDLE(name);
    NULL_POINTER_ERROR_HANDLE(symbol);

    if (st->count == st->capacity) {
        size_t new_capacity = st->capacity * 2;
        new_names = (const char**)realloc(st->names, new_capacity * sizeof(const char*));
        NULL_POINTER_ERROR_HANDLE(new_names);

        new_symbols = (PPSymbol**)realloc(st->symbols, new_capacity * sizeof(PPSymbol*));
        NULL_POINTER_ERROR_HANDLE(new_symbols);

        st->names = new_names;
        new_names = NULL;
        st->symbols = new_symbols;
        new_symbols = NULL;
        st->capacity = new_capacity;
    }

    st->names[st->count] = name;
    st->symbols[st->count] = symbol;
    st->count++;

    return 1;

error_cleanup:
    free(new_names);
    free(new_symbols);
    return 0;
}

PPSymbol* pp_symbol_table_lookup_mut(PPSymbolTable* st, const char* name, bool cascade) {
    if (st == NULL || name == NULL) return NULL;

    for (size_t i = 0; i < st->count; i++) {
        if (pp_name_match(st->names[i], name) != NULL) { return st->symbols[i]; }
    }

    if (cascade && st->parent != NULL) { return pp_symbol_table_lookup_mut(st->parent, name, true); }

    return NULL;
}

const PPSymbol* pp_symbol_table_lookup(const PPSymbolTable* st, const char* name, bool cascade) {
    return pp_symbol_table_lookup_mut((PPSymbolTable*)st, name, cascade);
}
