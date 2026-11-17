# Callable Refactor Checklist

Goal: unify Function / Lambda / Operator AST representations under a single callable model with a `CallableKind` discriminator. Remove duplicated node types and field reuse.

## Phase 0 – Planning & Discovery
- [ ] Review all `node_type` switches for callable declarations
- [ ] Inventory uses of `function_declaration` / `extern_function_declaration` fields
- [ ] Document operator-specific constraints currently enforced in semantic analyzer
- [ ] Identify tests that rely on distinct AST node types

## Phase 1 – AST Definition
- [x] Define `enum PP_AST_CallableKind { PP_AST_CALLABLE_FUNCTION, PP_AST_CALLABLE_LAMBDA, PP_AST_CALLABLE_OPERATOR }`
- [x] Introduce `PP_AST_CallableDeclaration` struct with common fields: name, params, param_count/capacity, param_symbol_table, return_type, body, kind
- [x] Introduce `PP_AST_ExternCallableDeclaration` struct with name, params, param_count/capacity, return_type, kind
- [x] Replace `PP_AST_FunctionDeclarationNode`, `PP_AST_LambdaDeclarationNode`, `PP_AST_OperatorDeclarationNode` with `PP_AST_CallableDeclarationNode`
- [x] Replace `PP_AST_ExternFunctionDeclarationNode`, `PP_AST_ExternLambdaDeclarationNode`, `PP_AST_ExternOperatorDeclarationNode` with `PP_AST_ExternCallableDeclarationNode`
- [x] Update `PP_AST_NodeType` enum
- [x] Update union payload names from `function_declaration`/`extern_function_declaration` to `callable_declaration`/`extern_callable_declaration`

## Phase 2 – Parser
- [x] Update `pp_parser_create_function_declaration` → create `CallableDeclarationNode` with kind FUNCTION
- [x] Update `pp_parser_create_lambda_declaration` → kind LAMBDA
- [x] Update `pp_parser_create_operator_declaration` → kind OPERATOR
- [x] Update extern parsers to create `ExternCallableDeclarationNode` with appropriate kind
- [x] Adjust AST node creation calls and field assignments
- [ ] Update parser tests

## Phase 3 – AST Utilities
- [x] Update `pp_ast.c` free/print logic for new node types
- [ ] Ensure ownership rules stay consistent for params, return_type, body
- [ ] Update `pp_ast_get_decoration` if needed

## Phase 4 – Semantic Analyzer
- [x] Update Pass 1 dispatch to handle `CallableDeclarationNode` and branch on `kind`
- [x] Migrate `pp_semantic_analyzer_build_global_table_function_decl`, `lambda_decl`, `operator_decl` into unified builder with kind checks
- [x] Preserve operator-specific validation: const-ref params, non-ref return, builtin redefinition guard
- [x] Update extern handling similarly
- [x] Update later passes that assert node_type == OperatorDeclarationNode
- [x] Run semantic tests

## Phase 5 – Code Generator
- [x] Update top-level emit switch to handle `CallableDeclarationNode` and `ExternCallableDeclarationNode`
- [x] Ensure `pp_code_generator_emit_function` works for all kinds
- [ ] Preserve operator-specific codegen hooks if any
- [ ] Verify DWARF/Debug info generation for operators still works
- [ ] Run codegen tests

## Phase 6 – Symbol Table / Type System
- [ ] Verify symbol creation uses `kind` for name mangling / overload resolution
- [ ] Ensure `PPType` kind for operator remains correct

## Phase 7 – Docs & Notes
- [ ] Update `dev-docs/parser-ast-mapping.md`
- [ ] Update `notes/operator_design.md` if needed
- [ ] Update internal comments

## Phase 8 – Validation
- [ ] Full build succeeds
- [ ] Unit tests pass: `make unittest`
- [ ] Demo programs run: `demo/01_fibo`, `demo/03_eigenvector`, `demo/05_bf`
- [ ] Regression test for operator overload resolution and const-ref materialization
- [ ] Review diff size and ensure no lingering field reuse

## Phase 9 – Cleanup
- [ ] Remove deprecated node type constants
- [ ] Remove dead code paths
- [ ] Update checklist to completed

Notes:
- No code changes are made yet; this checklist is for tracking only.
- Consider incremental migration: first merge externs, then unify internals.
