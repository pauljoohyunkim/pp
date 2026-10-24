# Array / List Codegen Findings

## Semantic Analyzer Status

Arrays are already semantically supported.

### Type creation
* `pp_type_from_type_node` handles `PP_AST_TypeArrayNode` / `PP_AST_TypeListNode`.
* `pp_type_validate_collection_element` rejects:
  * void element type
  * function / lambda element type
  * element type transitively containing a reference
* Array size is evaluated at compile time via `pp_array_size_from_simple_expr`. Size must be >0.
* `const` is canonicalised inward for arrays/lists.

### Array / List access
`pp_semantic_analyzer_check_array_list_access_expression`:
* Base must decay to `PP_TK_Array` or `PP_TK_List`.
* Index must decay to `uint32`.
* Compile-time bounds check for fixed-size arrays when index is constexpr.
* Const propagation: if base is const, result type is `const element`.
* **Value category propagation**: 
  ```
  value_cat = base_decoration->value_category == LVALUE ? LVALUE : RVALUE
  ```
  Array access is an lvalue iff the base is an lvalue.
* Decoration created with resolved_type, value_category, is_constexpr.

### Collection initializer
Semantic analysis validates element count vs size, allows implicit zero-fill, rejects reference element types.

## Codegen Status

Codegen does NOT yet support arrays/lists.

* `pp_backend_lower_type` asserts for `PP_TK_Array` / `PP_TK_List`:
  ```
  assert(0 && "TODO: pp_backend_lower_type does not yet support lowering, array, list, function, or lambda");
  ```
* `pp_code_generator_generate_rvalue_collection_init` is stubbed: `return NULL;`
* No handling for `PP_AST_ArrayListAccessNode` in `pp_code_generator_generate_lvalue` / `pp_code_generator_generate_rvalue`.

## L-value / R-value semantics

* Array variable itself is lvalue.
* `arr[i]` is lvalue iff `arr` is lvalue; otherwise rvalue.
* Codegen must generate GEP `[0, idx]` for array element pointer.
* Loading requires `lvalue_to_rvalue` when used in expression context.
* Arrays cannot contain references per semantic rules, so no reference semantics needed for elements.

## Codegen Plan

1. Backend type lowering
   * `PP_TK_Array` → LLVM `ArrayType [size x llvm_elem]`
   * Global arrays: LLVM global of array type
   * Local arrays: `alloca` of array type

2. Variable declaration
   * Global: `LLVMAddGlobal` with array type + `LLVMConstArray` initializer
   * Local: `LLVMBuildAlloca` + stores via GEP

3. Array / list access codegen
   * Add `PP_AST_ArrayListAccessNode` case to `pp_code_generator_generate_lvalue`
   * Generate base lvalue, generate index rvalue, `LLVMBuildGEP2` to element pointer
   * Rvalue path: lvalue → `l_value_to_r_value` if needed

4. Collection initializer codegen
   * For globals: build `LLVMConstArray`
   * For locals: alloca + store each element via GEP

5. Tests
   * Semantic tests already exist
   * Add CodeGen tests for array decl, access, init, const propagation
   * Verify IR contains `alloca [N x ...]`, `getelementptr`, `load`/`store`

## Notes

* Lists can be deferred after fixed-size arrays are stable.
* The pathological struct-initializer lvalue edge case is separate and documented in `edge_cases.md`.
