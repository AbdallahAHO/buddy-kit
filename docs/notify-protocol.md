# notify ↔ device protocol (v1)

The contract between the notify API (Cloudflare Worker + one Durable Object per device destination) and an enrolled
ESP32 device running buddy-kit firmware. Both sides implement exactly this; change it only by bumping `v`.

## Connection

- `GET /api/devices/connect` with `Upgrade: websocket` and `Authorization: Bearer <device token>`.
  - Dev (LAN): `ws://<mac-lan-ip>:8787/api/devices/connect` (`wrangler dev --ip 0.0.0.0 --port 8787`).
  - Prod: `wss://notify.aaho.cloud/api/devices/connect`.
- The device token is minted once by `POST /api/destinations {kind:"device"}` (format `dtk_<base64url>`; destination ids are `dst_…`, so the two can't be mixed up), stored by
  the server only as its sha256 hash, delivered to the device over USB/LAN only, kept on the device in NVS namespace
  `secrets` key `notifytok`, and never echoed by any status/ack.
- Server answers: `101` (accepted), `401` (unknown/revoked token), `426` (no upgrade header). At most 2 sockets per
  destination: a 3rd connection is accepted and the oldest is closed with code 4000.
- On accept the server sends every queued, unexpired delivery for this device as `notify` frames (oldest first).
- Reconnect after any close: exponential backoff 1 s → 2 s → 4 s … capped at 60 s, ±20 % jitter; reset to 1 s after a
  connection that stayed up ≥ 60 s. Close code 4001 (revoked) → stop reconnecting until re-enrolled.

## Frames

Every frame is one UTF-8 text message holding exactly one JSON object (no trailing newline on the wire). The device's
transport appends `\n` before feeding its line framer, so frames flow through the same `LineFramer`/dispatch as USB.

### Heartbeat (pinned bytes)

- Device sends the text `ping` (4 bytes, no newline) every 30 s; the server's Durable Object answers `pong` via
  `setWebSocketAutoResponse` (does not wake a hibernated object). No pong for 75 s → device closes and reconnects.
- `ping`/`pong` are not JSON: the device's framer ignores lines that don't start with `{`.

### Server → device

```json
{"cmd":"notify","v":1,"id":"ask_…","kind":"question","title":"Deploy reelandsheet?","body":"3 commits behind","actions":["Yes","No"],"ttl":540}
{"cmd":"notify","v":1,"id":"ask_…","retract":true,"reason":"answered"}
```

- `kind` ∈ `message` (no actions; dismiss only) · `notification` (no actions; acknowledge) · `approval` (actions
  exactly `["Approve","Deny"]` unless given) · `question` (1–4 `actions`).
- `ttl` = seconds left until the ask expires, computed at send time. The device drops it when `ttl` has passed
  (measured from receipt).
- `retract` removes it from the device if still shown/queued; `reason` ∈ `answered` (elsewhere) · `cancelled` · `expired`.
- Limits (the API rejects larger with problem+json; the device also truncates defensively): `title` ≤ 40 chars,
  `body` ≤ 160 chars, ≤ 4 `actions` each ≤ 12 chars, `id` ≤ 40 chars. ASCII is safest on the Muma's font.
- Existing commands (`status`, time sync) keep their current shapes and may also arrive over this socket.

### Device → server

```json
{"ack":"notify","id":"ask_…","state":"delivered"}
{"ack":"notify","id":"ask_…","state":"seen"}
{"ack":"notify","id":"ask_…","state":"answered","answer":"Yes"}
{"ack":"notify","id":"ask_…","state":"dismissed"}
```

- `delivered` on receipt (queued on device), `seen` when it is first on screen, `answered` with the exact action
  label chosen, `dismissed` for `message`/`notification` (acknowledged / dismissed).
- `answered`/`dismissed` are final; the server ignores later acks for the same id and acks on retracted ids.
- Any other line (`{"ack":"status",…}`) is accepted and may be stored as the device's last status.

## Server-side states (for reference)

delivery: `queued → delivered → seen → answered | dismissed | retracted | expired | failed`. An ask is answered by the
first final ack from any of its destinations; the others get `retract` with `reason:"answered"`.

## As built (firmware and server, v1)

Behaviour both sides settled on while implementing v1; the frames above are unchanged.

- **Inbox of 4.** The device holds at most 4 asks. A 5th `notify` gets no `delivered` ack and is otherwise ignored.
  The server keeps that delivery `queued` and sends every queued, open ask again (oldest first, at most 4 frames)
  whenever a slot may have freed: on connect, after any final ack (`answered`/`dismissed`) from that device, after a
  `retract` to it, and with each new ask. A repeated id re-acks `delivered`; the server treats that as a no-op.
- **Ids.** The device refuses ids over 40 characters; the server's ask ids are 16 (`ask_` + 12).
- **Offline answers are lost.** Lines the device writes while the socket is down are dropped, not replayed. An answer
  given offline never arrives: the delivery stays `seen` and the ask expires (or is answered elsewhere) as usual.
- **Liveness and backoff.** The device counts any inbound frame (`pong` or JSON) as liveness. A `401` or `429` on
  connect makes it back off; only close code 4001 stops it reconnecting.
- **`notifylink`** is the device's own local config command (sets the URL and token); it never crosses the socket.
- **Server → device commands** other than `notify` (`status`, time sync) are not sent by the server yet; device →
  server `{"ack":"status",…}` lines are stored as the destination's `lastStatus`.

Reference mock of the server side: `buddy-kit/tools/notify_mock.mjs`.

## Pairing over Wi-Fi (device code flow, RFC 8628 style)

A device that is online and knows the notify URL but has no token pairs without a cable: it shows a short code, the
owner types it into the console (signed in with an API key), and the device fetches its token.

1. Device → `POST /api/devices/pair` (no auth) with `{"model":"Spotpear Muma 1.54","fw":"0.5.0","hw":"ac:a7:04:22:90:84"}`
   → `201 {"pairingId":"prq_…","deviceSecret":"prs_…","code":"K7M-4QX","expiresIn":600,"interval":5}`.
   Rate-limited per client IP (10 an hour → 429 with `Retry-After`).
2. Device shows `code` large ("Enter at <console>") with the LED pulsing blue, and polls
   `GET /api/devices/pair/{pairingId}` with `Authorization: Bearer <deviceSecret>` every `interval` seconds:
   - `200 {"status":"pending"}`
   - `200 {"status":"approved","token":"dtk_…","destinationId":"dst_…","connect":"wss://<host>/api/devices/connect"}`:
     returned **once**; the device stores the token (NVS `secrets/notifytok`) and dials `connect`. Later polls → `410`.
   - `200 {"status":"expired"}` after `expiresIn`, or `404` for an unknown id or a wrong secret; the device stops and
     shows "pairing expired".
3. Owner → `POST /api/devices/pair/approve` (API key) with `{"code":"K7M-4QX","name":"Desk Muma","tags":["desk"]}`
   → `201` the new destination (no token in this response). An unknown or expired code → `404`; approve attempts are
   rate-limited per key.

- Codes: 6 characters of Crockford base32 without `0 O 1 I L U`, shown as `XXX-XXX`, matched case-insensitively with
  the dash optional, single-use, valid 10 minutes.
- The device derives the HTTPS base from its configured URL (`wss://host/api/devices/connect` → `https://host`;
  `ws://` → `http://` for LAN dev). Device command: `{"cmd":"notifylink","url":"<ws(s) url>","pair":true}` starts
  pairing (it replaces any stored token once approved).
