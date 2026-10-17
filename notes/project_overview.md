# PP Compiler Project Overview

This is a compiler implementation for a new programming language called "pp".

## Language Features (PP)

### Types
- **Primitives**: `int32`, `float`, `bool`, `void`
- **Type Modifiers**:
  - `const` - immutable after initialization
  - `&` - reference type
- **User-defined Types**:
  - `typedef` - creates an alias to existing type
  - `newtype` - creates a distinct type based on primitive
- **Aggregates**:
  - `struct` - composite type with named fields
  - Arrays: fixed-size, compile-time determined
  - Lists: resizable runtime arrays (planned)

### Declarations
- **Variables**: `let name :: type = initializer;`
- **Functions**: `func name :: (params) -> return_type { body }`
- **Lambdas**: `lambda name :: (params) -> return_type { body }` (pure/stateless)
- **Globals**: Variables, functions, typedefs, newtypes, structs at file scope

### Expressions
- Arithmetic: `+ - * / %`
- Assignment: `= += -= *= /= %= &= |= ^=`
- Bitwise: `& | ^`
- Comparison: `== != < > <= >=`
- Logical: `&& ||`
- Casting: `expression as type`
- Function calls: `func_name(args)`
- Array access: `array[index]`

### Statements
- Variable declaration/initialization
- Assignment
- Control flow: `if`, `for`, `while`
- Return: `return [expression];`
- Blocks: `{ statements }`

## Compiler Pipeline

1. **Lexing**: Converts source text to token stream
2. **Parsing**: Builds AST from tokens using recursive descent
3. **Semantic Analysis**: Type checking, const/reference checks, scope resolution
4. **Code Generation**: Translates annotated AST to target output

## Modules

* `src/pp_string_pool.c` – Interning pool for all source text fragments. Owns every identifier/literal string pointer used by the AST and type system; callers must never free these pointers.
* `src/pp_tokens.c` / `include/pp_tokens.h` – Token definitions and token stream structures. Lexer owns the token array until parsing is complete.
* `src/pp_lexer.c` – Lexical analysis. Produces tokens from source using the string pool for lexemes.
* `src/pp_parser.c` – Recursive-descent parser. Consumes tokens, builds AST nodes. Parser owns the AST root for the duration of a compilation unit and transfers ownership to semantic analysis.
* `src/pp_ast.c` / `include/pp_ast.h` – AST node definitions, construction and recursive free. Parent nodes own children; decorations are owned by the node that created them. Identifier strings are pool-owned.
* `src/pp_type.c` / `include/pp_type.h` – PPType representation for primitives, modifiers, typedef/newtype, structs, arrays. Types are immutable after creation.
* `src/pp_type_table.c` – Global Type Table. Owns all PPType instances created during Pass 1. Symbol table lookups for types go through here; callers hold non-owning pointers.
* `src/pp_symbol.c` / `src/pp_symbol_table.c` – Symbol and scoped symbol tables. Global symbol table owns symbols for globals; function/lambda bodies own their local symbol tables which are freed with the AST.
* `src/pp_error.c` – Error list management. Semantic analyzer owns the error list for a compilation unit.
* `src/pp_semantic_analyzer.c` – Pass 1 build of global tables, Pass 2 full analysis, expression checking, struct layout building, cycle detection. Owns temporary structures during analysis; final decorated AST is owned by the parser until program exit.
* `src/pplang.c` – Driver / CLI entry point. Orchestrates lexer → parser → semantic analysis → codegen and frees top-level resources.

## Notes on Ownership
See `notes/ownership_model.md` for a concise ownership map.

## Current Status

The compiler is being built from ground up in C:
- Lexer, parser, semantic analyzer and code generator planned following cz architecture
- Test-driven development with Google Test
- Build system via Makefile + clang + LLVM

## Usage

```bash
# Build compiler
make pplang

# Run compiler on PP source file
./bin/pplang path/to/file.pp

# Run unit tests
make unittest
./bin/unittest
```

## Design Notes

- C implementation for transparency and minimalism, successor of cz
- Error reporting includes line/column information
- Follows classic compiler architecture phases
- Extensible design for adding new features
- Test-driven development approach with test modules
