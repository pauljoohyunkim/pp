# Codegen Failures

## Test: CodeGenTest.GlobalStructInit
Source:
```
struct Point { x :: int32; y :: int32; }
let p :: Point = Point { .x = 1, .y = 2 };
```

Failure:
```
LLVMVerifyModule failed: Global variable initializer type does not match global variable type!
ptr @p
```

Smoke test via `bin/pplang` succeeds and prints correct IR:
```
@p = global %Point { i32 1, i32 2 }
```

But `LLVMVerifyModule` fails in unit test context. Possible mismatch between pplang and test harness codegen path, or module state leaking between tests.

## Test: CodeGenTest.StructInitPartialWithDefault
Source:
```
struct S { a :: int32; b :: int32 = 99; }
let s :: S = S { .a = 5 };
```

Failure:
```
LLVMVerifyModule failed: Global variable initializer type does not match global variable type!
ptr @s
```

Same symptom as above.

Need investigation of why LLVMVerifyModule rejects global struct initializer in unittest harness while pplang succeeds.
