# 05 – Semantics & Constraints

This chapter documents what is allowed and what is not allowed by the PP semantic analyzer.

## Type Construction
- Types are built Base → Modifiers → Reference.
- `const` can wrap any base type.
- `&` must be outermost.

## Variables
- Global variables cannot be references.
- Duplicate global symbol names are errors.
- Const variables must be initialized.
- Reference variables must be initialized with an l-value.
- Non-const reference cannot bind to const value.
- Uninitialized variable is error if type is const or reference; otherwise zero-initialized.

## Typedef / Newtype
- Base type must exist.
- Base type must not be const or reference.
- Duplicate names are errors.
- Newtype base cannot be function/lambda.
- Typedef creates alias, newtype creates distinct type.

## Structs
- Duplicate field names are errors.
- Struct fields with initializers must be compile-time constants.
- Reference fields cannot have default initializers.
- Reference fields must be initialized at instantiation.
- Struct cycle detection is performed.

## Arrays & Lists
Allowed element types:
- Value types only
- No `Reference` as element type
- No `void`
- No function or lambda
- No type that transitively contains a reference
- Array size must be non-zero and compile-time constant

Errors produced:
- `Element type is null`
- `element type cannot be a reference`
- `Array/List element type cannot be void`
- `Array/List element type cannot be function/lambda`
- `Array/List element type cannot transitively contain a reference`
- `Array size cannot be zero`
- `Could not compute array size`

Allowed:
```pp
let a :: [int32 : 4];
let b :: const [int32 : 4];
let c :: [int32 : 4]&   // reference to whole array
```

Disallowed:
```pp
let bad :: [int32& : 4];      // element is reference
let bad2 :: [S : 4];          // S contains reference
let bad3 :: [void : 4];
```

## Functions & Lambdas
- Duplicate names are errors.
- Parameter types must be fully defined.
- Duplicate parameter names are errors.
- Return type non-void requires all paths to return.
- Lambda return type must be present.
- Lambda is pure: no mutable reference parameters.

## Initialization
- Global initializers must be constexpr.
- Struct default initializers must be constexpr.
- Reference fields must be initialized with l-value at instantiation.
- Initializer type must match variable/field after decay.

## Casting
- Required between newtype and its base.
- Cast does not implicitly change const/reference qualifiers.

## Const Correctness
- Const value cannot be modified.
- Const reference binds to const or immutable data.
- Assignment to const variable is error.

## Control Flow
- Condition expressions must be `bool`.
- `for` init/update must be valid statements.

## Expression Checks
- Binary/unary operands must be compatible types.
- Identifier must be in scope.
- Struct member access requires field exists.
- Function call arguments must match parameter types.
- Array/list index must be integer.
