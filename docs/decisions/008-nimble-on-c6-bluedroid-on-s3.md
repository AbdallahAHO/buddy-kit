# 008: NimBLE on the C6, Bluedroid on the S3 boards

**Decision:** `lib/transport-ble` carries two BLE stack implementations — NimBLE selected by `-DBUDDY_BLE_NIMBLE` on the C6, Bluedroid by `-DBUDDY_BLE_BLUEDROID` on the S3 envs — chosen per board in `platformio.ini`.
**Status:** approved
**Date:** 2026-06-10

## Context

The BLE transport (NUS bridge to the Claude desktop app) must run on every
board. Bluedroid — the Arduino-core default `BLE` library — is the path of
least resistance on the S3s and what upstream used. But Bluedroid's RAM
appetite is wrong for the C6's 512 KB no-PSRAM budget (ADR 002), where
BLE must coexist with Wi-Fi STA and the captive portal. NimBLE is
substantially lighter and proven on the C6: ~150 KB heap free with both
radios up, enough headroom to also host the BLE HID mouse on the same
GATT server.

## Decision

One lib, two impls behind compile-time flags. Each env picks its stack
with `-DBUDDY_BLE_*`; the public transport surface (`ByteSource` + the
NUS service) is identical, so app code never knows which stack it's on.
One LDF consequence: chain mode (ADR 004) can't see that the Bluedroid TU
is `#ifdef`'d out on the C6 and drags in the core `BLE` lib anyway — the
C6 env carries `lib_ignore = BLE`.

## Consequences

The C6 fits its coexistence budget (`docs/connectivity.md`); S3 boards
keep the battle-tested upstream stack. The cost is two impls to maintain
and a per-env flag + `lib_ignore` ritual when adding boards
(`docs/extending.md`). If NimBLE proves out on the S3s, collapsing to one
stack is a future simplification.

## Related

- [002](002-c6-heap-is-the-floor.md) — the memory floor that forced NimBLE.
- [004](004-ldf-stays-on-chain-mode.md) — why the `lib_ignore` exists.
