# PP Language Documentation

Welcome to the PP language user documentation.

## Structure
- [Getting Started](01-overview.md) – What is PP, quick start
- [Types](02-types.md) – Primitive, modifiers, typedef/newtype, struct, array, list
- [Declarations](03-declarations.md) – Variables, functions, lambdas, structs
- [Expressions & Statements](04-expressions.md) – Operators, control flow, blocks
- [Semantics: Types & Constraints](07-semantics-types-constraints.md)
- [Semantics: Variables](08-semantics-variables.md)
- [Semantics: Structs](09-semantics-structs.md)
- [Semantics: Arrays & Lists](10-semantics-arrays-lists.md)
- [Semantics: Functions & Lambdas](12-semantics-functions-lambdas.md)
- [Semantics: Initialization](13-semantics-initialization.md)
- [Semantics: Const Correctness](14-const-correctness.md)
- [Semantics: Expressions](15-semantics-expressions.md)
- [Known Limitations](16-known-limitations.md) – Temporary bans and warnings
- [Examples](11-examples.md) – Common patterns and snippets

## Quick Start
```bash
make pplang
./bin/pplang path/to/file.pp
make unittest
```

## Notes
See `notes/` for original design notes.
