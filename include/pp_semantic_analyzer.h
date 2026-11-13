#ifndef PP_SEMANTIC_ANALYZER_H
#define PP_SEMANTIC_ANALYZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pp_ast.h"
#include "pp_error.h"
#include "pp_parser.h"
#include "pp_symbol_table.h"
#include "pp_type.h"
#include "pp_type_table.h"

typedef struct PP_OperatorOverloadEntry {
    const char* spelling;
    size_t arity;
    const PPType* lhs_type;
    const PPType* rhs_type;
    const PPSymbol* callable;
} PP_OperatorOverloadEntry;

typedef struct PP_OperatorOverloadRegistry {
    PP_OperatorOverloadEntry* entries;
    size_t count;
    size_t capacity;
} PP_OperatorOverloadRegistry;

typedef struct {
    const char* code;
    size_t code_length;
    PP_Token* tokens;
    size_t n_tokens;
    PP_AST_Node* program;
    PP_StringPool* sp;

    PPSymbolTable* gst;
    PP_GlobalTypeTable* gtt;

    /* Overloaded operator registry (minimal). */
    PP_OperatorOverloadRegistry* operator_overloads;

    const PPType* current_function_type;
    PP_ErrorList* error_list;
} PP_SemanticAnalyzer;

/**
 * @brief Creates semantic analyzer from parser
 *
 * @param parser Pointer to PP_Parser struct.
 * @return PP_SemanticAnalyzer* Pointer to allocated semantic analyzer, or NULL if failure.
 *
 * Note that dynamically allocated members of the parser are transferred to semantic analyzer,
 * so parser can safely be freed with its designated free function.
 */
PP_SemanticAnalyzer* pp_semantic_analyzer_create(PP_Parser* parser);

/**
 * @brief Frees semantic analyzer and its components
 *
 * @param sa Pointer to PP_SemanticAnalyzer
 */
void pp_semantic_analyzer_free(PP_SemanticAnalyzer* sa);

/**
 * @brief Invoke semantic analyzer to analyze the AST.
 *
 * @param sa Pointer to PP_SemanticAnalyzer
 * @return int 1 on success, 0 on failure.
 *
 * This decorates the AST and populates symbol table and global type table.
 * Note that just because 1 is returned does not mean the code is error free.
 * Always check the error list afterwards.
 */
int pp_semantic_analyzer_analyze(PP_SemanticAnalyzer* sa);

const PPSymbol* pp_semantic_analyzer_operator_lookup(PP_SemanticAnalyzer* sa, const char* spelling,
                                                     const PPType* lhs_type, const PPType* rhs_type);
const PPSymbol* pp_semantic_analyzer_unary_operator_lookup(PP_SemanticAnalyzer* sa, const char* spelling,
                                                           const PPType* operand_type);

#ifdef __cplusplus
}
#endif

#endif /* PP_SEMANTIC_ANALYZER_H */
