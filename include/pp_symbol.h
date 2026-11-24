#ifndef PP_SYMBOL_H
#define PP_SYMBOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "pp_type.h"

/* Forward declaration: the constant initializer is a non-owning pointer into the program AST.
 * pp_ast.h includes the symbol table headers, so pp_ast.h cannot be included here. */
typedef struct PP_AST_Node PP_AST_Node;

typedef struct PPSymbol {
    const PPType* type;
    bool is_constexpr;
    bool is_escapable_ref;
    bool is_global;
    bool is_definition;
    /* True only for callable *declaration* symbols (function/lambda/operator
     * names). Function-typed variables, parameters and fields are false: they
     * are storage locations that happen to hold a callable value. */
    bool is_callable_decl;
    /* True only for symbols declared with `constant`. Such a symbol has no storage and is never
     * emitted as an object-file symbol; references are folded to its compile-time value. */
    bool is_constant_decl;
    /* Non-owning pointer to the constant's initializer expression in the program AST. Set in
     * semantic-analysis Pass 1 so array-size evaluation can fold it; do not free from here. */
    const PP_AST_Node* constant_initializer;
    /* Set in Pass 2 once the constant's value has been validated. Used to enforce that constant
     * initializers may only reference constants declared earlier (prevents cycles). */
    bool constant_value_available;
} PPSymbol;

/**
 * @brief Create a PPSymbol
 *
 * @param type Interned type pointer from global type table
 * @param is_constexpr Whether symbol is constexpr
 * @param is_escapable_ref Whether reference is escapable
 * @param is_global Whether symbol is declared in the global scope
 * @param is_definition Whether symbol is a definition as opposed to a forward declaration
 * @param is_callable_decl Whether symbol names a callable declaration (function/lambda/operator)
 * @return PPSymbol* Allocated symbol on success, NULL on failure
 */
PPSymbol* pp_symbol_create(const PPType* type, bool is_constexpr, bool is_escapable_ref, bool is_global,
                           bool is_definition, bool is_callable_decl);

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
