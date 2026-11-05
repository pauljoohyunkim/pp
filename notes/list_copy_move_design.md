# List Copy / Move Design – Option A

## Goals
Enable value semantics for `List` and structs containing lists with safe deep copy and drop.

## Key decisions
* Per-type `copy_T` / `drop_T` inlined via `pp_code_generator_emit_copy` / `emit_drop`. Caching deferred.
* Drop stack handles any owning type, not just List.
* Deep copy for List implemented via `malloc + memcpy` for trivial element types.
* Semantic bans for init/assign/by-value params lifted.
* Move-only temporary bans removed.

## Type predicates
* `pp_type_needs_drop` – true for List, Struct with owning fields, Array of owning elements, Newtype wrappers.
* `pp_type_is_trivially_copyable` – primitives, arrays of trivial, structs with only trivial fields and no owning fields.
* `pp_type_is_copyable` – trivially copyable OR owning type whose elements/fields are copyable. `List(T)` is copyable iff `T` is copyable; recursive.

## Semantic checks
* Variable init from lvalue → require copyable if type needs drop.
* Assignment from lvalue → require copyable if type needs drop.
* Errors emitted with clear messages for move-only / non-copyable elements.

## Codegen
* Variable alloca for owning types initialized to zero/empty.
* Init from lvalue → `emit_copy`.
* Assignment → `emit_copy` then `emit_drop` old value, self-assignment safe.
* By-value param → caller `emit_copy` if arg is lvalue and type needs drop; callee pushes param onto drop stack.
* `List` copy is element-aware: fast `malloc+memcpy` for trivial elements, loop with recursive `emit_copy` for non-trivial elements.
* `List` drop is element-aware: loop over `len` and recursively `emit_drop` per element when `pp_type_needs_drop(elem)` before `free(data)`.

## Limitations
* Global struct field default initializers must be constexpr; lists are not constexpr.
* `pop`/`resize`/`append` mechanics are out of scope for this project phase.

## Future
* Per-type caching for `copy_T`/`drop_T`.
* Library / later phase support for `pop`/`resize`/`append`.
