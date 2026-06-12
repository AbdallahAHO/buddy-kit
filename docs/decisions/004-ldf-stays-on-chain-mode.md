# 004: LDF stays on default chain mode

**Decision:** PlatformIO's Library Dependency Finder runs in default `chain` mode for every env; `chain+`/`deep+` are banned, and the chain-mode side effects are handled explicitly (`lib_ignore = BLE` on the C6).
**Status:** approved
**Date:** 2026-06-10

## Context

The monorepo's private-lib packaging (`lib/<name>/library.json` +
`lib_extra_dirs`) leans on the LDF to wire app → lego → registry deps.
The "smarter" LDF modes that evaluate preprocessor conditionals sound
like the right tool for a repo full of `#ifdef`'d transports — but on the
pioarduino platform, `chain+` and `deep+` break FS.h/LittleFS resolution:
the build stops finding the filesystem library entirely. Field-verified,
cost real time.

## Decision

Every env stays on the default `chain` mode — the setting is simply never
overridden in `platformio.ini`. The known chain-mode false positive is
handled at the env level instead: chain mode can't see that the Bluedroid
impl in `transport-ble` is `#ifdef`'d out on the C6, so it drags in the
core `BLE` library; the C6 env carries `lib_ignore = BLE` to cut it.

## Consequences

Builds resolve LittleFS reliably on all four envs. The price is that every
flag-selected impl pair (like the two BLE stacks) may need a matching
`lib_ignore` in envs that exclude one side — a per-env line item, documented
in `docs/architecture.md` (Packaging) and the add-a-board recipe in
`docs/extending.md`.

## When to revisit

If the repo moves off pioarduino or the platform fixes chain+ FS
resolution, the tradeoff changes — re-test before switching modes.
