# 014: The notify socket uses IDF's esp_transport_ws and verifies TLS against the built-in CA bundle

**Decision:** `lib/transport-ws` drives a held WebSocket with ESP-IDF's `tcp_transport` (`esp_transport_ws` over `esp_transport_tcp`/`esp_transport_ssl`) from its own FreeRTOS task, and `wss://` verifies the server with `esp_crt_bundle_attach`; there is no unverified mode.
**Status:** approved
**Date:** 2026-11-07

## Context

notify (docs/notify-protocol.md) pushes asks to a device through one
Durable Object per destination, over a WebSocket the device holds open.
That contract is fixed by the server side: Bearer auth on the upgrade,
text frames, text `ping`/`pong` heartbeats, close code 4001 for a revoked
token. ADR 007 chose polling for the *fleet hub*; it still holds there.
notify needs push latency and a server-held queue, so the device holds
the socket instead.

Candidates on pioarduino (Arduino-ESP32 3.x precompiled IDF):

- links2004/WebSockets: a new registry dep, runs from `loop()` (a TLS
  handshake there blocks the UI), uses `String` internally, and its CA
  bundle path wants an app-embedded bundle file.
- `esp_websocket_client`: not in the precompiled libs; vendoring it adds
  ~1.5 kLOC plus its own reconnect policy we'd have to fight.
- `esp_transport_ws` (the layer `esp_websocket_client` is built on): already
  linked (`CONFIG_WS_TRANSPORT=1`), exposes upgrade status, opcodes, FIN
  and raw sends, and plugs into `esp_transport_ssl`.

The precompiled `libmbedtls.a` embeds IDF's full default certificate bundle
(`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`, ~67 KB flash), so
`esp_crt_bundle_attach` works with no extra files. `CONFIG_ESP_TLS_INSECURE`
is off in the precompiled sdkconfig, so esp-tls cannot skip verification.

## Decision

Use `esp_transport_ws` directly, zero new dependencies. One task owns the
socket and follows transport-http's shape (SPSC rings, `ByteSource`,
loop never blocks). `propagate_control_frames` is on so CLOSE frames (and
their 4001 code) reach the task, which answers PINGs itself. `wss://`
always attaches the CA bundle; LAN dev uses plain `ws://` (wrangler dev).
The heartbeat, backoff and stop rules are implemented exactly as in the
contract.

## Consequences

No new dep, no app-embedded certs, and Cloudflare's rotating chains
(Google Trust Services, Let's Encrypt, …) verify without pinning. Costs:
the task's 8 KB stack once the link is configured, and ~40 KB heap during
a TLS session (the C6 glance build has room; check buddy before composing
it there). Behavior on the wire is our code, so its rules live in
`docs/connectivity.md` and are covered by the local mock
(`tools/notify_mock.mjs`). TLS against the real server is not yet
field-verified.

## Related

ADR 002 (heap floor), ADR 007 (the hub keeps polling).
