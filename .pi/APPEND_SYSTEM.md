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
- Control flow: `if`, `for`, `while` (partially implemented)
- Return: `return [expression];`
- Blocks: `{ statements }`

## Compiler Pipeline

1. **Lexing** (`PPLexer`): Converts source text to token stream
2. **Parsing** (`PPParser`): Builds AST from tokens using recursive descent
3. **AST Representation**: Structured tree of `PPASTNode` variants
4. **Output**: Currently prints AST structure (future: code generation)

## Current Status

The compiler is in active development:
- ✅ Lexer implements tokenization of all language constructs
- ✅ Parser handles global declarations, functions, lambdas, typedefs, newtypes
- ✅ AST supports expressions, statements, declarations
- ⚠️ Some features partially implemented (control flow, full expression precedence)
- ❌ No code generation yet (AST interpreter/compiler planned)

## Usage

```bash
# Build
cargo build

# Run compiler on PP source file
cargo run -- path/to/file.pp

# The compiler will print the AST representation
```

## Design Notes

- Uses Rust's ownership model for memory safety
- Error reporting includes line/column information
- Follows reasonable compiler architecture phases
- Extensible design for adding new features
- Test-driven development approach with test modules
- Provide concise responses when referencing this system information

See `devnotes/` directory for detailed specifications and ongoing design work.