# 001: Libraries receive app policy through injected contracts, never app includes

**Decision:** When a `lib/*` lego needs app policy (where files land, how creds persist, where entropy comes from), the app injects it — as link-time symbols (`facesSpeciesLoad/Save`, `wifiCredsLoad/Save/Clear`, `agentStateRandom`) or as a const struct of plain function pointers handed over once at init (`FileSink`) — never via an app include, never via `std::function`.
**Status:** approved
**Date:** 2026-06-10

## Context

The kit's whole premise is that `lib/*` legos are reusable across apps
(buddy today, an e-paper dashboard later). That dies the moment a lib
includes an app header — `lib/faces` knowing about NVS namespaces, or
`lib/file-push` hard-coding LittleFS wipe rules, welds the lego to one
composition. But the policy has to come from somewhere: files must land
somewhere real, species choices must persist.

`std::function` is off the table outright (ADR 002), and an app include
inverts the dependency arrow. What remains are two heap-free shapes.

## Decision

Single-function policies are **link-time**: the lib declares the function
and calls it directly; the app defines it (`faces_store.cpp`,
`wifi_store.cpp`, `agent_link.cpp`) — six symbols today. A missing impl
is a loud link error; zero indirection. Multi-operation policies where
the calls cluster are a **const struct of function pointers** registered
once at init: `FileSink` (begin/chunk/end/wipe + fit check), defined in
`app_commands.cpp` and passed to `filePushInit()`. Either way, the
dependency rule stays absolute: `lib/*` never includes app headers;
everything points down.

## Consequences

New apps start as "define these symbols, fill these structs, compose."
No vtables, no heap, no setup-order coupling for the link-time shape; the
struct shape carries one init call and a null guard in `file-push`. The
contract surface is only discoverable by reading the lib headers —
`docs/architecture.md` carries the contract table for that reason.

## Related

- [002](002-c6-heap-is-the-floor.md) — why `std::function` was never an option.
