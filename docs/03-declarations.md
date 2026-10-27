# 03 – Declarations

## Global Declarations
Allowed at file scope:
- Variable: `let name :: type = initializer;`
- Function: `func name :: (params) -> return_type { body }`
- Lambda: `lambda name :: (params) -> return_type { body }`
- Struct: `struct Name { members }`
- Typedef: `typedef base Name;`
- Newtype: `newtype base Name;`

## Variables
```pp
let x :: int32 = 1;
let y :: const int32 = 1;
let z :: float;
```
Initializer must be compile-time. No initializer → zero init. Reference not allowed at global scope.

## Typedef & Newtype
```pp
typedef int32 int;
newtype int32 AGE;
```
Base type must be defined, not const/ref. Check duplicate and cycles.

## Struct
```pp
struct Point {
    x :: float;
    y :: float
}
struct Jacobian {
    x :: int32;
    y :: int32;
    z :: int32 = 1
}
struct Context {
    n :: int32&
}
```
Fields may have default initializer, unless reference.

## Functions & Lambdas
```pp
lambda relu :: (x :: float) -> float { ... }
func get_counter :: () -> int32 { ... }
func waste_cpu_cycle :: (n :: int32) -> void { ... }
```
Input types required. Output `void` optional to omit.
Lambda is pure/stateless, no mutable reference passing, can use const globals.
