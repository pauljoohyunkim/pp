# General Coding Rules

## Error handling
- Use `NULL_POINTER_ERROR_HANDLE(ptr)` for nullity checks that should jump to an `error_cleanup` label.
- Prefer `NULL_POINTER_ERROR_HANDLE` for all non-nullable inputs/objects at function entry and after allocations, rather than manual `if (!ptr) goto error_cleanup;`.
- When an `error_cleanup` label exists, do not perform early returns for null pointers; let the macro jump to cleanup.
- If a function has no `error_cleanup`, do **not** use `NULL_POINTER_ERROR_HANDLE`; use explicit early return instead. The macro requires a cleanup target.
- Exception: when a specific diagnostic must be emitted for the failure, perform an explicit nullity check, push the error via `pp_error_list_push_error` (or equivalent), then `goto error_cleanup`. This is allowed for semantic/user-visible errors where the message needs context.
  Example:
  ```c
  tmp = pp_type_create(...);
  if (!tmp) {
      pp_error_list_push_error(sa->error_list, sa->filename, line, col, "Failed to allocate ...");
      goto error_cleanup;
  }
  ```
  In such cases do not combine with `NULL_POINTER_ERROR_HANDLE`; the explicit check is intentional.

## Memory / Free discipline
- Custom free functions safely handle NULL: `free(NULL)` is valid in C.
- Do NOT add explicit nullity checks before calling a custom free function.
  - Example: `pp_ast_root_free(ptr);` not `if (ptr) pp_ast_root_free(ptr);`
  - Example: `pp_ast_decoration_free(decor);` not guarded.
- Free functions may still need guards to avoid dereferencing null pointers before loops or member access; those guards are for safety, not for the free call itself.
- In `error_cleanup` blocks, call free functions unconditionally (they handle NULL). Only guard loops that would dereference NULL (e.g., `if (fields) { for (...) }`).

## Loops / increments
- Use postfix increment/decrement notation in `for` loops and elsewhere: `i++`, `i--`.
- Avoid prefix form `++i`, `--i` in this project.
- Example: `for (unsigned int i = 0; i < n; i++) { ... }`
- **Do not use inline post-increment for vector/counting assignments.** Separate the increment onto its own line to keep ownership and error-cleanup clear.
  - Preferred:
    ```c
    arr[count] = elem;
    count++;
    ```
  - Avoid:
    ```c
    arr[count++] = elem;
    ```
  This applies to `arg_count++`, `count++`, `decl_count++`, etc. when used for appending to dynamically grown arrays/vectors.

## Consistency with cz
- Follow cz project conventions for error handling, cleanup via `goto error_cleanup`, and memory discipline.
- Keep edits small, one concern at a time, commit after each logical change.

## Function structure conventions (CZ style)
- **Pointer declarations**: All pointers declared and initialized to `NULL` at the very top of the function scope, before any logic.
  - Heap pointers created inside loops must also be declared at top and reused per iteration; do not redeclare inside the loop body.
  - After moving a pointer into a parent struct/array, nullify the source pointer to avoid double-free on error paths.
- **Token consumption**: Wrap token consumptions that only assert type in a scoped block `{ ... }` with a local temporary variable, then apply `NULL_POINTER_ERROR_HANDLE` to the consumed token.
- **Line/column capture**: Capture `line`/`col` from the consumed token inside the scoped block, using outer-scope variables.
- **Allocations**: Every allocation (node creation, calloc, etc.) immediately followed by `NULL_POINTER_ERROR_HANDLE(ptr)`, unless a specific diagnostic message must be emitted – see Error handling exception above. In the latter case, check explicitly, push error, then goto cleanup.
- **Ownership transfer**: After assigning a pointer to a parent node, set the source pointer to `NULL` to avoid double-free in error paths.
- **Error cleanup**: Single `error_cleanup` label at end of function; free all owned resources unconditionally (free functions handle NULL). Only guard loops that would access array elements.
- **Early returns / parser guard**: Do NOT use plain `if (parser == NULL) return NULL;`. Use `NULL_POINTER_ERROR_HANDLE(parser);` at function entry so the nullity check is expressed via the macro, consistent with cz/src/cz_parser.c. The macro communicates error handling intent and routes to `error_cleanup` where cleanup exists. If a diagnostic must be emitted for a null parser, use the explicit check + push error + goto pattern described in Error handling.

## Token consumption pattern
```c
// Good: scoped block for token assertion
{
    PP_Token* tok = pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
    NULL_POINTER_ERROR_HANDLE(tok);
    line = tok->line;
    col = tok->column;
}

// Avoid: bare consumption without scoping or error handling
PP_Token* tok = pp_parser_consume_token(parser, PP_TT_LEFT_PARENTHESIS);
if (!tok) return NULL;
```

## Allocation pattern
```c
// Good: immediate error handling with macro when no diagnostic needed
PP_AST_Node* node = pp_ast_node_create(PP_AST_FooNode, line, col);
NULL_POINTER_ERROR_HANDLE(node);

// Good: explicit check with specific diagnostic when required
PPType* t = pp_type_create(PP_TK_Int32);
if (!t) {
    pp_error_list_push_error(sa->error_list, sa->filename, line, col, "Failed to allocate type");
    goto error_cleanup;
}

// Avoid: manual null check without diagnostic or macro
PP_AST_Node* node = pp_ast_node_create(PP_AST_FooNode, line, col);
if (!node) goto error_cleanup;
```

## Error cleanup pattern
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