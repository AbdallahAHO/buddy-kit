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
