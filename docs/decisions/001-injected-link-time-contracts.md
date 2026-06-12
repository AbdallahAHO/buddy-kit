# 001: Libraries receive app policy through injected link-time contracts

**Decision:** When a `lib/*` lego needs app policy (where files land, how creds persist, where entropy comes from), the app supplies it by defining plain functions the lib declares — link-time injection, not function pointers and never an app include.
**Status:** approved
**Date:** 2026-06-10

## Context

The kit's whole premise is that `lib/*` legos are reusable across apps
(buddy today, an e-paper dashboard later). That dies the moment a lib
includes an app header — `lib/faces` knowing about NVS namespaces, or
`lib/file-push` hard-coding LittleFS wipe rules, welds the lego to one
composition. But the policy has to come from somewhere: files must land
somewhere real, species choices must persist.

Two conventional escapes both cost something. Runtime function pointers
(register-a-callback) add indirection, a null-check failure mode, and
setup-order coupling. `std::function` is off the table outright (ADR 002).

## Decision

Libs declare the functions they need (`FileSink`'s operations,
`facesSpeciesLoad/Save`, `wifiCredsLoad/Save/Clear`, `agentStateRandom`)
and call them directly. Each app defines them in its `*_store.cpp` glue.
The dependency rule stays absolute: `lib/*` never includes app headers;
everything points down.

## Consequences

A missing impl is a loud link error at build time, not a null-pointer
panic on the device. Zero runtime overhead — direct calls, no vtables or
pointer tables. New apps start as "implement these five symbols and
compose." The cost: an app can't swap a policy at runtime, and the
contract surface is only discoverable by reading the lib headers —
`docs/architecture.md` carries the contract table for that reason.

## Related

- [002](002-c6-heap-is-the-floor.md) — why `std::function` was never an option.
