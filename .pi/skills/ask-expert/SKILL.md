---
name: ask-expert
description: When stuck on LLVM/Clang API details, write a *_PROBLEM.md file and request expert review.
---

# Ask-an-Expert Skill

Use this when you hit an API ambiguity, build error, or design decision that requires authoritative external knowledge.

## When to use
* Unknown type names in LLVM C API
* Signature mismatches for DIBuilder / debug info functions
* Unclear LLVM module flag semantics
* Build errors you cannot resolve with local docs

## Process
1. Write a problem file in project root with suffix `*_PROBLEM.md`
   Example: `DI_PROBLEM.md`, `LLVM_FLAG_PROBLEM.md`
2. Document:
   * Problem statement and error messages
   * Context: version, file, code snippet
   * Investigation performed
   * Hypothesis
   * Options considered
3. Request expert analysis. The expert will read the problem file and provide definitive guidance.
4. Apply fix, remove problem file once resolved, and update checklist.

## Template
```markdown
# Title

## Problem
...

## Context
...

## Investigation
...

## Hypothesis
...

## Options
1. ...
2. ...

## Next steps for deeper analysis
...
```

## Notes
* Keep problem files short and factual
* Delete them after resolution to avoid stale docs
* Link problem files in commit messages if relevant
```

