---
name: incremental-plan-review
description: Create a migration plan, apply changes incrementally with in-code checklists, and review step-by-step using a review guide with tick boxes.
---

# Incremental Plan → Apply → Review

Use this skill for compiler / type-system migrations that touch multiple files and need auditable progress.

## Overview
1. **Plan** – write a migration markdown with goal, current vs desired behavior, step-by-step plan, risks, acceptance criteria.
2. **Apply incrementally** – implement one step at a time, keep in-code checklists up to date, add tests per step.
3. **Review in steps** – maintain a `*_REVIEW_GUIDE.md` with check boxes per area; tick as you verify.

This is the workflow used for the `newtype` hardening migration.

## 1. Plan

Create `migration-<name>.md` with:

* Goal
* Current behavior
* Desired behavior
* Step-by-step plan with file locations
* Risks
* Acceptance criteria
* Next steps

Mark each step `(ready for review)` when implementation is done and tests pass.

## 2. Apply Incrementally

### In-code checklists
Put a checklist comment block above each changed function, e.g.:

```c
/* pp_semantic_analyzer_check_cast_expression Checklist:
 * [x] Validate inputs via asserts
 * [x] Reject casting to reference type
 * [-] Propagate scope level -> N/A in PP
 */
```

Use `[x]` done, `[ ]` pending, `[-]` N/A with `-> explanation` for items intentionally skipped, matching the style used elsewhere in the file.

### Tests
Add unit tests per step. Prefer semantic tests that cover nominal vs machine-type behavior, error cases, and chain cases.

### Commit hygiene
Commit after each logical step with a clear message. Keep the branch focused.

## 3. Review in Steps

Maintain `migration-<name>_REVIEW_GUIDE.md` with sections:

1. Documentation
2. Pass 1 changes
3. Helpers
4. Feature hardening
5. Type equality / decay
6. Tests
7. Manual smoke tests
8. Clean-up & Safety

Each section has checklist items. Tick them as you verify. Do not commit the review guide ticks until you are ready to close the review.

### Review commands
```bash
git log --oneline <branch>
make bin/unittest
bin/unittest --gtest_filter="*Semantic*"
./bin/pplang /tmp/manual_test.pp
```

### Notebook topic
Set a stable notebook topic for the migration, e.g. `notebook_topic_set --topic newtype-hardening`, so context stays coherent across sessions.

## Tips
* Keep the plan file authoritative; the review guide mirrors it.
* Update both the in-code checklist and the review guide when you change behavior.
* Use `[-]` with `->` explanations for CZ-specific items that are N/A in PP.
* Manual smoke tests should be reproducible via `/tmp` files and `bin/pplang`.
* Run the full test suite after each step to avoid regressions.

## Example artifacts from newtype hardening
* Plan introduced at `fa8acce` – `migration-newtype-hardening.md` added
* Review guide introduced at `79430d4` – `migration-newtype-hardening_REVIEW_GUIDE.md` added
* In-code checklists in `src/pp_semantic_analyzer.c` and `src/pp_type_table.c`
* Tests in `tests/semantictest.cpp`: `NewtypeChainNominalDistinct`, `NewtypeChainCastAllowed`, `CastToConstTargetRejected`, etc.
* Final review ticks committed at `ea0b898`

Follow this pattern for future migrations.
