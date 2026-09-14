# PARSER IMPLEMENTATION ANALYSIS

## Context
Port AST + parsing from `../cz` to current `pp` project, using logic from `../pp-rust` but structuring the project like `cz`. Error handling for memory allocation must follow `cz`.

## 1. Source Repos Structure

### cz - C implementation
- `src/cz_parser.c`, `include/cz_parser.h`
- `include/cz_ast.h` – unified union AST node with enum `CZ_AST_NodeType`
- Memory management: manual calloc/realloc, NULL checks via `NULL_POINTER_ERROR_HANDLE(ptr)` macro + `goto error_cleanup`.
- Errors collected in `CZ_ErrorList` via `cz_error_list_push_error`.
- Pratt/precedence climbing parser:
  - `CZ_Precedence` enum: NONE=0 ... PRIMARY=11
  - `cz_parser_create_expression(parser, min_binding_power)`
  - Loop while next op precedence > min, then recurse with right-binding power.
  - Special cases for `AS` cast, `.` member access, `(` call, `[` index handled inline in the loop.
- Top-level program parsing in `cz_parser_create_program_node`: walks tokens, dispatches to function/struct/var/typedef/newtype, synchronizes on errors via `cz_parser_sync`.
- List building helpers push with realloc and capacity doubling for globals, incremental realloc for params/statements/members.

### pp-rust - Rust implementation
- `src/pp_parser.rs`, `src/pp_ast.rs`
- AST as Rust enum `PPASTNodeKind` with owned `Vec`, `Box`, `Rc`.
- Pratt parser:
  - `OperatorPrecedence` enum values 0,10,20...110
  - `infix_binding_power(token_type) -> Option<(u8,u8)>`
  - `create_expression_ext(min_binding_power)` recursive.
  - Call/Access handling inside same loop as binary ops, using left/right binding powers.
- Error handling via `Result<PPASTNode, PPError>` and propagation with `?`.
- Synchronization `synchronize()` skips to next sync token: If, Func, Lambda, Return, For, While, Typedef, Newtype, `;`, `}`.
- Statements parsed explicitly with `create_statement`, expression/assignment disambiguation via backtracking in `create_expression_or_assignment`.

## 2. Key Differences Relevant to Port

### Expression parsing
- **Both use precedence climbing**. cz uses enum values increasing with tightness; pp-rust uses spaced values 10 steps apart.
- **pp-rust** cleanly separates binary vs postfix operators (`LeftParenthesis`, `LeftSquareBracket`, `Period`) in the same loop with left-binding = CallAccess, right-binding = 0.
- **cz** mixes them in if/else branches inside the precedence loop and uses a single `CZ_Precedence` value.

### Assignment
- cz: assignment is a statement node, parsed in `create_statement` after parsing LHS expression then checking for assignment token.
- pp-rust: `create_expression_or_assignment` parses LHS as expression, then if next token is assignment op it builds `Assignment` AST node.

### Statements / Blocks
- cz: block is `CZ_AST_BlockStatementNodeType`; statements pushed via `cz_parser_push_statement_to_block`.
- pp-rust: `StatementList { stmts: Vec<PPASTNode> }`.

### Types
- Both support const, reference, array/list specifier.
- cz uses union inside `type_expression` with flags `is_const`, `is_reference`, `is_array`, `is_list`.
- pp-rust uses `TypeExpression { is_const, is_reference, array_list_specifier: Option<ASTNodeArraySpecifier>, kind }`.

### Error handling for memory allocation
- **cz model**: every allocation → `NULL_POINTER_ERROR_HANDLE` macro → jump to `error_cleanup` which frees partially built nodes. Errors pushed to error list but parsing continues via sync.
- **pp-rust model**: no explicit OOM handling; `Result` propagates parse errors only.

**Requirement:** keep cz-style allocation checks. Adopt pp-rust parser logic but wrap each allocation/creation in NULL check + goto cleanup pattern, and push allocation failures as fatal errors similar to cz.

## 3. Recommended Port Design

### Project structure like cz
```
include/
  pp_ast.h
  pp_parser.h
  pp_tokens.h
src/
  pp_parser.c
  pp_ast.c
...
```

Keep one file per module as in cz, header declares opaque structs and enums.

### Parser logic source: pp-rust
- Use Pratt with binding powers **exactly** as in pp-rust. Do not reuse cz enum values.
- Implement `pp_parser_create_expression_ext(min_bp)` mirroring Rust implementation.
- Preserve operator precedence / binding strengths from pp-rust:
  Equality=10, Relational=20, BitwiseOr=30, BitwiseXor=40, BitwiseAnd=50,
  Additive=60, Multiplicative=70, Cast=80, Unary=90, CallAccess=100, Primary=110
  Infix binding power is `(left_bp, right_bp)` where right_bp = left_bp+1 for left-associative ops, and right_bp = 0 for postfix CallAccess.
- Handle postfix operators in same loop: `(` call, `[` index, `.` member with left-binding 100, right-binding 0 as in pp-rust.

### AST mapping
Map pp-rust node kinds to cz-style union:
- Program → global_declaration_list
- FunctionDeclaration / LambdaDeclaration → FunctionDeclaration node
- ParamDeclaration → VariableDeclaration node reuse
- StatementList → BlockStatementNode
- Assignment → AssignmentStatementNode using binary_expression union with op flag
- Return, If, While, For as in cz
- TypeExpression with flags `is_const`, `is_reference`, array/list as in cz

### Error handling like cz
- All allocations via calloc/realloc checked immediately.
- Macro `NULL_POINTER_ERROR_HANDLE(ptr)` and `goto error_cleanup` in every creator function.
- On parse error, call `pp_parser_sync` to next sync token: `; } if func return for while typedef newtype`.
- Allocation failure → push fatal error via error list and return NULL, cleanup frees existing sub-nodes.

### Synchronization
Reuse cz sync token set extended with `lambda`, `typedef`, `newtype` as in pp-rust.

## 4. Concrete Steps
1. Define AST enums/types mirroring cz_ast.h but incorporate pp-rust node variants for struct init, collection init.
2. Implement Pratt expression parser from pp-rust, translate Result<> to NULL checks + goto cleanup.
3. Implement statement parsing with backtracking assignment detection as in pp-rust `create_expression_or_assignment`.
4. Keep memory allocation error handling identical to cz: check each calloc/realloc, free on error, push error to list.
5. Keep project file layout like cz: headers in include/, sources in src/.

This preserves modern parsing logic from pp-rust while retaining cz project structure and C error-handling discipline for allocations.

## 5. AST Design Decisions – confirmed 2026-03-19
1. Decoration: follow Rust logic – decoration only on expression-like nodes that need it: BinaryExpression, UnaryExpression, CastExpression, Literal, Identifier, FunctionCall, MemberAccess, StructInitializer, CollectionInitializer. No universal decoration field.
2. Scope level in decoration: drop for now, keep commented out for future re-add.
3. StructDeclaration fields: use single array of `PPASTStructField { PP_AST_Node* field; PP_AST_Node* initializer; }` to mirror Rust tuple `(Box<Node>, Option<Node>)`. Parallel arrays rejected.
4. StructInitializer: follow Rust model – array of tuples identifier/value per entry, not parallel name/value arrays.
5. TypeExpression: follow Rust model – `kind: PPASTTypeKind` enum with Identifier variant, plus `is_const`, `is_reference`, and separate `array_list_specifier`. Flag-based array/list representation replaced by specifier.
6. Function params & higher-order functions: keep params embedded in FunctionDeclaration as in pp-rust for now; function/lambda types will be handled in type system layer later.
7. Ownership moves: yes, set source pointer to NULL after transfer for memory safety. Document in APPEND_SYSTEM.md.
8. Vector growth: use #define constant initial capacity for all lists, double on full – same policy as error list/lexer token list. No magic numbers per list.
9. Node reuse ban: yes – dedicated nodes for Assignment, StructInitializer, etc., no reuse of binary expression for assignment.
10. Printing/debug: implement `pp_ast_root_print` now rather than stub.

## 6. Progress – 2026-03-19
- Committed AST design refactor to match Rust model:
  - Decoration moved from base node to specific expression nodes
  - StructDeclaration fields as `PP_AST_StructField[]`
  - StructInitializer entries as `PP_AST_StructInitEntry[]`
  - TypeExpression uses `kind` + `PP_AST_ArrayListSpecifier`
  - `AST_VECTOR_INIT_CAP` defined, vector growth policy unified
- `include/pp_ast.h` and `src/pp_ast.c` created/updated, compilation passes.
- `pp_ast_root_print` implemented, tab-indented recursive printer similar to cz
- Commits:
  - `AST design refactor per Rust model`
  - `Implement pp_ast_root_print similar to cz`

Next session tasks:
1. Add vector push helpers for all AST lists using AST_VECTOR_INIT_CAP + double-on-full.
2. Implement node creator functions with NULL_POINTER_ERROR_HANDLE + goto cleanup and ownership nulling.
3. Begin Pratt parser skeleton using Rust binding strengths, mapping `create_expression_ext`.


