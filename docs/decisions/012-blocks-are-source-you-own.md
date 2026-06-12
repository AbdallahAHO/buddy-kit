# 012: Blocks are app source you copy in, not a library you depend on

**Decision:** Reusable compositions (a screen + its menu row, ladder rungs, store bit) ship as *blocks*: verbatim app source plus the whole lines it owns in the app's central tables, applied and reverted by `tools/blocks.py` — never as a lib/ lego, never behind a runtime registry.
**Status:** approved
**Date:** 2026-06-12

## Context

Legos (lib/*) cover behavior, but a feature like the Wi-Fi pairing screen
is a *composition*: 48 lines of screen code plus one surface-chain line,
three focus-ladder rungs, a settings row, a store bit — smeared across
five app files. That smear made compositions non-reusable: a new app
would re-derive them from docs/extending.md by hand. The conventional
fixes both add a layer: auto-registration magic (linker sections, macro
tables) hides control flow and breaks rule 4's "one greppable place
each"; making the composition a lib couples it to app policy it must not
know (the dependency rule).

shadcn solved the same problem in React by not solving it as a library:
components are source you copy and own, and the CLI edits *your* files.

## Decision

A block is `blocks/<name>/`: the source files verbatim plus a
`block.json` of whole-line anchored patches into the central tables.
`add` copies and inserts; `rm` is the exact inverse; `export` refreshes
the block from the app after in-place iteration. The enabling idiom,
applied once as a seam refactor: every hook point is a single removable
line — ladder rungs end in `else`, multi-line conditions keep one term
per line, overlay row counts are computed. Rule 4 survives intact: the
tool writes into the same central tables a human would.

## Consequences

Proven by inversion on the first carve (`wifi-pairing`, 2 files +
16 patches): `rm` → zero references, build green; `add` → byte-identical
tree. After `add`, the code is indistinguishable from hand-written —
agents iterate on plain C++, no DSL. The costs: patches are exact-line
matches, so refactoring a seam means re-exporting affected blocks
(`verify` catches drift), and block.json is hand-maintained for now.
Workflow lives in `docs/extending.md` (Blocks).
