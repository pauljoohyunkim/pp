# List Review Checklist – allow-recursive-types work

## Summary of commits since dev

Commit range `dev..list-codegen-recursion`:

* b8462e2 (HEAD -> list-codegen-recursion, origin/list-codegen-recursion) refactor: remove unused visitation state, clean up PP_TypeVisitState
* 5a5da01 refactor: remove visited parameter from pp_type_needs_drop/pp_type_is_copyable
* fe4f762 refactor: remove __thread and global type table dependency from type predicates
* 3037f8b Allow recursive types: static-free visit stacks and declare-then-body codegen (#42)
* 75483d6 code generation recursion guard
* 31b8824 Identify problem with infinite recursion in code generator involving struct with list of itself as field
* ff8bb56 Add code gen smoke tests to unit test and format code
* e84cc5a docs: mark Step 1 and Step 5 complete in copy/drop cache plan
* 410b519 codegen: extend per-type copy/drop caching to List/Struct
* d52b128 codegen: wire per-type copy/drop caching with ensure helpers for trivial types
* 539a80d Basic ensure copy fn
* 5bdf477 Copy/Drop_T caching helpers
* 4be68f1 List in list (#41)
* 40c8710 Struct init copy
* 43a0eea Struct containing list generation
* 68bb98b Copy then mutate test
* e0385b0 List copy init and assign test
* f707e6b Array elementwise drop
* 8cc58d1 List copy
* d24edf5 No-op for self assignment
* e787868 Lift by-value parameter copy ban
* aa9200d Function call and copy drop parameters
* 035b4e6 Lift semantic analyzer ban for copying list
* 55c28ad Wire to assignment
* 2c29aee Variable declaration generation with copy/drop
* 2d14baf Emit copy stub
* 6e17193 Type is trivially copyable checker
* 38ed8c4 Drop recursively in code generator

Total 26 commits.

## Key files changed

```text
.pi/skills/codegen-test-smoke-then-add/SKILL.md
dev-docs/semantic-analysis.md
docs/10-semantics-arrays-lists.md
include/pp_code_generator.h
include/pp_code_optimization.h
include/pp_linker_offload.h
include/pp_type.h
notes/allow_recursive_types_plan.md
notes/list_copy_move_cache_plan.md
notes/list_copy_move_design.md
notes/list_copy_move_plan.md
pp_test_code.pp
src/pp_code_generator.c
src/pp_code_optimization.c
src/pp_linker_offload.c
src/pp_parser.c
src/pp_semantic_analyzer.c
src/pp_type.c
src/pplang.c
tests/codegentest.cpp
```

Core implementation:
* src/pp_code_generator.c – per-type copy/drop caching, declare-then-body codegen, static-free stacks
* src/pp_type.c – static-free cycle detection for type predicates, removal of __thread guards
* src/pp_semantic_analyzer.c – lift list copy bans, copyability checks
* include/pp_code_generator.h, include/pp_type.h – API updates

## Notes findings

### notes/allow_recursive_types_plan.md
Goal: allow `struct S { s :: [S]; }` and mutually recursive structs via declare-then-body codegen without banning type.

Key status:
* Step 1 – make ensure_* helpers declare-then-body: [x]
* Step 2 – verify codegen termination with smoke tests: [x]
* Step 3 – runtime semantics decision: document behavior, runtime guard option pending [ ]
* Step 4 – tests added for RecursiveStructCopy and MutuallyRecursiveStructCopy: [x]
* Step 5 – documentation updates: [ ] pending
* Step 6 – static-free cycle detection for semantic checks: [x]
  - Replace __thread guard_stack/guard_depth in src/pp_type.c with local stack passed to impl
  - Refactor pp_type_needs_drop, pp_type_is_copyable, pp_type_is_trivially_copyable to _impl with stack
  - Remove __thread ref_guard from src/pp_semantic_analyzer.c
  - Ensure codegen path with PP_TypeVisitState unchanged
* Risks noted: runtime infinite loop on user-built cycles via mutation; static globals removed.

### notes/list_copy_move_cache_plan.md
Goal: per-type copy_T/drop_T caching.

Status:
* Design helper signatures: [x]
* Refactor emit logic into reusable generators: [x]
* Extend ensure_copy_fn for List/Struct: [x]
* Extend ensure_drop_fn for List/Struct: [x]
* Wire emit functions: [x]
* Testing: make && make unittest passes [x]; Valgrind clean pending [ ]
* Optional improvements: recursion guard [x]

### notes/list_copy_move_plan.md
Option A implementation checklist for list copy/move.

Completed items include audit, make drop recursive and per-type, default-initialize owning variables, per-type copy generation, wire copy sites, update semantic analysis, tests, documentation, final cleanup, list-of-lists support.

List-of-lists support implemented:
* pp_type_is_copyable for List requires element copyable recursively
* Element-wise copy/drop for List with fast memcpy path for trivial elements
* emit_drop for List iterates len and recursively drops element when needed
* Semantic analyzer break removed for collection init decoration
* Acceptance: [[int32]] copies correctly, no leak, flat-list tests unchanged

### notes/list_copy_move_design.md
Design decisions documented:
* Per-type copy_T/drop_T, drop stack for owning types
* Type predicates: pp_type_needs_drop, pp_type_is_trivially_copyable, pp_type_is_copyable
* Semantic checks for init/assign/by-value params
* Codegen: alloca init, emit_copy/emit_drop, self-assignment safe
* Limitations: global struct field default initializers must be constexpr; pop/resize out of scope

## Checklist items for review

### API changes
- [ ] Review changes to `include/pp_code_generator.h` – new ensure/emit signatures
- [ ] Review changes to `include/pp_type.h` – type predicate signatures with stack params
- [ ] Verify public API stability for `PP_CodeGenerator` cache entry fields

### Static-free cycle detection
- [ ] Confirm `__thread` guard_stack/guard_depth removed from `src/pp_type.c`
- [ ] Confirm `pp_type_needs_drop_impl`, `pp_type_is_copyable_impl`, `pp_type_is_trivially_copyable_impl` use explicit stack arrays
- [ ] Confirm `__thread ref_guard` removed from `src/pp_semantic_analyzer.c`
- [ ] Verify no remaining `__thread` usage in type predicates: `grep -rn "__thread" src/pp_type.c src/pp_semantic_analyzer.c`
- [ ] Run `make unittest` – 282 tests still pass

### Declare-then-body codegen
- [ ] Verify `pp_code_generator_ensure_copy_fn` creates function, registers `entry->copy_fn` before body generation
- [ ] Verify `pp_code_generator_ensure_drop_fn` follows same declare-then-body pattern
- [ ] Verify builder save/restore works for forward declaration
- [ ] Check IR for `pp.copy.Node` and recursive call via cache for self-referential struct

### Visited param / PP_TypeVisitState removal
- [ ] Confirm `visited` parameter removed from `pp_type_needs_drop` / `pp_type_is_copyable`
- [ ] Confirm `PP_TypeVisitState` cleanup complete and unused
- [ ] Search codebase for dead references: `grep -rn "PP_TypeVisitState" src include`

### Removal of __thread and global type table dependency
- [ ] Verify type predicates no longer depend on global type table for cycle detection
- [ ] Confirm no static mutable state in type predicates

### Tests
- [ ] `CodeGenTest.RecursiveStructCopy` passes
- [ ] `CodeGenTest.MutuallyRecursiveStructCopy` passes
- [ ] Smoke test for `struct Node { v :: int32; kids :: [Node]; }` copy
- [ ] Smoke test for mutually recursive `A`/`B` with list fields
- [ ] Existing list copy/move tests still pass: copy then mutate, assign over existing, self-assignment, list of lists, struct containing list
- [ ] Run `make unittest` and capture output
- [ ] Valgrind clean for ListOfListsDeepCopy

### Runtime semantics
- [ ] Document current behavior for deep copy of cyclic owned graph may loop at runtime
- [ ] Decide if runtime guard needed or acceptable limitation
- [ ] Add test for tree copy 3 levels deep to ensure no leak/double free

### Documentation updates
- [ ] Update `notes/list_copy_move_cache_plan.md` to note recursive types now supported
- [ ] Update notebook page `list-copy-recursive-codegen`
- [ ] Update `docs/10-semantics-arrays-lists.md` with recursive type support notes
- [ ] Update `dev-docs/semantic-analysis.md` with copyable vs move-only notes
- [ ] Ensure `notes/allow_recursive_types_plan.md` Step 5 documentation marked complete

### Cleanup
- [ ] Remove all temporary `#warning`s related to list semantics
- [ ] `make clean && make` produces zero warnings about list semantics
- [ ] Verify no unused visitation state code remains

### Notes files review
- [ ] Re-read `notes/allow_recursive_types_plan.md` for open items
- [ ] Re-read `notes/list_copy_move_cache_plan.md` for pending Valgrind/ASan
- [ ] Confirm `notes/list_copy_move_plan.md` Step 9 list-of-lists acceptance criteria complete
