# 007: Devices poll the hub (/poll + /push) instead of holding Durable Object connections

**Decision:** Device ↔ cloud transport is a 1 Hz poll loop — `GET /poll` drains queued commands, `POST /push` uploads device lines — not WebSockets or any held connection.
**Status:** approved
**Date:** 2026-06-11

## Context

The M3 fleet hub is a Cloudflare Worker with a `DeviceHub` Durable Object
per device. Durable Objects support WebSockets, which would give instant
command delivery — but the device side is an ESP32-C6 with ~109 KB free
heap alongside Wi-Fi + BLE, no TLS wired yet, and a hard rule that the
loop thread never blocks. A held TLS WebSocket costs heap permanently,
needs reconnect machinery, and couples device liveness to connection
state.

The device already had `transport-http`: a FreeRTOS background task
polling a *local* hub and feeding the same LineFramer as USB/BLE.

## Decision

Keep the poll contract and make the Worker speak it. Every second, while
Wi-Fi is ONLINE and a hub URL is set: drain the TX ring → `POST /push`,
then `GET /poll` → fill the RX ring. The Durable Object is a per-device
command *queue* (`/enqueue` from admin, `/drain` on poll), not a session.
Presence is a free side effect: the Worker upserts the registry from each
poll's identity headers.

## Consequences

The cloud was purely additive — the device code that talked to a desktop
hub talks to the Worker unchanged, and commands, acks, snapshots and OTA
triggers all ride the same byte pipe. Command latency is bounded by the
poll interval (~1 s), which is fine for a desk pet and for fleet OTA.
Polling burns a request/second/device — acceptable at hobby-fleet scale.
Contract details: `docs/protocol.md`, `docs/cloud.md`, `docs/connectivity.md`.
