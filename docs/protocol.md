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
| `{"cmd":"status"}` | full status blob: name/owner/sec, bat, sys, stats, ★wifi `{state,ssid,ip}`, ★hub `{url,ok}`, ★jiggler `{on,hid}` |
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

That's the whole contract — an M3 Cloudflare Worker only needs these two
routes. Local dev hub: `python tools/test_hub.py` (+ `POST /queue` to
enqueue lines for the device).

## Dev tools (apps/buddy/tools/)

| Tool | Proves |
|---|---|
| `test_serial.py` | persona reacts to snapshots over USB |
| `test_xfer.py` | folder-push protocol over USB (see hardware.md for the C6 ack quirk) |
| `test_hub.py` | the hub poll/push loop end-to-end |
| `prep_character.py` / `flash_character.py` | build + flash GIF character sets |
