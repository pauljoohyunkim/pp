# Migration Plan: Prefix Collection Types

## Context
Current PP grammar uses suffix array/list specifiers:

```
<type> ::= <mutability> <base_type> [ <array_specifier> ] [ "&" ]
<array_specifier> ::= "[" [ <expression> ] "]"
```

Collection initializers already use prefix form:
```
<collection_init> ::= "[" <type> [ ":" <expression> ] ";" <elem_list> "]"
```

This creates a split notation for the same concept and makes nesting/reference placement ambiguous.

## Goal
Adopt a uniform prefix notation for collection types:

* `[int32 : 4u]` – array of four int32
* `[int32]`       – list of int32
* `[int32 : 4u]&` – reference to an array of four int32
* `const [int32 : 4u]` – const array

Reference placement is unambiguous and element references are rejectable at type construction.

## Rationale
* Unambiguous nesting: `[[int32 : 3u] : 4u]`
* Consistent with initializers
* Type AST becomes recursive: Array(List(Element, size?))
* Clear error messages for illegal element references

## Breaking Change
All existing code using `T[N]` or `T[]` must be rewritten.

## Steps

### 1. BNF / docs
Update `notes/bnf.md`:
- Remove `<array_specifier>` from `<type>` / `<base_type>`.
- Add:
```
<collection_type> ::= "[" <type> [ ":" <expression> ] "]" | <base_type>
<type>            ::= <mutability> <collection_type> [ "&" ]
```
Keep `<collection_init>` unchanged.

### 2. AST
`include/pp_ast.h`
- Introduce type expression node kinds: `PP_AST_TypeBaseNode`, `PP_AST_TypeArrayNode`, `PP_AST_TypeListNode`.
- For array/list store `element_type_expr` and optional `size_expr`.

Update `src/pp_ast.c` constructors/free.

### 3. Parser
`src/pp_parser.c`
- Refactor `pp_parser_create_type` to parse `<mutability>` then recursive `<collection_type>`.
- On `'['` parse inner type, optional `':' <expression>`, expect `']'`. Return Array/List node.
- Ensure initializer parsing still discriminates by presence of `;` after `]`.

### 4. Semantic Analyzer
`src/pp_semantic_analyzer.c`
- Rewrite `pp_semantic_analyzer_from_type_expr` to walk recursive type expression.
- Resolve element type first.
- Reject element type being a reference when building Array/List.
- Push const inward, then wrap Array/List, then optionally Reference.
- Update `pp_array_size_from_simple_expr` usage for both type sizes and initializer sizes.
- Adjust `pp_type_equal`, `pp_type_decay`, printing.

### 5. Type System
`include/pp_type.h`, `src/pp_type.c`
- Ensure `PPType` onion can represent `Array(element, size)` and `List(element)`.
- No structural changes if already recursive; only construction path changes.

### 6. Tests
- `tests/parsertest.cpp`: update type literals in parser tests.
- `tests/semantictest.cpp`: update variable declarations from `int32[3u]` to `[int32 : 3u]`, lists from `int32[]` to `[int32]`.
- Add parser tests for nested arrays `[[int32 : 3u] : 4u]` and reference forms.
- Add semantic tests for illegal element references: `[int32& : 4u]` should error.

### 7. Validation
- Run `make unittest`.
- Spot check with `bin/pplang` on small PP files using new syntax.

## File Touch List
- notes/bnf.md
- include/pp_ast.h
- src/pp_ast.c
- src/pp_parser.c
- src/pp_semantic_analyzer.c
- include/pp_type.h / src/pp_type.c (if needed)
- tests/parsertest.cpp
- tests/semantictest.cpp

## Rollout Notes
- Commit BNF change first.
- Parser/AST changes before semantic changes to keep compilation green.
- Keep a small compatibility shim or script for migrating examples later.

## Success Criteria
- `func main :: () -> int32 { let a :: [int32 : 3u] = [int32 : 3u; 1,2,3]; return 0; }` compiles with zero semantic errors.
- `[int32& : 4u]` is rejected with “element type cannot be a reference”.
- `[[int32 : 3u] : 4u]&` parses and type checks as reference to array of arrays.

## Appendix – Current Snapshots for Fresh Session

### Current BNF excerpt (notes/bnf.md)
```
<type>                      ::= <mutability> <base_type> [ <array_specifier> ] [ "&" ]
<mutability>                ::= "const" | ""
<base_type>                 ::= "void"
                              | "int32" [ <array_specifier> ]
                              | "uint32" [ <array_specifier> ]
                              | "float" [ <array_specifier> ]
                              | "bool"  [ <array_specifier> ]
                              | <identifier> [ <array_specifier> ]
<array_specifier>           ::= "[" [ <expression> ] "]"
<collection_init>           ::= "[" <type> [ ":" <expression> ] ";" <elem_list> "]"
```

### Relevant AST
`include/pp_ast.h` defines:
```c
typedef struct {
    PP_AST_Node* type_expr;
    PP_AST_Node* size_expr;
    PP_AST_Node** elements;
    unsigned int element_count;
    unsigned int element_capacity;
    PP_AST_Decoration decoration;
} PP_AST_CollectionInitializer;
```

### Parser entry
`src/pp_parser.c` has `static PP_AST_Node* pp_parser_create_collection_initializer(PP_Parser* parser);` called when `'['` is seen in expression context.

### Semantic checker
`src/pp_semantic_analyzer.c`:
- `pp_array_size_from_simple_expr` requires unsigned integer literals for array sizes.
- `pp_semantic_analyzer_check_collection_initializer_expression` at ~2977 builds type via `pp_array_size_from_simple_expr` and per-element checks.

Use these anchors when implementing the recursive type expression parser and type resolver.

## Progress as of 2026-09-18

### Completed
* **1. BNF / docs** – `notes/bnf.md` updated to prefix collection types.
* **2. AST** – TypeBase/TypeArray/TypeList nodes in use; parser emits recursive type expression wrapper.
* **3. Parser** – Prefix collection parsing implemented; all parser tests migrated to `[T : N]` / `[T]` syntax.
* **4. Semantic Analyzer** – Recursive `pp_type_from_type_node`, element reference rejection, per-element checks for collection initializers, const canonicalisation pushed inward for Array/List. `pp_type_decay` descends through structural containers only; stops at nominal Struct/Newtype.
* **5. Type System** – No structural changes required; existing onion model sufficient.
* **6. Tests** – Parser + Semantic suites migrated and extended:
  * `ParserTest.NestedArrayType`, `ParserTest.ArrayReferenceType`
  * `SemanticTest.IllegalElementReferenceArray`, `SemanticTest.NestedArrayTypeValid`, `SemanticTest.CollectionInitializerSuccessCriteria`
  * All 172 Parser + Semantic tests pass.
* **7. Validation** – `make unittest` green.

### Design decisions locked in
* Canonicalisation: `const [T : N]` ≡ `[const T : N]`. Const is pushed inward on construction for Array/List.
* Decay descends only through structural containers (Array/List); stops at nominal types (Struct/Newtype).
* Element reference ban enforced at type construction via `pp_type_validate_collection_element`.

### Remaining polish
* Pretty-printing for recursive collection types
* Full end-to-end initializer-with-values success demo and size-mismatch reporting
* Minor warning clean-up (unused error_cleanup label)


