# 10 – Semantics: Functions & Lambdas

## Declaration
Functions are global.

```pp
func name :: (params) -> return_type { body }
lambda name :: (params) -> return_type { body }
```

Rules:
- Duplicate names are errors.
- Parameter types must be fully defined.
- Duplicate parameter names are errors.
- Return type must be defined.
- If return type is non-void, all control paths must return a value.
- Parameter count in AST must match type.

## Parameters
- Local scope created for parameters.
- Parameter symbols are created with escapable reference flag if type is reference.
- By-value List parameters are disallowed; use reference. *Temporary restriction for move-only v1.*

## Lambdas
- Kind must be lambda, not function.
- Same rules as functions for parameters and return.
- Lambdas are pure/stateless in design; mutable reference parameters are restricted.

Errors:
- Symbol previously defined
- Invalid return type for function/lambda
- Failed to intern function/lambda type
- Failed to register function/lambda
- "%s" is not a function / is not a lambda function
- Function/Lambda returns non-void, but there may exist a path that it exits without returning something
- Function has wrong number of types
- Duplicate parameter detected
- Could not push parameter to parameter scope
- By-value List parameter is not allowed in move-only v1; use a reference

## Calls
- Argument types must match parameter types after decay.
- Function must be defined before use.
- Return value type is determined by function type.
