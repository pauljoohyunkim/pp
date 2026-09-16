# General Coding Rules

## Error handling
- Use `NULL_POINTER_ERROR_HANDLE(ptr)` for nullity checks that should jump to an `error_cleanup` label.
- Prefer `NULL_POINTER_ERROR_HANDLE` for all non-nullable inputs/objects at function entry and after allocations, rather than manual `if (!ptr) goto error_cleanup;`.
- When an `error_cleanup` label exists, do not perform early returns for null pointers; let the macro jump to cleanup.

## Memory / Free discipline
- Custom free functions safely handle NULL: `free(NULL)` is valid in C.
- Do NOT add explicit nullity checks before calling a custom free function.
  - Example: `pp_ast_root_free(ptr);` not `if (ptr) pp_ast_root_free(ptr);`
  - Example: `pp_ast_decoration_free(decor);` not guarded.
- Free functions may still need guards to avoid dereferencing null pointers before loops or member access; those guards are for safety, not for the free call itself.

## Loops / increments
- Use postfix increment/decrement notation in `for` loops and elsewhere: `i++`, `i--`.
- Avoid prefix form `++i`, `--i` in this project.
- Example: `for (unsigned int i = 0; i < n; i++) { ... }`

## Consistency with cz
- Follow cz project conventions for error handling, cleanup via `goto error_cleanup`, and memory discipline.
- Keep edits small, one concern at a time, commit after each logical change.
