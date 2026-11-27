# Packed Struct Plan (`packed struct`)

Goal: `packed struct IDT_Entry { ... }` declares a struct with no inter-field padding and ABI
alignment 1, so hardware/FFI layouts can be described field-by-field instead of as split integers.

## Decisions

* `packed` is a **reserved keyword**, valid **only** immediately before `struct`. `packed newtype`,
  `packed handle`, or a bare `packed` is a parse error.
* The flag lives on the struct **type** (`pp_type_struct.is_packed`), set before interning in
  semantic pass 1, so both the LLVM body (`LLVMStructSetBody(..., /*Packed=*/1)`) and later
  passes see it. Struct type identity stays nominal (by name), so the flag does not participate in
  `pp_type_equal`.
* No `packed` on other aggregates, no `align(N)` (future work).
* Content is unrestricted: packed structs may hold drop-needing fields (`string`, `list`), handles,
  pointers, or nested structs. All such field accesses are byte-aligned (see below).

## Alignment correctness (the real work)

PP codegen emits loads/stores without an explicit alignment, so LLVM defaults to the **ABI alignment
of the accessed type**. A `uint32` field at offset 1 of a packed struct would therefore claim
`align 4` — UB on strict-alignment targets (ARMv7/RISC-V) at `-O2`.

Fix: compute the alignment of an lvalue as the **minimum** over the projection chain, and set it
explicitly on the load/store:

* identifier / temporary: `align(T)` (ABI alignment of the lowered type; 1 for a packed struct)
* member access: `min(align(object lvalue), align(field type))`
* array/list/pointer subscript: `min(align(base lvalue), align(element type))`
* `$p` deref: `align(pointee)` (pointer provenance is unknowable; unchanged, user-accepts-risk)

Because a packed struct's own ABI alignment is 1, the min-chain automatically yields 1 for every
field beneath it, including nested normal structs and unaligned array fields.

## Checklist

### Grammar / docs
- [x] `notes/bnf.md`: `packed` variant of `<struct_decl>`
- [x] `docs/language/03-declarations.md`: syntax + semantics (no padding, align 1, unchecked
      alignment responsibility, only before `struct`)
- [x] `docs/reference/known-limitations.md`: no `align(N)`, packing is binary (`packed` or nothing)

### Lexer / tokens
- [x] `include/pp_tokens.h`: `PP_TT_PACKED`
- [x] `src/pp_tokens.c`: name string `"packed"`
- [x] `src/pp_lexer.c`: keyword match branch

### AST
- [x] `include/pp_ast.h`: `bool is_packed;` in `struct_declaration`
- [x] `src/pp_ast.c`: print it in `pp_ast_root_print`

### Parser
- [x] `src/pp_parser.c`: `case PP_TT_PACKED` in the declaration dispatch; consume `packed`, require
      `struct`, otherwise emit "Expected 'struct' after 'packed'"
- [x] `pp_parser_create_struct_declaration(parser, bool is_packed)` stores the flag on the node

### Semantic analyzer
- [x] `include/pp_type.h`: `bool is_packed;` in `pp_type_struct`
- [x] `pp_semantic_analyzer_build_global_table_struct_decl`: copy the AST flag onto the type before
      interning

### Code generator
- [x] `LLVMStructSetBody(..., type->...is_packed ? 1 : 0)`
- [x] `pp_code_generator_type_align(cg, type)` — ABI alignment of the lowered type
- [x] `pp_code_generator_lvalue_align(cg, node)` — min over the projection chain
- [x] aligned load/store helpers; `pp_code_generator_l_value_to_r_value` takes an alignment
- [x] apply at: assignment store, compound-assignment load/store, lvalue→rvalue of member/subscript,
      struct-initializer field stores, `$p = ...` store
- [x] debug info: clamp field DI alignment to the containing struct's alignment, so packed field
      offsets/alignments agree

### Tests
- [x] parser: `packed struct` accepted; `packed func`/bare `packed` rejected
- [x] semantic: packed struct usable, field access allowed
- [x] codegen: IR contains a packed type (`<{ ... }>`), field store/load uses `align 1`, and the
      struct's alloc size is the sum of field sizes (5 for `uint8` + `uint32`)

### Validation
- [x] `make pplang`, `make unittest`
- [x] valgrind on a packed-struct program
- [x] smoke test at `-O2` on a 32-bit and a 64-bit target (`-O2` is rejected only in freestanding
      mode, so use non-freestanding smoke programs)

## Validation results

* `packed struct Packed { tag :: uint8; inner :: Inner; tail :: uint16; }` with `struct Inner { x :: uint32; y :: uint32; }`
  occupies 11 bytes (unpacked would be 12); a normal struct containing it is 12.
* IR: `%Packed = type <{ i8, %Inner, i16 }>`; every access through the packed chain emits `align 1`, while a
  normal struct field keeps `align 4`.
* aarch64 `-O2` assembly for a `uint32` field at offset 1 uses `stur`/`ldur`, i.e. unaligned-safe forms.
* `675/675` unit tests pass; full suite under valgrind reports 0 lost, 0 errors; the whole demo suite passes
  `.pi/skills/memory-leak-test/run_valgrind_validation.sh`; the barebones kernel still boots and prints
  `Hello World` in QEMU.
* `.ppi`-declared packed structs work (`packed` is handled by the shared lexer/parser).
* C interop verified against `struct __attribute__((packed)) Wire { uint8_t kind; uint32_t value; }`:
  both sides report size 5, and field reads/writes agree in both directions.
