# 07 – Semantics: Structs

## Declaration
```pp
struct Name {
    field :: type [= default];
    ...
}
```

Rules:
- Duplicate struct names are errors.
- Duplicate field names within a struct are errors.
- Field types must be well-defined.
- Struct recursive cycles are detected and rejected.

## Field initializers
- Optional default initializer per field.
- Initializer must be compile-time constant.
- Initializer type must match field type after decay.
- Reference fields cannot have default initializers.
- Reference fields must be provided at struct instantiation.

## Struct layout
- Layout is built after type registration.
- Fields retain order of declaration.
- Struct contains reference if any field type transitively contains reference.

Errors:
- Type previously defined
- Struct field is duplicate
- Failed to create type for struct field
- Failed to add struct field
- Struct contains recursive cycle
- Initializer for struct field is invalid
- Global scope initializer for struct field should be constexpr
- Struct field is a reference and cannot have a default initializer at global scope
- Initializer for struct field does not match the field type

## Instantiation
```pp
let p :: Point = Point { .x = 1.0, .y = 2.0 };
```
- All non-defaulted fields must be provided.
- Field names must match exactly.
- Initializers must be compatible types.
