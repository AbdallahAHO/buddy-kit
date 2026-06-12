# Connectivity

Two independent pieces: **wifi-link** (getting online, a lifecycle manager —
not a transport) and **transport-http** (a ByteSource that rides on it).

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

## Coexistence budget (C6)

BLE (NimBLE) + Wi-Fi STA + portal all run together: ~108 KB heap free during
the portal, ~150 KB with the portal closed. The portal tears down its DNS +
HTTP servers and the AP when it closes; watch heap in the status ack if you
add anything here.
