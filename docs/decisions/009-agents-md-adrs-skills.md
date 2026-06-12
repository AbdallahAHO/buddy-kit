# 009: Agent rules live in vendor-neutral AGENTS.md; decisions live in ADRs; skills under .agents/skills/

**Decision:** Repo-wide agent rules move to a top-level `AGENTS.md` (`CLAUDE.md` becomes the one-line `@AGENTS.md` import shim); decision rationale moves to `docs/decisions/`; reusable workflows live as skills under `.agents/skills/` with `.claude/skills/` symlinks and a `skills-lock.json` for vendored externals.
**Status:** approved
**Date:** 2026-06-12

## Context

The repo's rules lived in `CLAUDE.md`, which only Claude Code reads —
Codex and other agents got nothing, though almost none of the content was
Claude-specific. Decision rationale was compressed into gotcha bullets
(the *why* behind the partition table or the BLE stack split took
archaeology to recover). And the field-verified workflows — probe the
serial port without DTR-resetting your own session, release over fleet
OTA — existed only as session memory.

## Decision

Three homes, by content type. **Rules** → `AGENTS.md` (the
[agents.md](https://agents.md/) convention; Codex/Cursor read it natively,
Claude Code via the `@AGENTS.md` import). **Why** → ADRs (001–008
backfilled from git history). **Workflows** → skills, canonical at
`.agents/skills/<name>/SKILL.md`, exposed to Claude Code via relative
symlinks in `.claude/skills/`; vendored external skills are pinned in
`skills-lock.json` with a sha256.

## Consequences

Any agent finds the same rules, and the gotcha bullets now cite their
ADRs. Skill review rejected more than it kept: an `add-a-lego` skill
(docs/extending.md *is* that skill — wrapping it is a drift machine);
vendoring `esp32-serial-commands` (teaches `echo > $PORT`, which
DTR-resets this hardware); `esp32-firmware-engineer` wholesale
(ESP-IDF-shaped: idf.py/menuconfig — only its panic-triage reference was
salvaged, adapted); and all skills.sh finds (generic manuals or
SaaS-coupled, none survived pruning) — so `skills-lock.json` starts empty.
The cost is three surfaces to keep coherent; the docs contract row for
ADRs is the guard.
