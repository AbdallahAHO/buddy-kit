---
name: adr
description: Write an architecture decision record in docs/decisions/ — when a change involves a material decision (new dependency, layer contract, memory/partition layout, protocol shape, infra choice), or when superseding an approved ADR. Use before committing any such change.
---

# Write an ADR

Convention: `docs/decisions/README.md` (paths are from the repo root).
ADRs hold the *why*; the matching `docs/*.md` holds the *how*.

## Does this change need one?

Yes, if it decides something a future session would otherwise have to
reverse-engineer: a new dependency, a layer contract (ADR 001), a memory
rule (002), a partition/flash layout (005, 006), a protocol shape (007),
a stack/infra choice (004, 008), or a convention (009). Routine work
inside an existing decision (new screen, new command, new face) needs a
doc update, not an ADR.

## Steps

1. Next number: `ls docs/decisions/ | grep -o '^[0-9]*' | sort -n | tail -1`
   + 1, zero-padded to NNN.
2. Copy `docs/decisions/template.md` → `NNN-<slug>.md`.
3. Title = the decision in active voice. One-line `**Decision:**` summary
   that reads on its own. Spine: Context → Decision → Consequences.
   ≤40 lines target, 120 hard cap — push mechanics into the matching doc
   and link to it from Consequences.
4. Status `approved`, date today (solo repo: approved at commit time).
5. Add the index line at the bottom of `docs/decisions/README.md` —
   **same commit**.
6. If a doc carries the *how*, add a 1-line `Rationale: [ADR NNN](…)`
   cross-ref there (see hardware.md / architecture.md for the pattern).

## Immutability

Never edit an approved ADR. To change course: write a successor ADR, and
flip the old one's `**Status:**` to `superseded by NNN` (that status flip
is the only permitted edit). The ADR lands in the same commit as the code
it justifies.
