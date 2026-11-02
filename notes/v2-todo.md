# PP V1 → V2 TODO

## High-level goals after V1 merge
* Multi-file linking / module support
* Function forward declaration
* Strings implementation
* List operations as operators
* User-defined operators
* LLVM optimization passes
* Hardening & DX improvements

## Linking / multi-file support
- [x] Define module / import syntax in grammar and notes/bnf.md
- [x] Lexer support for import/include directives
- [x] Parser: import statements, module AST nodes
- [x] Symbol table: cross-module resolution, separate `pp_symbol_table` per module with global linker map
- [x] Semantic analyzer: resolve symbols across modules, detect duplicate definitions
- [x] Code generator: emit object files per module, link via LLVM linker
- [x] Linker offload: implement `src/pp_linker_offload.c` multi-file linking API
- [x] Driver `src/pplang.c`: CLI flags for multiple input files, output .o/.exe
- [ ] Tests: `tests/linkertest.cpp` with two-module integration
- [ ] Docs: docs/15-modules-linking.md

### Module interface design decision
**Open decision: textual #include preprocessor vs compiler-generated interface files**
- Preprocessor option: unified source buffer + source location mapping before lexer. Pros: familiar C-style includes. Cons: no type checking across files, requires line map for diagnostics, duplicates C header problems.
- Interface file option: compiler emits `.ppi` with exported declarations, imported via semantic analysis, with mangling as link-time backstop. Pros: compile-time type/purity checking, good diagnostics, aligns with PP safety goals. Cons: moderate implementation cost.
- Current leaning: prefer interface file approach for PP-to-PP, keep `extern "C"` unmangled escape hatch for foreign code. Textual `#include` deferred unless needed for non-code assets.
- Action: create design doc `notes/module_interface_design.md` comparing both approaches, choose syntax, decide on mangling scheme, and update BNF/notes accordingly.

## Function forward declaration
- [x] Grammar: allow `fn foo(...);` prototype at global scope
- [x] Parser: prototype AST node, differentiate declaration vs definition
- [x] Semantic analyzer: register forward-declared symbols, defer body checking
- [x] Type checker: allow calls to forward-declared functions
- [x] Code generator: emit external declarations, resolve at link time
- [x] Tests: forward declaration before use, circular declarations error cases
- [x] Notes update: notes/project_overview.md, notes/type_system.md

## Strings
Open decision: array vs list
- [ ] Design decision doc: notes/string_design.md
  - Array-backed immutable string with length prefix vs list of chars
  - Interoperability with C strings and LLVM
- [ ] Lexer: string literal tokenization, escape sequences
- [ ] Type system: `String` type definition, const correctness
- [ ] Parser / AST: string literal node, string concatenation
- [ ] Semantic analyzer: string operations type checking
- [ ] Code generator: LLVM string constants, `malloc` + copy, string functions
- [ ] Ownership model update for strings
- [ ] Tests: string literals, concat, interning

## List operations as operators, no builtins
Design principle: stay away from builtin functions, prefer operators
- [ ] Design decision doc: notes/list_operator_design.md
  - List type semantics – dynamic array vs linked list, ownership model
  - Operator set instead of builtin functions
- [x] Global list initialization policy: allow empty list as constexpr, reject non-empty list at global scope
  - Semantic analyzer marks empty list constexpr, explicit error for non-empty global list
  - Code generator emits constant zeroed handle for empty global list
- [ ] Length operator: `#expr`
  - For arrays: compile-time constant where possible
  - For lists: runtime length
  - Lexer: `#` as unary operator token
  - Parser: unary `#` expression
  - Semantic analyzer: type check, array length constant folding
  - Code generator: array -> constant / metadata, list -> runtime call
- [ ] Indexing / access operator: `list[i]` already exists, ensure const correctness
- [ ] Append operator: `list << value` or `list += value`
  - Choose syntax, document in notes
  - Parser: binary append op
  - Semantic analyzer: type check
  - Code generator: list append implementation
- [ ] Pop / remove operators:
  - `list >>` for pop back? Or `list--` ?
  - Decision needed: postfix operators for pop, or `list - value` ?
  - Document proposed syntax
- [ ] Insert / push operators:
  - `list << value` for append, `value >> list` for prepend?
  - Need concrete operator choices
- [ ] Equality / comparison operators already exist
- [ ] Bitshift operators as binary operators
  - `<<` and `>>` for bitshift
  - Conflict with proposed list append/pop? Need disambiguation
  - Decide precedence and overload resolution
  - Parser: extend binary operator table
  - Semantic analyzer: integer types only
  - Code generator: LLVM shl/shr
- [ ] Ownership & memory: allocation strategy, growth factor, deallocation for lists
- [ ] Tests: `tests/listtest.cpp` covering `#`, append, pop, indexing
- [ ] Docs: docs/08-semantics-arrays-lists.md update with operator table

## User-defined operators
Goal: define operators like functions/lambdas with signature considerations
- [ ] Design doc: notes/operator_overloading_design.md
  - Syntax: `op + (a, b) -> ...` or `operator +` declaration
  - Scope: global, type-specific, or generic?
- [ ] Signature design question:
  - Allow via const reference for parameters or by value?
  - Implications for ownership, mutability, performance
  - Decision: default to const reference for non-primitive types, by value for primitives?
- [ ] Grammar: operator definition declaration in BNF
- [ ] Parser: operator definition AST node, operator token as identifier
- [ ] Semantic analyzer:
  - Validate operator signature, arity, precedence
  - Check parameter constness / reference rules
  - Register operator in type-specific operator table
- [ ] Code generator: dispatch user-defined operator to function call
- [ ] Overload resolution:
  - Built-in vs user-defined precedence
  - Ambiguity handling
- [ ] Restrictions:
  - Which operators can be overloaded? All binary/unary or subset?
  - Prevent overloading of `#`, bitshift? Decide policy
- [ ] Tests: user-defined `+`, `<<`, `#` for custom types
- [ ] Docs: docs/13-expressions.md update with operator definition syntax

## General hardening
- [ ] CI pipeline: GitHub Actions for build + unit tests
- [ ] Remove internal Pi skills from mainline or move to `.pi/` ignored in repo
- [ ] Split monolithic V1 PR knowledge: create incremental docs for each subsystem
- [ ] Known limitations update: docs/14-known-limitations.md
- [ ] Error messages polish
- [ ] Memory leak checks integrated in Makefile
- [x] LLVM optimization passes: enable and configure LLVM pass pipeline in code generator, add `-O0/-O1/-O2/-O3` flags to driver, document behavior

## Open questions
- Strings: array vs list? Need decision before implementation.
- List implementation: array-backed vs linked? Performance vs ownership complexity
- Operator syntax for list append/pop vs bitshift `<<`/`>>` conflict
- Length operator `#` precedence with unary ops
- User-defined operators: const reference for params? By value? Mutability rules?
- Forward declaration interaction with lambdas and generics?
- Module system naming: `import` vs `use` vs `link`?
- Module system design: textual `#include` preprocessor with source map vs compiler-generated interface files `.ppi` with mangling? See Module interface design decision above.
