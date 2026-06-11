# Cloud — fleet hub & OTA (`cloud/`)

A Cloudflare Worker that *is* the device hub (the `/poll` + `/push` contract
from [protocol.md](protocol.md)) **and** a fleet-management API: a registry of
devices, firmware storage, and broadcast OTA. This is the M3 endgame — with
a hub in the cloud, the buddy needs no desktop and no USB; it polls for work
and pulls its own firmware updates.

## Pieces (Cloudflare primitives)

```
cloud/
  src/index.ts      Hono app — routes + auth
  src/hub.ts        DeviceHub Durable Object: one per device, holds its command queue
  src/dashboard.ts  single-page fleet view (HTML)
  schema.sql        D1 tables
  wrangler.jsonc    bindings + dev vars
```

| Primitive | Role |
|---|---|
| **Durable Object** `DeviceHub` | per-device command queue (`/drain` on poll, `/enqueue` from admin). One DO instance per device id — the only realtime state. |
| **D1** `devices`, `firmware` | registry + presence (auto-upserted from poll headers) and the firmware version index. Queryable for the dashboard. |
| **R2** `buddy-firmware` | the `.bin` blobs. |

## Auth model

- **Device** → `Bearer DEVICE_KEY` (shared fleet key) on `/poll` and `/push`.
  Firmware download (`/fw/:version`) is `?t=DEVICE_KEY` instead of a header,
  so the device's plain-GET OTA pull needs no auth code.
- **Admin** → `Bearer ADMIN_KEY` (or `?k=` for the dashboard) on `/v1/*`.

Per-device token issuance (rotate/revoke one device) is the obvious next
hardening step; v1 uses one fleet key for simplicity. Set both as secrets in
production: `wrangler secret put DEVICE_KEY` / `ADMIN_KEY`.

## Routes

| Method · path | Auth | Purpose |
|---|---|---|
| `GET /poll` | device | drain queued commands; auto-registers + refreshes presence from `X-Device-Id`/`X-Model`/`X-Fw` |
| `POST /push` | device | device → cloud lines (acks, decisions); stashes the latest status |
| `GET /fw/:version?t=` | `?t=` | stream the firmware blob from R2 |
| `GET /v1/devices` | admin | fleet list (id, model, fw, last-seen, ip) |
| `GET /v1/firmware` | admin | firmware versions |
| `POST /v1/firmware?version=X` | admin | upload a `.bin` (raw body) → R2 + index |
| `POST /v1/ota/broadcast` `{version}` | admin | enqueue an OTA command to **every** device |
| `POST /v1/devices/:id/cmd` | admin | enqueue one command to one device |
| `GET /` | — | dashboard (asks for the admin key once, kept in localStorage) |

The device side is unchanged from the local hub: it already speaks
`/poll`+`/push` (transport-http) and pulls OTA URLs it's handed (lib/ota).
Fleet integration was purely additive — auth + identity headers
(see [protocol.md](protocol.md) and the device's `transport-http`).

## How a fleet OTA flows

```
admin: POST /v1/firmware?version=0.3.0  (body = firmware.bin)   → R2 + D1
admin: POST /v1/ota/broadcast {version:"0.3.0"}                 → enqueue to every DeviceHub
device: GET /poll                        → {"cmd":"ota","url":".../fw/0.3.0?t=KEY"}
device: lib/ota pulls that URL           → Worker streams the blob from R2
device: flashes inactive slot, reboots   → reports fw 0.3.0 on its next poll
dashboard: device row flips to 0.3.0
```

The OTA URL is built from `PUBLIC_URL` (env) if set, else the request origin.
**Set `PUBLIC_URL`** to the Worker's reachable address — in dev that's the LAN
IP of the machine running `wrangler dev` (devices can't reach `localhost`);
in prod it's the `workers.dev`/custom-domain URL.

## Local development

```bash
cd cloud
pnpm install
pnpm run db:init:local                 # apply schema.sql to local D1
pnpm run dev                           # wrangler dev on 0.0.0.0:8787 (LAN-reachable, plain HTTP)
```

Point a device at it (over USB/BLE):

```
{"cmd":"hub","url":"http://<your-lan-ip>:8787","token":"dev-fleet-key"}
```

The device appears in the dashboard within a poll; upload a firmware version
and broadcast to push it. Verified end-to-end on the C6: register → view →
broadcast 0.3.0 → device pulls → reboots ota_0→ota_1 → fleet shows 0.3.0.

## Production deploy (when ready)

```bash
cd cloud
wrangler login
wrangler d1 create buddy_fleet            # paste the id into wrangler.jsonc
wrangler r2 bucket create buddy-firmware
wrangler d1 execute buddy_fleet --remote --file=schema.sql
wrangler secret put DEVICE_KEY            # and ADMIN_KEY
# set PUBLIC_URL to the deployed https URL (var or secret)
wrangler deploy
```

**TLS note:** a deployed Worker is `https://`. The device's `transport-http`
and `lib/ota` currently do plain HTTP — talking to the live Worker needs
`WiFiClientSecure` (~40 KB heap; `setInsecure()` skips cert pinning, fine for
a hobby fleet). That HTTPS upgrade is the one remaining device change for
production; everything else is proven over the LAN.
