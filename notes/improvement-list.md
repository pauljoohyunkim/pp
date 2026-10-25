# Improvements & Technical Debt

## List handle caching - Potential Improvement
**Current approach:** `pp_backend_lower_type` for `PP_TK_List` uses name lookup via `LLVMGetTypeByName2` to find/reuse the shared `%pp.list` handle. No in-memory cache in `PP_CodeGenerator`.

**Potential improvement:** Cache the handle type in `cg->list_handle_type` as a singleton.
**Why:** Avoids fragile check-then-act race on global LLVM name lookup, guarantees exactly one LLVM struct definition, prevents name collision with user structs, makes lowering O(1).

**Status:** Not implemented; name lookup is sufficient for single-threaded build. Documented as optional robustness improvement.

## Array size truncation
**Issue:** `LLVMArrayType` takes `unsigned` size, current code casts `size` to `unsigned`. 
**Improvement:** Use `LLVMArrayType2` with 64-bit size or assert size fits in `unsigned`. 
**Status:** Open.

## Struct registration consistency
**Issue:** Struct lowering currently registers via opaque struct creation in `lower_type`. Ensure struct is registered in type map before returning to avoid duplicate creation when `Array(N, Struct)` is lowered first.
**Status:** Needs verification.

## Post-order comment accuracy
**Issue:** Comment in `pp_backend_lower_type` claims element type is registered during recursion, but primitives are not registered. 
**Improvement:** Reword comment to reflect that pass-1 loop owns registration.
**Status:** Open.

## Double-push prevention
**Fixed:** `lower_type` no longer pushes types to map; pass-1 loop owns pushes.
