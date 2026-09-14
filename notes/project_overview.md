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
- **Variables**: `name :: type = initializer;`
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
