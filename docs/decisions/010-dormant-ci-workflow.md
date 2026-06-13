# 010: CI is a dormant GitHub Actions workflow: pio build matrix + cloud typecheck

**Decision:** `.github/workflows/ci.yml` ships before any remote exists — a 4-env PlatformIO build matrix plus a `tsc --noEmit` job for `cloud/` — so the first `git push` gets CI with zero setup.
**Status:** approved
**Date:** 2026-06-12

## Context

Three of the four board envs are compile-only — no hardware on the desk
ever exercises them, so nothing catches a change that breaks an S3 build
until someone flashes one. The cloud Worker had no typecheck gate at all
(`wrangler dev` tolerates type errors). The repo has no remote yet, but
"add CI later" historically means never.

## Decision

Commit the workflow dormant. Job `firmware`: matrix over the four
`platformio.ini` envs, `pip install 'platformio==6.*'` (major pinned;
the platform itself is zip-pinned in the ini), caching `~/.platformio`
and `apps/buddy/.pio`. Job `cloud`: pnpm install + `pnpm run typecheck`
(`typescript` added as a devDep — `tsc` wasn't reachable before). A
commented-out `native-tests` stanza marks where the M2 job slots in.

Rejected as solo-repo over-engineering: Makefile/justfile wrappers,
clang-format, pre-commit hooks, and a fake native-tests job before M2
exists.

## Consequences

The compile-only envs get a standing contract: every push rebuilds all
four. The cost of dormancy is rot risk — action versions and the pnpm
major can age before the workflow ever runs; the first real push should
expect one shake-out run. Hardware-verification stays human (the
build/verify loop in `AGENTS.md`) — CI proves compile, never behavior.

## Related

- [009](009-agents-md-adrs-skills.md) — the scaffold this completes.

## Errata (2026-06-13)

CI is no longer dormant — the repo went public and the workflow activated on
the first push. Since this ADR: the firmware matrix grew to **5 builds**
(buddy × 4 envs + glance × 1), the commented `native-tests` stanza is now a
**real job** (`pio test -e native_test`, see ADR 002), and ci.yml gained a
`concurrency` group + docs-only `paths-ignore`. The dormant *posture* this
ADR chose is unchanged; these are its fulfilment.
