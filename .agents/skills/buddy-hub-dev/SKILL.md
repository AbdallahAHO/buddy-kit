---
name: buddy-hub-dev
description: Run the buddy-kit fleet hub (Cloudflare Worker) locally and connect a device to it — wrangler dev, D1 init, PUBLIC_URL gotcha, device registration. Use when developing cloud/, testing the hub, registering a device, debugging /poll //push, or when a device won't appear in the dashboard.
---

# buddy: local fleet hub loop

The Worker lives in `cloud/`; full contract in `../../docs/cloud.md`
(routes, auth, fleet OTA flow) and `../../docs/protocol.md` (the
/poll + /push lines).

## Start it

```bash
cd cloud
pnpm install
pnpm run db:init:local     # apply schema.sql to local D1
pnpm run dev               # wrangler dev on 0.0.0.0:8787 (LAN-reachable)
```

## Top gotcha: PUBLIC_URL must be the LAN IP

OTA URLs are built from `PUBLIC_URL` (else the request origin). Devices
cannot reach `localhost` — set `PUBLIC_URL` to the LAN IP of the machine
running `wrangler dev` (in `wrangler.jsonc` dev vars), or broadcast OTA
hands devices a URL they can't fetch.

## Register a device

Over USB or BLE:

```
{"cmd":"hub","url":"http://<your-lan-ip>:8787","token":"dev-fleet-key"}
```

Persists to NVS (`buddy/huburl`); the device appears in the dashboard
(`http://localhost:8787/`, admin key once) within one poll.

## How it moves

Every 1 s while Wi-Fi is ONLINE and a hub URL is set, the device drains
its TX ring → `POST /push`, then `GET /poll` → RX ring, feeding the same
LineFramer as USB/BLE — so all commands, acks, and file push work over
the hub unchanged. Polling-not-WebSockets rationale:
`../../docs/decisions/007-devices-poll-the-hub.md`.

Hub health shows in the device's status ack and info pages. Plain
`http://` only for now — a deployed `https://` Worker needs the
WiFiClientSecure device change (see docs/cloud.md, TLS note).
