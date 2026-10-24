# Edge Cases

## Temporary struct initializer used as lvalue source

**File**: `pp_test_code.pp`
```pp
struct S { a :: int32; b :: int32 = 99; }
func f :: () -> void {
    let s :: int32 = S { .a = 5 }.a;
}
```

**Symptom**
```
pplang: src/pp_code_generator.c:1186: LLVMValueRef pp_code_generator_generate_lvalue(...): Assertion `0 && "Cannot get l-value"' failed.
```

**Diagnosis**
* The initializer `S { .a = 5 }.a` is a `MemberAccessNode` whose object is a `StructInitializerNode`.
* `pp_code_generator_generate_rvalue_struct_member_access` obtains an lvalue for the member by calling `pp_code_generator_generate_lvalue` on the member access node.
* `pp_code_generator_generate_lvalue_struct_member_access` in turn calls `pp_code_generator_generate_lvalue` on `node->member_access.object`, i.e. the struct initializer.
* `pp_code_generator_generate_lvalue` only supports `IdentifierNode`, `FunctionCallNode`, `MemberAccessNode`. `StructInitializerNode` is not supported, so the default case asserts `Cannot get l-value`.

**Why it happens**
A struct initializer `S { .a = 5 }` denotes a temporary value, not a named storage location. Taking an lvalue of a temporary is semantically invalid. The codegen currently treats member access uniformly as lvalue → rvalue, which works for named variables/structs but not for temporaries.

**Workaround / expected handling**
* This is pathological usage and will be addressed later.
* Options:
  * Disallow member access on temporary struct initializers in semantic analysis, or
  * Teach codegen to materialize the temporary into an alloca and then take its field, i.e. generate an rvalue directly for struct initializer member access rather than going through lvalue path.

**Status**
Recorded for later fix. Currently crashes with assertion.
