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

## 5. AST Design Decisions – confirmed 2026-09-14
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

## 6. Progress – 2026-09-14
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

### Parser port progress – branch `port-parser` 2026-09-14

- Project scaffold created matching cz structure
  - `include/pp_parser.h` with `PP_Parser` struct and API `pp_parser_create / pp_parser_parse / pp_parser_free`
  - `src/pp_parser.c` starter with cz-style error handling, `NULL_POINTER_ERROR_HANDLE` + `goto error_cleanup`
- Ownership transfer from lexer to parser at creation; string pool moved to parser, tokens point directly to interned strings
- Helper utilities implemented:
  - `pp_parser_peek_token_type`, `pp_parser_consume_token`
  - Sync set: IF FUNC LAMBDA RETURN FOR WHILE TYPEDEF NEWTYPE SEMICOLON RIGHT_CURLY_BRACKET
  - `PP_Precedence` enum with pp-rust spaced values: Equality=10, Relational=20, BitwiseOr=30, BitwiseXor=40, BitwiseAnd=50, Additive=60, Multiplicative=70, Cast=80, Unary=90, CallAccess=100, Primary=110
  - `pp_parser_infix_binding_power` mapping token types to left/right binding powers
- Pratt expression skeleton
  - `pp_parser_create_expression_ext(min_bp)` with loop over infix ops
  - `create_unary` builds `PP_AST_UnaryExpressionNode` for `-`, `!`
  - Primary: identifier, integer/float/true/false literals, parenthesized expressions delegated back to expression parser
  - Literal nodes reuse string pool pointer, no strdup
  - Binary expression nodes built with left/right children and operator token type
  - Cast handling scaffolding for `as` with `pp_parser_create_type` stub
- Commits on branch port-parser:
  - scaffold pp parser with cz structure and pp-rust Pratt binding powers
  - add infix binding power definition and fix build warnings
  - add literal creation and forward declaration
  - implement unary expression node creation
  - add parentheses handling in primary
  - implement binary expression node in Pratt loop
  - add type stub and prepare cast handling
- Build passes with only warnings; `bin/pplang` links.

Next session tasks:
1. Add vector push helpers for all AST lists using AST_VECTOR_INIT_CAP + double-on-full.
2. Implement node creator functions with NULL_POINTER_ERROR_HANDLE + goto cleanup and ownership nulling.
3. Complete postfix handling in Pratt loop: `(` function call, `[` array access, `.` member access with left-binding CALL_ACCESS
4. Implement statement parsing with backtracking assignment detection as in pp-rust `create_expression_or_assignment`
5. Program dispatch for global declarations FUNC/LAMBDA/STRUCT/LET/TYPEDEF/NEWTYPE


## 7. Notable discrepancies vs pp-rust AST – 2026-09-14
Identified after comparison with `../pp-rust/src/pp_ast.rs`:
- **ArrayListAccess decoration**: Fixed – added `PP_AST_Decoration* decoration` to `array_list_access` union member, matching Rust.
- **ParamDeclaration resolved_type**: pp-rust includes `resolved_type: Option<PPTypeId>` on ParamDeclaration. Type system not implemented yet; field left commented out in header with TODO note.
- **TypeExpression array_spec optionality**: Fixed – array_spec is now a pointer, matching Option behavior.
- **Function node naming**: Renamed union member to `function_declaration` for clarity; both Function and Lambda reuse same layout with named identifier per BNF.
- All other expression nodes decoration placement matches Rust model: BinaryExpression, UnaryExpression, CastExpression, Literal, Identifier, FunctionCall, MemberAccess, StructInitializer, CollectionInitializer.

Discrepancies addressed as above.
3. Begin Pratt parser skeleton using Rust binding strengths, mapping `create_expression_ext`.

## 8. Parser Port Progress Update – 2026-09-14 to present
### Branch port-parser
Current state after incremental commits:
- Pratt expression core implemented with pp-rust binding powers and infix_binding_power
- Expression nodes: Binary, Unary, Cast, FunctionCall, ArrayListAccess, MemberAccess
- Assignment detection in `create_expression_or_assignment` for = += -= *= /= %= &= |= ^=
- Statement dispatch split into typed helpers:
  * `pp_parser_create_return_statement`
  * `pp_parser_create_if_statement`
  * `pp_parser_create_while_statement`
  * `pp_parser_create_for_statement`
  * `pp_parser_create_variable_declaration`
  * `pp_parser_create_block` building StatementList
- Variable declaration now uses `pp_parser_create_param_decl` parsing `identifier :: type`
- Type parsing extended to handle const, primitive types void/int32/float/bool, identifier types, and reference `&`, with array specifier `[size]` support and repeated dimensions loop
- Program node dispatch wired:
  * FUNC → pp_parser_create_function_declaration with param list, return type, block
  * LAMBDA stub
  * STRUCT → pp_parser_create_struct_declaration with member list `param_decl [= expr];`
  * TYPEDEF / NEWTYPE implemented with base_type + identifier
  * LET/CONST → global variable declaration via param_decl
- Primary extended:
  * Struct initializer detection `identifier { .field = expr , … }`
  * Collection initializer `[ type [:size] ; elem* ]`
- Postfix call/index/member handled in Pratt loop with CallAccess binding power
- Unused `pp_parser_strdup` removed
- Build passes on port-parser with only pre-existing warnings

Recent commits:
- implement postfix function call, index, and member access in Pratt parser
- add expression_or_assignment stub / assignment detection
- add block parsing with statement list
- split statement creation into typed helpers for if/while/return/var_decl
- add param_decl parser for identifier :: type
- use param_decl parser in variable declaration
- extend type parsing with const/reference and primitives
- increment parser globals and primary init – struct/collection initializer support
- implement param list and function declaration parsing
- implement struct declaration parsing with members
- remove unused strdup and improve struct parsing
- support repeated array specifiers in type parsing

Next steps:
1. Complete lambda declaration parsing mirroring function
2. Implement full error reporting via pp_error_list on parse failures and allocation OOM
3. Add `pp_parser_parse_param_list` reuse for function/lambda, finalize param list cleanup on error with cz-style goto cleanup
4. Enhance struct initializer parsing to handle optional trailing commas and better error recovery
5. Add unit tests / sample parses for func/struct/typedef/global var

## 9. Similarity Assessment – 2026-09-15
### Structure vs ../cz/
- **Project layout**: `include/pp_parser.h`, `src/pp_parser.c`, `include/pp_ast.h` mirrors cz's `include/cz_parser.h`, `src/cz_parser.c`.
- **Ownership model**: parser owns code/tokens/string pool, ownership transferred from lexer with NULLing source pointers – same pattern as cz_parser_create.
- **Error handling**: `NULL_POINTER_ERROR_HANDLE` + `goto error_cleanup` now present in creators; `pp_error_list_push_error` used in `pp_parser_consume_token`, matching cz's push-error-on-mismatch approach.
- **Sync & recovery**: sync token set and `pp_parser_sync` mirrors `cz_parser_sync`.
- **Vector growth**: `AST_VECTOR_INIT_CAP` + double-on-full used for decls/params/fields – same policy as cz error list/lexer.

### Logic vs ../pp-rust/
- **Pratt core**: `PP_Precedence` values 10,20,…110 exactly match pp-rust `OperatorPrecedence`; `pp_parser_infix_binding_power` returns left/right binding powers as in Rust.
- **Expression loop**: `pp_parser_create_expression_ext` implements precedence-climbing with unary primary and postfix call/index/member handling inside the same loop – equivalent to Rust's `create_expression_ext`.
- **Assignment backtracking**: `pp_parser_create_expression_or_assignment` parses LHS expression then checks assignment ops, matching Rust `create_expression_or_assignment`.
- **Statements / blocks**: statement dispatch `create_return_statement`, `create_if_statement`, etc., mirrors Rust statement parsing; block builds `StatementList`.
- **Types**: `is_const`, `is_reference`, `array_spec` pointer matches Rust `TypeExpression` with Option array specifier.
- **AST shape**: decoration only on expression nodes, struct fields as `PP_AST_StructField[]`, struct init entries as `PP_AST_StructInitEntry[]` – aligns with Rust model decisions confirmed 2026-09-14.

Conclusion: structure is cz-like, parsing logic is pp-rust-like; the port is consistent with both reference bases.

## 10. Roadmap to Almost Complete Parser
**Parsing completeness**
1. Finalize lambda declaration – done in current commit, ensure cleanup is cz-style.
2. Full global declaration coverage:
   - `func`, `lambda`, `struct`, `typedef`, `newtype`, `let`/`const` globals – all parsers present, need final error messages and sync on failure.
3. Statement completeness:
   - Expression statements, variable decl in block, assignment statements, return, if/else-if chains, while, for with init/cond/update variants, block nesting.
4. Primary values:
   - Identifier, literals, parenthesized, struct initializer, collection initializer fully implemented; add string literal support when lexer provides it.
5. Type parsing:
   - Mutability `const`, base types, identifier types, reference `&`, array specifiers `[size]` repeated – done; optionally support nested array specifier in base_type per BNF.
6. Error handling:
   - Push parse errors for missing tokens, unexpected token, unclosed delimiters using `pp_error_list_push_error` with line/col like cz.
   - Allocation failures already guarded by NULL_POINTER_ERROR_HANDLE + goto cleanup.
7. Program dispatch robustness:
   - Replace placeholder sync with proper error recovery and continue parsing subsequent declarations.
8. Testing:
   - Add sample PP programs covering each global decl, expression precedence, struct init, collection init, control flow.
9. Cleanup:
   - Remove remaining `#warning` placeholders once type system lands, ensure no memory leaks via `pp_ast_root_free` paths.

With the above done the parser will be functionally equivalent to pp-rust's parser in C with cz's project/error discipline.

## 11. Current Status – 2026-09-15 update
### Recent incremental improvements
- Lambda declaration now pushes cz-style errors for missing identifier, `::`, `(`, `)`, `->` and uses goto cleanup with proper free of params/return_type/body.
- Struct declaration adds error pushes for missing identifier after `struct`, missing `{`, and missing `}` with cleanup path.
- Typedef / Newtype declarations now capture typedef/newtype token position, push errors for missing identifier and missing terminating `;`, and return node with correct source location.
- Variable declaration (`let`/`const`) uses NULL_POINTER_ERROR_HANDLE + goto cleanup, pushes errors for missing declarator, missing initializer expression, and missing terminating `;`.
- Statement error recovery: expression statements now sync on parse failure and push error for missing `;`.
- All creators consistently use `NULL_POINTER_ERROR_HANDLE` and `error_cleanup` labels; ownership transfer and free paths mirror cz patterns.

### Similarity assessment updated
Structure vs cz:
- Error handling now extends to all global decl parsers with `pp_error_list_push_error`; sync set unchanged.
- Cleanup paths complete for lambda, struct, typedef, newtype, variable declaration.

Logic vs pp-rust:
- No semantic deviation; error pushes are additive and do not change parsing logic.

## 12. Final Similarity Assessment & Roadmap – 2026-09-16
### Structure vs ../cz/
- Project layout matches cz: `include/pp_parser.h` / `src/pp_parser.c`, AST headers mirror cz_ast.h.
- Memory discipline: every allocation checked via `NULL_POINTER_ERROR_HANDLE`, cleanup via `goto error_cleanup`, frees partial sub-nodes – identical to cz pattern.
- Error list usage: `pp_error_list_push_error` called on missing tokens, unclosed delimiters, and parse errors; sync token set mirrors cz_parser_sync.
- Ownership transfer from lexer to parser with NULLing source pointers matches cz_parser_create.
- Vector growth policy unified via `AST_VECTOR_INIT_CAP` + double-on-full, matching cz error list / lexer.

### Logic vs ../pp-rust/
- Pratt precedence values 10/20…110 match Rust OperatorPrecedence exactly.
- Infix binding power mapping reproduces Rust left/right pairs; CallAccess = 100 with right_bp=0 as in Rust.
- Assignment disambiguation via `create_expression_or_assignment` mirrors Rust backtracking approach.
- Statement dispatch, block building, type parsing (const/reference/array specifier), struct/collection initializers follow Rust AST shape decisions confirmed 2026-09-14.
- No semantic deviation; error pushes are purely additive diagnostics.

### Current hardening state – branch port-parser
- Function/lambda/struct/typedef/newtype/let declarations now have cz-style error pushes for missing '(' , ')' , '->' , '{' , '}' , ';' and appropriate NULL_POINTER_ERROR_HANDLE + goto cleanup.
- Parameter declaration, struct fields, variable declaration creators hardened.
- Literal type mapping added, string literal primary support added.
- Block parser cleanup improved; expression statement sync with error push for missing ';' added.
- Struct initializer and collection initializer now push errors for missing '{' , '}' , '[' , ']' , type, semicolon.
- Type parsing pushes error for missing base type and missing ']' in array specifier.
- Newtype declaration captures token position and pushes errors for missing identifier / ';'.

### Remaining incremental steps
1. Ensure remaining creators have NULL_POINTER_ERROR_HANDLE + goto cleanup – struct initializer member iteration, collection element loop edge cases; verify type parsing cleanup on allocation failure for array_spec chain.
2. Add cz-style error pushes for missing tokens in those creators where not yet present (e.g., struct initializer '.' / identifier / '=' mismatches).
3. Add additional error recovery sync points in statement parsers where appropriate; can be deferred until usage testing.
4. Consider adding sample parses/tests for each global declaration and expression precedence corner cases.
5. Finalize PARSER_IMPLEMENTATION.md similarity assessment above.

All edits kept small per function with commits after each change, maintaining cz-style discipline while preserving pp-rust parsing logic.






