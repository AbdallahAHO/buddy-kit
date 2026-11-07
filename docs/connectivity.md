# Connectivity

Three independent pieces: **wifi-link** (getting online, a lifecycle
manager — not a transport) and two ByteSources that ride on it:
**transport-http** (the hub poll) and **transport-ws** (the notify socket).

## wifi-link states

```
            wifiLinkStartPortal()                join ok
  OFF ───────────────────────────→ PORTAL ─────┐
   │                                  │        │
   │ wifiLinkConnect()/Join()         │ creds  ▼
   └────────────→ JOINING ────────────┴──→ ONLINE ──(link lost)→ JOINING
                     │ fail/20s                │
                     ▼                         │ wifiLinkDisconnect/Forget
                  FAILED (portal up)           ▼
                  or OFF (headless)           OFF
```

All transitions happen in `wifiLinkTick()` — call it every loop. On a
dropped link the device silently rejoins once. The app adds boot
resilience on top: while the wifi setting is on and creds exist, an OFF
radio retries the join every 5 minutes (first retry immediate), so a
router reboot or a failed boot join can't park the device offline forever.

## Pairing UX (the QR flow)

1. Settings → **wifi setup** (or `{"cmd":"wifi","portal":true}`) → SoftAP
   `Buddy-XXXX` (WPA2, pass `buddyXXXX` — both derived from the MAC) and the
   screen shows a `WIFI:` QR any phone camera can join, with the creds as
   text fallback and a live status line.
2. Captive portal (DNS hijack → any URL): scanned-SSID dropdown + password,
   live status polling ("Connecting… / Connected! Buddy is at x.x.x.x /
   Failed: wrong password?"). Endpoints: `/`, `/scan`, `/save`, `/status`,
   plus the iOS/Android/Windows captive-detection paths.
3. On success: creds persist (injected store → NVS `buddy/wssid,wpass`),
   the AP lingers 8 s so the phone sees "online", the device screen
   auto-closes after 5 s, and **`settings.wifi` auto-enables** so the device
   reconnects on every boot.
4. Failure keeps the portal up with the error shown on both ends.

Recovery/restart paths: the same menu entry re-enters pairing any time;
Settings → reset → **forget wifi** (arm-to-confirm) wipes creds; factory
reset clears them too; `{"cmd":"wifi",…}` does all of it scriptably.

The STA stays usable while the AP is up (`WIFI_AP_STA`), so the portal can
scan and join without tearing down the phone's session.

## transport-http (the hub)

A FreeRTOS task (the loop thread never blocks on HTTP — the poll-don't-hold
rationale is [ADR 007](decisions/007-devices-poll-the-hub.md)):

```
every 1 s, while wifi ONLINE and a hub URL is set:
  1. drain TX ring  → POST <hub>/push   (acks, permission decisions)
  2. GET <hub>/poll → fill RX ring      (commands, snapshots)
```

Rings are single-producer/single-consumer (lock-free on 32-bit indexes);
the RX side feeds the same LineFramer path as USB/BLE, so **everything**
works over the hub unchanged. `{"cmd":"hub","url":…}` configures + persists
(NVS `buddy/huburl`); health shows in the status ack and on the info pages.

Current limitation: plain `http://` only — fine on the LAN; the M3 Worker
needs TLS (WiFiClientSecure, ~40 KB RAM) which is not wired yet.

## transport-ws (the notify socket)

A held WebSocket for notify v1 ([notify-protocol.md](notify-protocol.md)),
built on IDF's `esp_transport_ws` — no new dependency
([ADR 014](decisions/014-notify-socket-on-idf-ws-transport.md)). One
FreeRTOS task (8 KB stack, created on the first `wsTransportStart`) owns the
socket; the loop only touches the rings:

```
while a url is set, not revoked, and wifi is ONLINE:
  connect (Authorization: Bearer <token> on the upgrade, 10 s timeout)
  loop every ≤100 ms:
    every 30 s → text "ping"            any frame in → liveness
    75 s with nothing in → close + reconnect
    TX ring  → one text frame per \n-terminated line (newline stripped)
    frame in → RX ring + '\n' on FIN    (PING → PONG, CLOSE → read code)
  on close: 4001 → stop until the next wsTransportStart
            else backoff 1 s·2^n, cap 60 s, ±20 % jitter;
                 n resets after a connection that stayed up ≥ 60 s
```

- `wss://` verifies the server against IDF's built-in CA bundle; there is
  no insecure mode (the precompiled esp-tls can't skip verification). Use
  `ws://` for LAN dev against `wrangler dev` or `tools/notify_mock.mjs`.
- 401/429 on the upgrade back off like any other failure; only close code
  4001 stops the task.
- Writes are all-or-nothing and only accepted while connected, so a line
  is never split and offline status acks don't flush as stale junk.
- A reconfigure (`notifylink`) drops the live session and reconnects
  immediately with a fresh backoff.

Muma heap: 284.9 KB free at boot with notify unconfigured, 276.2 KB once
the task exists (parked, Wi-Fi off). A TLS session adds ~40 KB while up.

## notify pairing (apps/glance/pair_link.cpp + lib/notify-pair)

`{"cmd":"notifylink","url":…,"pair":true}` runs the contract's device-code
flow from a `pair_link` task (8 KB stack, exits when the pairing ends),
never the loop. HTTP is IDF's `esp_http_client` with fixed buffers;
`https://` attaches the same CA bundle as `wss://`. The state machine
(base URL `wss://h/…` → `https://h`, reply handling, code formatting,
deadline) is pure and native-tested; the task only does I/O and feeds
results back under a mutex. On approval the loop stores the token
(`secrets/notifytok`) and the `connect` url (`buddy/notifyurl`) and starts
transport-ws, replacing any previous link. The deviceSecret and token live
only in RAM until then and are zeroed after use.

## Improv Serial (USB Wi-Fi provisioning)

glance answers [Improv Serial](https://www.improv-wifi.com/serial/) v1 on
the USB port next to line-JSON
([ADR 015](decisions/015-improv-serial-shares-the-usb-line-port.md)):
a packet can only start where a line would, so JSON is never misread.

| RPC | Reply |
|---|---|
| 0x01 Wi-Fi settings | state *provisioning* → `wifiLinkJoin` (creds persist on success) → state *provisioned* + result with no URL (glance serves none), or error *unable to connect* + state *ready* |
| 0x02 get current state | *provisioned* (+ empty result) when online, else *ready* / *provisioning* |
| 0x03 get device info | `glance`, fw version, chip model, board name |
| anything else (scan, hostname, …) | error *unknown RPC* (ESP Web Tools falls back to typing the SSID) |
| bad checksum / malformed | error *invalid RPC* |

Improv has no identify RPC over serial. Replies go to USB only. The web
flasher manifest declares `improv: true` for glance, so ESP Web Tools shows
"Configure Wi-Fi" after install.

## Coexistence budget (C6)

BLE (NimBLE) + Wi-Fi STA + portal all run together: ~108 KB heap free during
the portal, ~150 KB with the portal closed. The portal tears down its DNS +
HTTP servers and the AP when it closes; watch heap in the status ack if you
add anything here.
