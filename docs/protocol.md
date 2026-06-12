# Wire protocol

Everything on every transport is **UTF-8 JSON, one object per line, `\n`
terminated** — over USB serial, BLE Nordic UART Service, or the HTTP hub.
The desktop side is documented in upstream's REFERENCE.md; this doc covers
what the device speaks, including buddy-kit's additions.

## BLE transport

Nordic UART Service: service `6e400001-…`, RX write `6e400002-…`, TX notify
`6e400003-…`. Advertises `Claude-XXXX` (BT MAC suffix). LE Secure
Connections bonding with a 6-digit on-screen passkey; characteristics are
encrypted-only. Notifies are chunked to the negotiated MTU.

## Heartbeat snapshot (desktop → device)

```json
{"total":3,"running":1,"waiting":1,"msg":"approve: Bash",
 "entries":["10:42 git push","..."],"tokens":184502,"tokens_today":31200,
 "prompt":{"id":"req_abc","tool":"Bash","hint":"rm -rf /tmp/foo"}}
```

Sent on change + ~10 s keepalive. No snapshot for 30 s = treat as
disconnected. `tokens` is cumulative since the bridge started (see
data-flow.md for the delta latch). One-shots on connect: `{"time":[epoch,
tzOffsetSec]}` and `{"cmd":"owner","name":"…"}`.

## Commands (any transport) and their acks

Every `cmd` gets `{"ack":"<cmd>","ok":bool,"n":uint}` (+`"error"` on
failure). Buddy-kit additions are marked ★.

| Command | Effect |
|---|---|
| `{"cmd":"status"}` | full status blob: name/owner/sec, bat, sys, stats, ★wifi `{state,ssid,ip}`, ★hub `{url,ok}`, ★jiggler `{on,hid}`, ★ota `{slot}` |
| `{"cmd":"name","name":…}` | set pet name (sanitized, NVS) |
| `{"cmd":"owner","name":…}` | set owner name |
| `{"cmd":"species","idx":n}` | select ASCII species; `255` = GIF mode |
| `{"cmd":"unpair"}` | erase BLE bonds |
| `{"cmd":"permission","id":…,"decision":"once"\|"deny"}` | device → desktop (sent, not received) |
| ★`{"cmd":"wifi","ssid":…,"pass":…}` | join now; persists on success |
| ★`{"cmd":"wifi","portal":true}` | enter pairing mode + show the QR screen |
| ★`{"cmd":"wifi","forget":true}` | wipe creds, radio off |
| ★`{"cmd":"hub","url":"http://…"}` | poll that hub (persisted, survives reboot) |
| ★`{"cmd":"hub","off":true}` | stop + forget the hub |
| ★`{"cmd":"jiggler","on":bool}` | BLE mouse jiggler mode (persisted); status reports `jiggler {on,hid}` |
| ★`{"cmd":"ota","url":"http://…/x.bin"}` | pull + flash + reboot (see docs/ota.md); status reports `ota {slot}` |
| ★`{"cmd":"vdp","on":bool}` | stream the canvas as dirty stripes (USB only); `"full":true` forces a keyframe |

## Folder push (character install)

```
char_begin{name,total} → file{path,size} → chunk{d:base64}* → file_end → … → char_end
```

Every chunk is acked with bytes-written — **the per-chunk ack is the flow
control** (flash erases block; don't batch). The fit check runs before
anything is wiped; only one character set lives on LittleFS at a time
(`/characters/<name>/`, wipe-all on begin). `char_end` activates the set
(manifest.json → GIF player) and flips species NVS to GIF mode.

## ★ BLE HID mouse (jiggler mode)

`lib/hid-mouse` attaches a HID-over-GATT mouse to the same GATT server and
bond store as the NUS bridge — one device identity. While enabled
(Settings → jiggler, or the cmd above) the advertisement gains the HID
service UUID (0x1812) + mouse appearance, so macOS Bluetooth settings lists
the device as an input device; pairing uses the same on-screen passkey.
The app's jiggle policy: ±1 px net-zero nudge every 30 s while a host is
subscribed. Settings value column shows `off` / `adv` / ` up` (host
connected). NimBLE builds only; Bluedroid gets no-op stubs.

## ★ Hub HTTP contract (lib/transport-http)

The device polls a hub once per second while Wi-Fi is online:

```
GET  <base>/poll   → 200 + zero-or-more \n-terminated JSON lines (or 204)
POST <base>/push   ← \n-terminated JSON lines from the device (acks, decisions)
```

When the hub is a fleet server (see cloud.md), the device also sends
`Authorization: Bearer <token>` and identity headers `X-Device-Id` /
`X-Model` / `X-Fw` on every poll/push, set via `{"cmd":"hub","url":…,
"token":…}`. The plain local hub ignores them.

That's the whole contract — an M3 Cloudflare Worker only needs these two
routes. Local dev hub: `python3 tools/test_hub.py` (+ `POST /queue` to
enqueue lines for the device).

## ★ Virtual display (lib/virtual-display)

A framebuffer tee, not a renderer: the lego hashes the logical canvas in
2-row stripes and ships *changed* stripes as JSON lines, so a client paints
the exact pixels the firmware rendered (ADR 011):

```
host → device   {"cmd":"vdp","on":true|false}   start (keyframe first) / stop
                {"cmd":"vdp","full":true}       force a full keyframe
device → host   {"vdp":"info","w":184,"h":224,"sh":2}
                {"vdp":"s","y":12,"b":"<base64 RGB565-LE, sh rows>"}
```

Frames are paced (≤3 stripes ≈ 3 KB per loop tick; a full 184×224 keyframe
lands in ~2 s) and may tear across stripes mid-animation — fine for a
preview. The app routes frames to **USB only**: BLE NUS would fragment the
~1 KB lines and the hub poll would flood. Browser client:
`lib/virtual-display/viewer/index.html` (WebSerial; mind the DTR reset).

## Dev tools (apps/buddy/tools/)

| Tool | Proves |
|---|---|
| `test_serial.py` | persona reacts to snapshots over USB |
| `test_xfer.py` | folder-push protocol over USB (see hardware.md for the C6 ack quirk) |
| `test_hub.py` | the hub poll/push loop end-to-end |
| `prep_character.py` / `flash_character.py` | build + flash GIF character sets |
