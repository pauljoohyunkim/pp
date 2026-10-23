# Debug Findings – Struct Member Access CodeGen Crash

## Context
Continuation of PP code generator struct member access implementation.

- `pp_code_generator_generate_lvalue_struct_member_access` and `pp_code_generator_generate_rvalue_struct_member_access` are implemented and wired.
- Tests `CodeGenTest.StructMemberRead`, `StructMemberWrite`, `StructMemberMixedTypes` pass.
- Test `CodeGenTest.StructMemberMultiFieldAccess` crashes with assertion:
  ```
  src/pp_code_generator.c:1437: LLVMValueRef pp_code_generator_generate_rvalue_binary(...): Assertion `llvm_lhs_type' failed.
  ```

## Reproduction
Source written to `pp_test_code.pp`:
```pp
struct Data { a :: int32; b :: int32; c :: int32; }
func test_multi :: (d :: Data) -> int32 { let sum :: int32 = d.a + d.b + d.c; return sum; }
```
Running `./bin/pplang pp_test_code.pp` reproduces the assertion.

## GDB Investigation
Breakpoint at `pp_code_generator.c:1436` in `pp_code_generator_generate_rvalue_binary`:
- `cg->global_env_b->type_count = 7`
- `cg->global_env_b->type_map[1].type = 0x5555555c7f60` → `PP_TK_Int32`
- `lhs_decoration->resolved_type = 0x5555555c8180` → `PP_TK_Int32`
- `lhs_decoration->resolved_type` is **not** the same pointer as the Int32 entry in the backend type map.
- `pp_environment_backend_lookup_type(cg, lhs_decoration->resolved_type)` returns `NULL` for that pointer, causing `assert(llvm_lhs_type)` to fail.

Pointer mismatch observed:
- Backend Int32: `0x5555555c7f60`
- Decoration Int32: `0x5555555c8180`

Both have `kind == PP_TK_Int32`, but pointer identity differs.

## Hypothesis
`pp_environment_backend_lookup_type` uses pointer equality to find a type in `global_env_b->type_map`. If the semantic analyzer or type decay creates a distinct `PPType` instance for the same logical type, lookup fails.

Possible sources:
- `pp_type_decay_cg` creates temporary types via `pp_type_create` + `pp_global_type_table_find_type`. If `find_type` fails to canonicalize, a new copy may be used.
- Member access decoration may resolve to a type object that is a copy rather than the canonical global type table instance.
- Const wrappers or other type transformations could produce new `PPType` nodes with same kind but different address.

## Impact
All struct member reads/writes that involve a binary expression with more than one member access trigger the duplicate-type path and crash.

## Next Steps
1. Verify whether `lhs_decoration->resolved_type` for member access points to the canonical global type table instance.
2. Check `pp_global_type_table_find_type` / `pp_global_type_table_find_by_name` behavior for primitive types.
3. Consider making `pp_environment_backend_lookup_type` robust to type equality by kind/name instead of pointer equality, or ensure all decorations reference canonical types.
4. Ensure `pp_type_decay_cg` and semantic analysis always return canonical `PPType*` from the global type table.

## Files
- `pp_test_code.pp` – failing source
- `src/pp_code_generator.c` lines ~1436-1438
- `src/pp_type_table.c` `pp_type_decay_cg`
- Tests: `tests/codegentest.cpp` `CodeGenTest.StructMemberMultiFieldAccess`
