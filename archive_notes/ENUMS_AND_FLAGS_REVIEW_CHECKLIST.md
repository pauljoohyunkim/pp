# Enums & Flags — Review Checklist

Branch: `feature/enum-flag` vs `dev`
Commits: `55d36f4` (lexer/parser), `1a5aca8` (semantic), `87376a2` (codegen), `8dcb1ce` (cast signedness fix),
`f422d7e` (docs). Diff: 19 tracked files, +1899/−18.

```bash
git diff --stat dev...HEAD
git diff dev...HEAD -- src/pp_parser.c            # layer by layer is easier than the whole diff
git diff dev...HEAD -- src/pp_semantic_analyzer.c # the big one (+667)
git diff dev...HEAD -- src/pp_code_generator.c
```

## 0. Hygiene before you start

- [x] `git status`: the standalone `demo/07_enums_and_flags` was **removed** on purpose (enums are now
      demonstrated by `demo/05_bf`, which dispatches on an `enum BFCommand` declared in `brainfuck.ppi`).
      The two checklist files were moved to `archive_notes/` (`D HEX_CHAR_LITERAL_CHECKLIST.md`,
      `MD ENUMS_AND_FLAGS_CHECKLIST.md` are unstaged) — decide whether those moves belong on this branch.
- [x] Confirm you do not want `ENUMS_AND_FLAGS_CHECKLIST.md` history preserved rather than deleted+re-added.
- [x] Branch has 3 commits titled `WIP:` plus later fixups. If the history matters, ask for a squash/reword before merge.
- [x] `notes/bnf.md` and `docs/language/10-enums-and-flags.md` describe the same rules — read them against each
      other; drift here is cheap to fix now and expensive later.

## 1. Design fidelity (the decisions that must not drift)

- [ ] `enum` and `flag` are **fully separate**: separate AST nodes, separate inline union structs, separate
      parser entry points, separate semantic builders. No shared `is_flag` parameter anywhere.
      (Check `include/pp_ast.h`, `src/pp_parser.c:2115` / `:2235`, `src/pp_semantic_analyzer.c:2455` / `:2588`.)
- [ ] Constants are `Type::Member`, never `Type.Member`; `.` stays struct-only.
- [ ] No `has` builtin reintroduced anywhere.
- [ ] `integer -> enum` is banned **unconditionally**; `integer -> flag` allowed; `enum/flag -> integer` allowed;
      enum↔flag (different nominal) rejected.
- [ ] `none` is reserved only inside `flag` bodies, and is *not* a keyword (still usable as an identifier).

## 2. Type layer (`include/pp_type.h`, `src/pp_type.c`)

- [ ] Representation is `PP_TK_Newtype` + `enum_kind` (+ `members`, `member_count`) — deliberately not new
      `PP_TK_*` kinds. Check no `switch` on kinds silently mishandles an enum/flag incident to that choice
      (e.g. `pp_type_is_numerical`, `pp_type_equal`, `pp_type_decay`).
- [ ] `pp_type_free` frees `members` for the newtype case; no double free when the candidate is rejected by
      `pp_global_type_table_intern`.
- [ ] Helpers `pp_type_is_enum/_flag/_enum_or_flag`, `pp_type_find_enum_member`, `pp_type_kind_is_integer` have
      the contracts you expect (NULL-safe, no unwrapping surprises).

## 3. Parser (`src/pp_parser.c`)

- [ ] The two declaration parsers are ~115 lines each **by design**. Diff them against each other and confirm the
      only differences are the keyword, the node type and the field names. Any other divergence is a bug.
- [ ] Body grammar: comma separated, optional trailing comma, empty body allowed, `}` closes, **no semicolon**.
- [ ] Both keywords added to the global dispatch and to `pp_parser_is_sync_token` (error recovery).
- [ ] `::` postfix: requires a bare identifier immediately to the left; check
      - `a.b::C` is rejected with the intended message,
      - `(T)::M` works (parenthesised identifier),
      - `T::M` in the presence of a *function call* `f(T)::M` does not silently do the wrong thing,
      - the consumed left node is freed exactly once.
- [ ] `~` added to unary parsing and to the precedence table consistently.

## 4. AST (`include/pp_ast.h`, `src/pp_ast.c`)

- [ ] `pp_ast_free` covers both declaration nodes (each `member_value_exprs[i]`, then both arrays) and
      `PP_AST_QualifiedConstantNode` (decoration). Confirm no leak path when parsing aborts mid-declaration.
- [ ] `pp_ast_get_decoration` and the print/`to_string` paths handle the three new node types.
- [ ] Decoration fields `is_enum_constant` / `enum_constant_value` are initialised for **all** decorations
      (check `pp_ast_decoration_create`), so codegen cannot read garbage.

## 5. Semantic analyzer (`src/pp_semantic_analyzer.c`) — the main review target

Pass 1 typology (`:2455` enum, `:2588` flag):
- [ ] Underlying type resolved through `pp_type_from_type_node`, const/reference rejected, integer primitive
      required, **flag unsigned** required.
- [ ] Duplicate type name checked against both the alias and the named tables (same rule as `newtype`/`handle`).
- [ ] Duplicate member names: O(n²) scan is fine; make sure the error names the type and the member.
- [ ] Values (`pp_semantic_eval_int_constant`, `:2259`):
  - [ ] literals via `strtoull(..., 0)` (hex works), suffix stripped, whole string must be consumed;
  - [ ] identifiers resolve **only to earlier members** of the same body (no leakage to outer scopes);
  - [ ] character literals (`'+'`, `'\x0a'`, `'\n'`) are accepted as uint8 constants — new code added for the
        bf demo; check the escapes and that a malformed escape is rejected;
  - [ ] unary `-` and `~`; binary `+ - * / % << >> | & ^`; division/modulo by zero and shift ≥ 64 error;
  - [ ] 64-bit wraparound is *silent* — is that acceptable, or should `1 << 64`, `A * 2` overflow, etc. error?
- [ ] Auto numbering: enum `0, +1` (UINT64_MAX guard); flag `1`, then next power of two **strictly greater**
      (guard near `UINT64_MAX`). Verify `Sync = 1 << 40` followed by an auto member yields `1 << 41`.
- [ ] Fit check (`pp_semantic_int_value_fits`, `:2216`): width-based on the underlying kind; confirm 128-bit kinds
      really accept everything, and that `int8`/`uint8` both cap at `UINT8_MAX` (no negative values by design).
- [ ] Strictly increasing check is unsigned (`value <= previous_value`). Confirm that is what you want for
      signed underlying types too.
- [ ] Flag extras: explicit `0` rejected; value must be a single bit **or** the OR of earlier members that
      intersect it (`pp_semantic_flag_value_is_combination`, `:2240`). Try to construct a case that passes but
      should not (e.g. `A=1, B=2, C=1|2|4`).
- [ ] Error cleanup: `free(members)` + `pp_type_free(nt)`, and `members` nulled after the ownership transfer.

Expression typing:
- [ ] Qualified constant (`:4550`): rejects non-enum/flag types with a clear message, resolves `Flag::none` as 0,
      marks the decoration `RVALUE`/constexpr with `is_enum_constant` + value. Is a **typedef alias** of an enum
      accepted, and does the decoration carry the true enum type (not the alias)?
- [ ] `pp_array_size_from_simple_expr` duplicates the lookup logic via
      `pp_semantic_lookup_qualified_constant_value` — confirm the two paths cannot disagree.
- [ ] Binary (`check_binary_expression`): enum/flag operands require `pp_type_equal`; enum allows `==`/`!=` only;
      flag allows `== != | & ^`, result type reuses the lhs canonical pointer. Confirm both sides being flags of
      the same type but *different* pointers still works (nominal equality, pointer reuse safety).
- [ ] `~` on integer primitives and flags only; Bool is **not** an integer primitive here — confirm `~true` and
      `true as int32` are rejected (both are today).
- [ ] Compound `&= |= ^=` only when both sides are the same flag type; a flag `+=` must still error.
- [ ] Cast (`check_cast_expression`): enum→int, flag→int, int→flag OK; int→enum error; enum↔flag error.
      Check the *order* of this branch relative to callable/reference/pointer handling — could a pointer-to-enum
      or a reference-to-flag sneak past?

## 6. Code generator (`src/pp_code_generator.c`)

- [ ] Declaration nodes are no-ops in the global emit switch (nothing to emit, nothing to skip silently).
- [ ] `pp_code_generator_generate_rvalue_qualified_constant` (`:3878`): lowers the nominal type to the
      underlying integer type and emits `LLVMConstInt(value, false)`; asserts the decoration invariant.
      Is an lvalue path reachable? (It must not be — semantics marks constants as rvalues.)
- [ ] `case PP_TT_TILDE` (`:3549`) → `LLVMBuildNot`; confirm flags ride the same path and that the operand type is
      always an integer type here.
- [ ] Binary/compound flag ops rely on the generic integer lowering — confirm no signed-vs-unsigned division or
      shift logic is accidentally applied to flag types (`pp_type_is_unsigned_integer` does **not** unwrap
      newtypes: for a flag it returns false).
- [ ] Debug info (newtype enum case): `DW_TAG_enumeration_type` with `LLVMDIBuilderCreateEnumerator` per member,
      `is_unsigned` true for flags and for unsigned underlying enums, alignment now **the underlying size in
      bits** (was hardcoded 64). Check a `uint64` enum member of `1 << 63`: the C API takes `int64_t`, so the
      emitted `DW_AT_const_value` is negative but flagged unsigned — verify dwarfdump output is sane.
- [ ] `pp_code_generator_build_int_cast` (`:3585`): trunc / zext / sext by width, signedness from the unwrapped
      source type; used in the generic int→int branch and numeric→pointer normalisation. It returns the value
      unchanged when widths match — confirm that is still correct for an enum→underlying cast (bit pattern
      preserved, new nominal type).
- [ ] `pp_type_is_unsigned_integer` non-unwrapping is a latent trap for future code touching flags; note it.

## 7. Tests

Accept-path coverage (14 semantic + 10 codegen + parser/lexer tests):
- [ ] Read the *reject* tests and ask whether any of them would still pass for the wrong reason (e.g. failing on a
      later error rather than the intended rule). The semantic tests assert on error counts, which is coarse.
- [ ] Gaps worth a test if you care:
  - [ ] enum with an empty body / flag with an empty body;
  - [ ] flag with more than 32 members, or an auto member after `1 << 40` (power-of-two jump);
  - [ ] `enum :: uint64 { High = 1 << 63 }` end-to-end (value emission + DWARF);
  - [ ] `enum :: int32` explicit value `2147483647` (fit boundary) and `2147483648` (rejected);
  - [ ] qualified constant through a typedef alias;
  - [ ] `~Flag::X` vs `~X` on an integer in one test;
  - [ ] `arr[e]` indexing with `e as uint32` (documented requirement, not tested).

## 8. Cross-cutting questions to settle

- [ ] Should `strtoull`-based evaluation reject literals that were lexed as `int64`-typed (e.g. `18446744073709551615`
      vs a suffixed literal)? The evaluator currently ignores the literal's declared type and re-parses the
      lexeme.
- [ ] `enum` over a signed type with `-1` is rejected as "out of range" — is the diagnostic good enough, given the
      documented rule is "values are unsigned"?
- [ ] Does `#array` / array-size usage of an enum constant behave when the constant exceeds `SIZE_MAX`? (Rejected
      — confirm the message.)
- [ ] Anything to say about `switch`-like exhaustiveness? Out of scope by decision — confirm no stale TODO claims
      otherwise.

## Already verified during development (you do not need to re-derive these)

- `make -j` + `./bin/unittest` → 525 tests pass.
- Full valgrind validation (unit tests + all 7 demos, plain and `-g`) → ALL CLEAN.
- `-g` build inspected with `llvm-dwarfdump`: `DW_TAG_enumeration_type` with correct byte sizes and enumerator
  names/values for `enum :: uint8` and `flag :: uint32`.
- Unsigned widening regression: `enum :: uint8 { A = 200 }` cast to `uint32` yields 200 (was 4294967240 before
  `build_int_cast`).
- `enum Neg :: int32 { Z = -1 }` → rejected; empty body → accepted; `flag Wide :: uint64 { A = 1 << 40, B }` →
  accepted; `true as int32` and `~true` → rejected.

## Suggested review order

1. Docs/BNF first (5 min) — you are reviewing against stated intent.
2. Parser + AST (15 min) — mechanical, catches structure mistakes early.
3. Semantic analyzer (30 min — the bulk of the risk: value rules and casts).
4. Codegen (15 min) — small, mechanical; focus on DI and the int-cast helper.
5. Tests (10 min) — check the reject tests are failing for the right reason.
