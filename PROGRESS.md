# PP Parser Refactoring Progress

## Overview
This document tracks the progress of refactoring `src/pp_parser.c` to match cz-style conventions as part of the PP compiler project.

**Current Branch:** `port-parser-2`  
**Working Directory:** `/home/pbjk/Documents/Lab/pp`  
**Last Updated:** 2026-09-16

## Task Description
Refactor `src/pp_parser.c` to match cz-style conventions:
1. Remove unnecessary `extern` declarations inside translation unit
2. Convert `pp_parser_create_type` from chained if-else to switch statement
3. Apply CZ-style conventions to all parser functions:
   - All pointers declared and initialized to `NULL` at top of function scope
   - Token consumptions wrapped in small scoped blocks with `NULL_POINTER_ERROR_HANDLE`
   - All allocation checks use `NULL_POINTER_ERROR_HANDLE(ptr)` instead of `if (!ptr) return NULL;`
   - Early returns only for `parser == NULL` guard (no cleanup needed)
   - `error_cleanup` label used consistently with `goto` for error paths
   - `error_cleanup` frees resources without null checks (free functions handle NULL)
   - Scoped blocks for token consumptions that only assert token type
   - Ownership transfer: after assigning to parent node, set source pointer to `NULL`

## Completed Refactoring (19 functions)

| Function | Status | Key Changes |
|----------|--------|-------------|
| `pp_parser_create_type` | ✅ | Switch statement for token types |
| `pp_parser_create_if_statement` | ✅ | Scoped token blocks, NULL_POINTER_ERROR_HANDLE |
| `pp_parser_create_while_statement` | ✅ | Same pattern |
| `pp_parser_create_for_statement` | ✅ | Same pattern |
| `pp_parser_create_return_statement` | ✅ | Fixed `.col` → `.column` bug |
| `pp_parser_create_block` | ✅ | |
| `pp_parser_create_expression_ext` | ✅ | Complex recursive function |
| `pp_parser_create_unary` | ✅ | |
| `pp_parser_create_literal` | ✅ | |
| `pp_parser_create_identifier` | ✅ | |
| `pp_parser_create_struct_declaration` | ✅ | |
| `pp_parser_create_typedef_declaration` | ✅ | |
| `pp_parser_create_newtype_declaration` | ✅ | |
| `pp_parser_create_struct_initializer` | ✅ | |
| `pp_parser_create_collection_initializer` | ✅ | |
| `pp_parser_create_param_decl` | ✅ | |
| `pp_parser_parse_param_list` | ✅ | Pointer declarations at top, scoped token consumption, ownership transfer |
| `pp_parser_create_variable_declaration` | ✅ | CZ-style refactoring with scoped blocks and error_cleanup |
| `pp_parser_create_function_declaration` | ✅ | Scoped token blocks, ownership transfer, NULL_POINTER_ERROR_HANDLE |
| `pp_parser_create_lambda_declaration` | ✅ | Scoped token blocks, ownership transfer, NULL_POINTER_ERROR_HANDLE |

### Additional Changes
- Removed 2 unnecessary `extern` declarations
- Fixed `return_tok->col` → `return_tok->column` bug in `pp_parser_create_return_statement`

## Remaining Functions (0)

All parser functions have been refactored to CZ-style conventions. Build succeeds with no new warnings.

## Key Patterns Established

### Function Structure
```c
static PP_AST_Node* pp_parser_create_foo(PP_Parser* parser) {
    PP_AST_Node* node = NULL;
    PP_AST_Node* child = NULL;
    PP_Token* tok = NULL;
    unsigned int line = 0, col = 0;
    
    if (parser == NULL) return NULL;
    
    // Token consumption in scoped block
    {
        tok = pp_parser_consume_token(parser, PP_TT_SOMETHING);
        NULL_POINTER_ERROR_HANDLE(tok);
        line = tok->line;
        col = tok->column;
    }
    
    // Allocations with error handling
    child = pp_parser_create_child(parser);
    NULL_POINTER_ERROR_HANDLE(child);
    
    node = pp_ast_node_create(PP_AST_FooNode, line, col);
    NULL_POINTER_ERROR_HANDLE(node);
    
    // Transfer ownership, nullify source pointers
    node->foo.child = child;
    child = NULL;
    
    return node;
    
error_cleanup:
    pp_ast_root_free(node);
    pp_ast_root_free(child);
    return NULL;
}
```

### Error Cleanup Pattern
```c
error_cleanup:
    // Free functions handle NULL, no checks needed
    pp_ast_root_free(node);
    pp_ast_root_free(child);
    
    // Arrays: guard loop, free array itself
    if (fields) {
        for (unsigned int i = 0; i < count; i++) {
            pp_ast_root_free(fields[i].field);
            pp_ast_root_free(fields[i].initializer);
        }
    }
    free(fields);
    return NULL;
```

## Build Status
- Compiles successfully with only pre-existing warnings
- No new warnings introduced by refactoring
- Run `make obj/pp_parser.o` to verify

## Files Modified
- `src/pp_parser.c` - Main refactoring target
- `notes/general_coding_rule.md` - Updated with CZ-style conventions
- `PROGRESS.md` - This file

## Next Session Instructions

### To Resume Work:
1. Checkout branch: `git checkout port-parser-2`
2. Navigate to project: `cd /home/pbjk/Documents/Lab/pp`
3. Verify build: `make obj/pp_parser.o`
4. Start with `pp_parser_parse_param_list` (line ~1466)

### Refactoring Order (Dependency Order):
1. `pp_parser_parse_param_list` - Base function used by others
2. `pp_parser_create_variable_declaration` - Calls `pp_parser_create_param_decl`
3. `pp_parser_create_function_declaration` - Calls `pp_parser_parse_param_list`
4. `pp_parser_create_lambda_declaration` - Calls `pp_parser_parse_param_list`

### After Each Function:
- Run `make obj/pp_parser.o` to verify no new warnings
- Commit with descriptive message

### When All Done:
- Run full test suite if available
- Final code review
- Merge to main branch

## Reference Files
- `notes/general_coding_rule.md` - Detailed coding conventions
- `src/pp_parser.c` - Main source file
- `include/pp_*.h` - Header files for AST, tokens, errors

## Git History (port-parser-2 branch)
- `446aee9` - Initial refactor: externs, type switch, if/while/for/return/block
- `fca9538` - struct_declaration and typedef_declaration refactoring
- `226d81e` - error_cleanup null check fixes for struct/typedef
- `b51e45e` - Add PROGRESS.md and update general_coding_rule.md
- `b416cd0` - collection_initializer and param_decl refactoring

---
*Last updated: 2026-09-16*
*All parser functions refactored to CZ-style conventions. Build verified.*