# Operator Support Progress (checklist)

## Shared prerequisite (done-ish)
- [x] Const-ref parameters can bind rvalues via materialized temporaries (semantic analyzer)
- [x] Codegen materializes rvalues into temporaries for const references
- [x] Loop stack fix via entry-block allocas
- [x] Updated `notes/bnf.md` for operator declarations
- [x] Add temporary-lifetime drops at full-expression boundaries for owning temporaries (function-call const-ref temps)
- [x] Add focused unit tests for rvalue-to-const-ref binding (`s + "...` and custom operators)

## 1. Language / BNF design
- [x] Decide operator declaration syntax (`operator` vs `func operator` vs `lambda operator`)
- [x] Update `notes/bnf.md` for operator declarations
- [x] Define operator signature rules: params `const T&`, non-reference return
- [x] Define operator resolution key (decayed operand types only; no pass-mode ambiguity)

## 2. Lexer / parser
- [x] Add `operator` keyword to lexer
- [x] Parse operator declarations as global declarations
- [x] Parse operator infix usage as calls (or special operator expressions)
- [x] Build AST nodes for operator declarations (reuse function nodes)

## 3. Semantic analyzer
- [x] Accept operator declaration form
- [x] Enforce const-ref-only parameters for operators
- [x] Enforce non-reference operator return type
- [x] Reject by-value / mutable-ref operator overloads that differ only by pass mode
- [x] Reject duplicate operator resolutions for same operand types

## 4. Code generator
- [x] Generate operator declaration as function/lambda
- [x] Prototype operator name mangling via interned type pointers
- [x] Generate operator calls correctly
- [x] Handle reference binding and temporaries (reuse const-ref machinery)

## 5. Standard library / examples
- [ ] Migrate or add `string`/`list` operator examples
- [x] Update README / docs
- [x] Add regression tests end to end (basic smoke)

### Notes / open decisions
- Operator declaration syntax:
  - Binary: `operator + :: (x :: const Vector&, y :: const Vector&) -> Vector`
  - Unary: `operator ! :: (x :: const Vector&) -> Vector`
- Ambiguity notes:
  - Same symbol may be prefix unary or infix binary; use parser position.
  - Arity distinguishes unary vs binary during resolution.
  - Duplicate definitions/extern conflicts must reject by arity and decayed operand types.
  - Only one non-reference return per operator declaration.
  - Extern/export via normal `extern` mechanism like functions.
  - Binary: `operator + :: (x :: const Vector&, y :: const Vector&) -> Vector`
  - Unary: `operator ! :: (x :: const Vector&) -> Vector`
- Need guard reserved builtin operators / duplicate resolutions in semantic analyzer.
- Need define unary and binary discovery and dispatch consistently.

## 6. Parser-side operator resolution
- [x] Confirm operator lookup does not clash with functions ( overload dispatch implemented)
- [ ] Document precedence, associativity, and overload behavior
- Builtin operator shadowing now rejected for binary and unary core types.
