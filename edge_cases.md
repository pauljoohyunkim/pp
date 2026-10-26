# Edge Cases

## Temporary struct initializer used as lvalue source

**File**: `pp_test_code.pp`
```pp
struct S { a :: int32; b :: int32 = 99; }
func f :: () -> void {
    let s :: int32 = S { .a = 5 }.a;
}
```

**Original Symptom**
```
pplang: src/pp_code_generator.c:1186: LLVMValueRef pp_code_generator_generate_lvalue(...): Assertion `0 && "Cannot get l-value"' failed.
```

**Diagnosis**
* The initializer `S { .a = 5 }.a` is a `MemberAccessNode` whose object is a `StructInitializerNode`.
* `pp_code_generator_generate_rvalue_struct_member_access` originally always went through the lvalue path, calling `pp_code_generator_generate_lvalue` on the member access node.
* `pp_code_generator_generate_lvalue_struct_member_access` called `pp_code_generator_generate_lvalue` on the struct initializer object, which is an rvalue, not supported -> assert.

**Fix**
* Branch on the base's `value_category` in `pp_code_generator_generate_rvalue_struct_member_access`.
  * Base is lvalue → keep existing GEP lvalue path.
  * Base is rvalue → generate base as rvalue, then `LLVMBuildExtractValue` from the aggregate.
* Add precondition assert in `pp_code_generator_generate_lvalue` that decoration value_category == LVALUE.
* Struct initializer rvalue generation already builds undef + insertvalue, so extract works.

**Status**
Fixed. `pp_test_code.pp` now compiles and codegens successfully:
```
define void @f() {
entry:
  %s = alloca i32, align 4
  store i32 5, ptr %s, align 4
  ret void
}
```
All 36 CodeGen tests pass.

## Rvalue struct member access edge cases

Added robust CodeGen tests in `tests/codegentest.cpp` for rvalue struct bases:

* **StructMemberAccessViaFunctionCallRvalue** – non-constant temporary via function return: `make().a`. Verifies `extractvalue` is used for rvalue base and value propagates.
* **NestedRvalueMemberAccess** – nested initializer chain `S { .inner = T { .x = 1 } }.inner.x`. Verifies codegen succeeds with constant folding/extract.
* **MixedLvalueRvalueMemberAccess** – lvalue base `s.a` still uses GEP/`field_gep`. Confirms lvalue path unchanged.
* **ConstPropagationRvalueMemberAccess** – const field accessed on rvalue result from function. Verifies `extractvalue` and correct constant.

All 41 CodeGen tests pass with `make pplang unittest`.
